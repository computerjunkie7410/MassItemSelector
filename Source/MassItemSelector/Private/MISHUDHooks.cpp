#include "MISHUDHooks.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"

#define LOCTEXT_NAMESPACE "ItemSelector"

DEFINE_LOG_CATEGORY_STATIC(LogMISHUD, Log, All);

namespace
{
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

bool UpdateHintArray(FScriptArrayHelper& Hints, FTextProperty* Action, FTextProperty* Key, bool bRemove,
    const FText& ActionText, const TCHAR* KeyText)
{
    for (int32 I = Hints.Num() - 1; I >= 0; --I)
    {
        const bool bOurs = Action->GetPropertyValue_InContainer(Hints.GetRawPtr(I)).EqualTo(ActionText) &&
            Key->GetPropertyValue_InContainer(Hints.GetRawPtr(I)).ToString() == KeyText;
        if (!bOurs) continue;
        if (!bRemove) return false;
        Hints.RemoveValues(I);
        return true;
    }
    if (bRemove) return false;
    uint8* Entry = Hints.GetRawPtr(Hints.AddValue());
    Action->SetPropertyValue_InContainer(Entry, ActionText);
    Key->SetPropertyValue_InContainer(Entry, FText::FromString(KeyText));
    return true;
}

FArrayProperty* FindSetterArray(UFunction* Setter, FArrayProperty* Cache)
{
    if (!Setter || !Cache) return nullptr;
    for (TFieldIterator<FArrayProperty> It(Setter); It; ++It)
        if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_ReturnParm) && It->Inner->SameType(Cache->Inner)) return *It;
    return nullptr;
}
}

bool UMISHUDHooks::UpdateMassSelectionHint(UObject* Target, bool bRemove)
{
    auto* RefundsProperty = Target ? FindFProperty<FObjectPropertyBase>(Target->GetClass(), TEXT("Widget_DismantleRefunds")) : nullptr;
    UObject* Refunds = RefundsProperty ? RefundsProperty->GetObjectPropertyValue_InContainer(Target) : nullptr;
    auto* Cache = Refunds ? FindFProperty<FArrayProperty>(Refunds->GetClass(), TEXT("mCachedKeybinds")) : nullptr;
    FTextProperty* Action = nullptr;
    FTextProperty* Key = nullptr;
    UFunction* Setter = Refunds ? Refunds->FindFunction(TEXT("SetKeybindingHints")) : nullptr;
    FArrayProperty* Input = FindSetterArray(Setter, Cache);
    if (!Input || !FindHintFields(Cache, Action, Key)) return false;
    FScriptArrayHelper Hints(Cache, Cache->ContainerPtrToValuePtr<void>(Refunds));
    const bool bMassChanged = UpdateHintArray(Hints, Action, Key, bRemove, LOCTEXT("MassSelection", "Mass Selection"), TEXT("M"));
    const bool bPanelChanged = UpdateHintArray(Hints, Action, Key, bRemove, LOCTEXT("SelectionTypes", "Advanced Dismantle Filter"), TEXT("N"));
    if (!bMassChanged && !bPanelChanged) return false;
    FStructOnScope Params(Setter);
    Input->CopyCompleteValue(Input->ContainerPtrToValuePtr<void>(Params.GetStructMemory()), Cache->ContainerPtrToValuePtr<void>(Refunds));
    Refunds->ProcessEvent(Setter, Params.GetStructMemory());
    UE_LOG(LogMISHUD, Log, TEXT("%s Mass Selection / M via %s.mCachedKeybinds -> SetKeybindingHints; %d total entries."),
        bRemove ? TEXT("Removed") : TEXT("Added"), *Refunds->GetName(), Hints.Num());
    return true;
}

#undef LOCTEXT_NAMESPACE
