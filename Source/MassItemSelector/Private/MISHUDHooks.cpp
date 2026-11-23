#include "MISHUDHooks.h"
#include "MISInputBindings.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
#include "Blueprint/UserWidget.h"
#include "UObject/Stack.h"

#define LOCTEXT_NAMESPACE "ItemSelector"

bool UMISHUDHooks::bUpdatingHints = false;

namespace
{
struct FNativeHintState
{
    FArrayProperty* Array = nullptr;
    TUniquePtr<FStructOnScope> NativeInput;
    TUniquePtr<FStructOnScope> RenderedInput;
};
TMap<TWeakObjectPtr<UObject>, FNativeHintState> NativeHintStates;

bool FindHintFields(FArrayProperty* Array, FTextProperty*& Action, FTextProperty*& Key)
{
    auto* Element = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
    if (!Element) return false;
    for (TFieldIterator<FTextProperty> It(Element->Struct); It; ++It)
    {
        if (It->GetName().StartsWith(TEXT("Action_"))) Action = *It;
        if (It->GetName().StartsWith(TEXT("KeyBinding_"))) Key = *It;
    }
    return Action && Key;
}

void UpdateHintArray(FScriptArrayHelper& Hints, FTextProperty* Action, FTextProperty* Key, bool bRemove,
    const FText& ActionText, const FText& KeyText)
{
    for (int32 I = Hints.Num() - 1; I >= 0; --I)
    {
        if (!Action->GetPropertyValue_InContainer(Hints.GetRawPtr(I)).EqualTo(ActionText)) continue;
        if (bRemove) Hints.RemoveValues(I);
        else if (!Key->GetPropertyValue_InContainer(Hints.GetRawPtr(I)).EqualTo(KeyText))
            Key->SetPropertyValue_InContainer(Hints.GetRawPtr(I), KeyText);
        return;
    }
    if (bRemove) return;
    uint8* Entry = Hints.GetRawPtr(Hints.AddValue());
    Action->SetPropertyValue_InContainer(Entry, ActionText);
    Key->SetPropertyValue_InContainer(Entry, KeyText);
}

FArrayProperty* FindSetterArray(UFunction* Setter, FArrayProperty* Cache)
{
    if (!Setter || !Cache) return nullptr;
    for (TFieldIterator<FArrayProperty> It(Setter); It; ++It)
        if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_ReturnParm) && It->Inner->SameType(Cache->Inner)) return *It;
    return nullptr;
}
}

void UMISHUDHooks::TrackNativeHints(UObject* Target, FArrayProperty* Array, const void* InitialHints)
{
    if (NativeHintStates.Contains(Target)) return;
    UFunction* Setter = Target->FindFunction(TEXT("SetKeybindingHints"));
    FArrayProperty* Input = FindSetterArray(Setter, Array);
    if (!Input) return;
    auto& State = NativeHintStates.Add(Target);
    State.Array = Input;
    State.NativeInput = MakeUnique<FStructOnScope>(Setter);
    State.RenderedInput = MakeUnique<FStructOnScope>(Setter);
    Input->CopyCompleteValue(Input->ContainerPtrToValuePtr<void>(State.NativeInput->GetStructMemory()), InitialHints);
    Input->CopyCompleteValue(Input->ContainerPtrToValuePtr<void>(State.RenderedInput->GetStructMemory()), InitialHints);
}

bool UMISHUDHooks::CheckNativeHints(UObject* Target, FArrayProperty* Array, const void* Hints)
{
    auto* State = NativeHintStates.Find(Target);
    if (!State || !Array || !Hints || !Array->Inner->SameType(State->Array->Inner)) return true;
    void* Native = State->Array->ContainerPtrToValuePtr<void>(State->NativeInput->GetStructMemory());
    void* Rendered = State->Array->ContainerPtrToValuePtr<void>(State->RenderedInput->GetStructMemory());
    if (HaveSameHints(Array, Hints, Rendered) || (!bUpdatingHints && HaveSameHints(Array, Hints, Native))) return false;
    if (!bUpdatingHints) State->Array->CopyCompleteValue(Native, Hints);
    State->Array->CopyCompleteValue(Rendered, Hints);
    return true;
}

bool UMISHUDHooks::CopyNativeHints(UObject* Target, FArrayProperty* Array, void* Hints)
{
    const auto* State = NativeHintStates.Find(Target);
    if (!State || !Array->Inner->SameType(State->Array->Inner)) return false;
    Array->CopyCompleteValue(Hints, State->Array->ContainerPtrToValuePtr<void>(State->NativeInput->GetStructMemory()));
    return true;
}

bool UMISHUDHooks::ShouldRebuildNativeHints(UObject*, const TArray<int32>&)
{
    checkNoEntry();
    return true;
}

DEFINE_FUNCTION(UMISHUDHooks::execShouldRebuildNativeHints)
{
    P_GET_OBJECT(UObject, Target);
    Stack.MostRecentProperty = nullptr;
    Stack.StepCompiledIn<FArrayProperty>(nullptr);
    auto* Array = CastField<FArrayProperty>(Stack.MostRecentProperty);
    const void* Hints = Stack.MostRecentPropertyAddress;
    P_FINISH;
    P_NATIVE_BEGIN;
    *static_cast<bool*>(RESULT_PARAM) = CheckNativeHints(Target, Array, Hints);
    P_NATIVE_END;
}

void UMISHUDHooks::ClearNativeHintState()
{
    NativeHintStates.Reset();
}

bool UMISHUDHooks::HaveSameHints(FArrayProperty* Array, const void* A, const void* B)
{
    FScriptArrayHelper Left(Array, A);
    FScriptArrayHelper Right(Array, B);
    if (Left.Num() != Right.Num()) return false;
    const auto* Element = CastFieldChecked<FStructProperty>(Array->Inner);
    for (int32 Index = 0; Index < Left.Num(); ++Index)
    {
        for (TFieldIterator<FProperty> It(Element->Struct); It; ++It)
        {
            if (const auto* Text = CastField<FTextProperty>(*It))
            {
                if (!Text->GetPropertyValue_InContainer(Left.GetRawPtr(Index)).EqualTo(
                    Text->GetPropertyValue_InContainer(Right.GetRawPtr(Index)))) return false;
            }
            else if (!It->Identical_InContainer(Left.GetRawPtr(Index), Right.GetRawPtr(Index))) return false;
        }
    }
    return true;
}

void UMISHUDHooks::UpdateDismantleHints(UObject* Target, EMISDismantleHints Mode, TSharedPtr<FStructOnScope>& SavedHints, APlayerController* Player, const MIS::FDismantleBindings& Bindings)
{
    auto* RefundsProperty = Target ? FindFProperty<FObjectPropertyBase>(Target->GetClass(), TEXT("Widget_DismantleRefunds")) : nullptr;
    UObject* Refunds = RefundsProperty ? RefundsProperty->GetObjectPropertyValue_InContainer(Target) : nullptr;
    auto* Cache = Refunds ? FindFProperty<FArrayProperty>(Refunds->GetClass(), TEXT("mCachedKeybinds")) : nullptr;
    FTextProperty* Action = nullptr;
    FTextProperty* Key = nullptr;
    UFunction* Setter = Refunds ? Refunds->FindFunction(TEXT("SetKeybindingHints")) : nullptr;
    FArrayProperty* Input = FindSetterArray(Setter, Cache);
    if (!Input || !FindHintFields(Cache, Action, Key)) return;
    UObject* HintContainer = nullptr;
    if (auto* Widget = Cast<UUserWidget>(Refunds))
    {
        if (auto* Hints = Widget->GetWidgetFromName(TEXT("mBuildHints")))
        {
            if (auto* NativeCache = FindFProperty<FArrayProperty>(Hints->GetClass(), TEXT("mKeybinds")))
            {
                HintContainer = Hints;
                TrackNativeHints(Hints, NativeCache, NativeCache->ContainerPtrToValuePtr<void>(Hints));
            }
        }
    }
    FStructOnScope Params(Setter);
    void* Desired = Input->ContainerPtrToValuePtr<void>(Params.GetStructMemory());
    const void* Current = Cache->ContainerPtrToValuePtr<void>(Refunds);
    const bool bLasso = Mode == EMISDismantleHints::Draw || Mode == EMISDismantleHints::Preview;
    if (bLasso)
    {
        if (!SavedHints)
        {
            SavedHints = MakeShared<FStructOnScope>(Setter);
            Input->CopyCompleteValue(Input->ContainerPtrToValuePtr<void>(SavedHints->GetStructMemory()), Current);
        }
        FScriptArrayHelper Hints(Input, Desired);
        auto AddHint = [&](const FText& Text, const FText& Binding) {
            UpdateHintArray(Hints, Action, Key, false, Text, Binding);
        };
        if (Mode == EMISDismantleHints::Draw)
        {
            AddHint(LOCTEXT("DrawLasso", "Select items"), FText::Format(LOCTEXT("DragInput", "Drag {0}"), Bindings.Primary.GetText(Player)));
        }
        else
        {
            AddHint(LOCTEXT("ApplyLasso", "Add selection"), Bindings.Primary.GetText(Player));
            AddHint(LOCTEXT("ClearLasso", "Clear lasso"), Bindings.Secondary.GetText(Player));
        }
        AddHint(LOCTEXT("LassoDistance", "Adjust distance"), LOCTEXT("MouseWheel", "Wheel"));
    }
    else
    {
        if (!CopyNativeHints(HintContainer, Input, Desired))
            Input->CopyCompleteValue(Desired, SavedHints
                ? Input->ContainerPtrToValuePtr<void>(SavedHints->GetStructMemory()) : Current);
        SavedHints.Reset();
        FScriptArrayHelper Hints(Input, Desired);
        const FText KeyHint = FText::Format(LOCTEXT("HoldLassoKey", "Hold {0}"),
            MIS::GetLassoKeyText(Player));
        UpdateHintArray(Hints, Action, Key, Mode != EMISDismantleHints::Default && Mode != EMISDismantleHints::Selection,
            LOCTEXT("LassoAction", "Lasso selection"), KeyHint);
        UpdateHintArray(Hints, Action, Key, Mode != EMISDismantleHints::Selection || !Bindings.SelectionFilter.Key.IsValid(),
            LOCTEXT("SelectionTypes", "Advanced Dismantle Filter"), Bindings.SelectionFilter.GetText(Player));
    }
    if (HaveSameHints(Input, Desired, Current)) return;
    Cache->CopyCompleteValue(Cache->ContainerPtrToValuePtr<void>(Refunds), Desired);
    TGuardValue<bool> UpdateGuard(bUpdatingHints, true);
    Refunds->ProcessEvent(Setter, Params.GetStructMemory());
}

#undef LOCTEXT_NAMESPACE
