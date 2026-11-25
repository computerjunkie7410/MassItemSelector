#if WITH_DEV_AUTOMATION_TESTS

#include "MISSelectionPanel.h"
#include "MISHUDHooks.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "UObject/StructOnScope.h"
#include "Patching/BlueprintHookBlueprint.h"
#include "Patching/BlueprintHookTargetSpecifiers.h"
#include "Toolkit/HookingCodeGeneration.h"
#include "Toolkit/ScriptExprAssembler.h"
#include "MISModule.h"
#include "MISConfiguration.h"
#include "MISInputBindings.h"
#include "Input/FGChildInputMappingContext.h"
#include "PlayerMappableKeySettings.h"
#include "InputTriggers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "FGGameFeatureData.h"
#include "MISGameFeatureData.h"
#include "Engine/AssetManagerTypes.h"
#include "Engine/AssetManager.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "MISOverlay.h"
#include "InputCoreTypes.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "FGRecipe.h"
#include "FGDismantleModeDescriptor.h"
#include "UObject/UnrealType.h"
#include "MISSelectionNames.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableWire.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/TextLocalizationManager.h"

namespace
{
UFGChildInputMappingContext* GetLassoContext()
{
    return LoadObject<UFGChildInputMappingContext>(nullptr, TEXT("/MassItemSelector/Inputs/IMC_MassItemSelector.IMC_MassItemSelector"));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISHintContentTest, "MassItemSelector.Selection.HintContent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISHintContentTest::RunTest(const FString&)
{
    UClass* Class = LoadClass<UUserWidget>(nullptr, TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/Widget_BuildMode.Widget_BuildMode_C"));
    if (!TestNotNull(TEXT("Native hint widget class is available"), Class)) return false;
    UFunction* Setter = Class->FindFunctionByName(TEXT("SetKeybindingHints"));
    if (!TestNotNull(TEXT("Native hint setter is available"), Setter)) return false;
    FArrayProperty* Array = nullptr;
    for (TFieldIterator<FArrayProperty> It(Setter); It; ++It)
        if (It->HasAnyPropertyFlags(CPF_Parm)) { Array = *It; break; }
    if (!TestNotNull(TEXT("Native hint array is available"), Array)) return false;
    auto* Element = CastFieldChecked<FStructProperty>(Array->Inner);
    FTextProperty* Action = nullptr;
    FTextProperty* Key = nullptr;
    for (TFieldIterator<FTextProperty> It(Element->Struct); It; ++It)
    {
        if (It->GetName().StartsWith(TEXT("Action_"))) Action = *It;
        if (It->GetName().StartsWith(TEXT("KeyBinding_"))) Key = *It;
    }
    if (!TestNotNull(TEXT("Native action text is available"), Action) ||
        !TestNotNull(TEXT("Native key text is available"), Key)) return false;
    FStructOnScope Left(Setter), Right(Setter);
    void* A = Array->ContainerPtrToValuePtr<void>(Left.GetStructMemory());
    void* B = Array->ContainerPtrToValuePtr<void>(Right.GetStructMemory());
    FScriptArrayHelper AHints(Array, A), BHints(Array, B);
    AHints.AddValues(2);
    BHints.AddValues(2);
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const TCHAR* Label = Index == 0 ? TEXT("Dismantle") : TEXT("Advanced Dismantle Filter");
        const TCHAR* Binding = Index == 0 ? TEXT("Hold LMB") : TEXT("N");
        Action->SetPropertyValue_InContainer(AHints.GetRawPtr(Index), FText::FromString(Label));
        Action->SetPropertyValue_InContainer(BHints.GetRawPtr(Index), FText::FromString(Label));
        Key->SetPropertyValue_InContainer(AHints.GetRawPtr(Index), FText::FromString(Binding));
        Key->SetPropertyValue_InContainer(BHints.GetRawPtr(Index), FText::FromString(Binding));
    }
    TestFalse(TEXT("Independent text histories differ under the game's identity comparison"),
        FTextProperty::Identical_Implementation(Action->GetPropertyValue_InContainer(AHints.GetRawPtr(0)),
            Action->GetPropertyValue_InContainer(BHints.GetRawPtr(0)), 0, FTextProperty::EIdenticalLexicalCompareMethod::None));
    TestTrue(TEXT("Recreated text with unchanged content keeps the existing HUD hints"), UMISHUDHooks::HaveSameHints(Array, A, B));
    Key->SetPropertyValue_InContainer(BHints.GetRawPtr(1), FText::FromString(TEXT("F6")));
    TestFalse(TEXT("Rebinding a shortcut requires a hint update"), UMISHUDHooks::HaveSameHints(Array, A, B));
    Key->SetPropertyValue_InContainer(BHints.GetRawPtr(1), FText::FromString(TEXT("N")));
    BHints.SwapValues(0, 1);
    TestFalse(TEXT("Changing hint order requires an update"), UMISHUDHooks::HaveSameHints(Array, A, B));
    BHints.SwapValues(0, 1);
    BHints.RemoveValues(1);
    TestFalse(TEXT("Removing a hint requires an update"), UMISHUDHooks::HaveSameHints(Array, A, B));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISNativeCounterTest, "MassItemSelector.Selection.NativeCounter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISNativeCounterTest::RunTest(const FString&)
{
    UClass* Class = LoadClass<UUserWidget>(nullptr,
        TEXT("/Game/FactoryGame/Interface/UI/InGame/Widget_DismantleMode.Widget_DismantleMode_C"));
    if (!TestNotNull(TEXT("Native dismantle HUD class exists"), Class)) return false;
    auto* PanelProperty = FindFProperty<FObjectPropertyBase>(Class, TEXT("mMassDismantleFeedback"));
    auto* NumberProperty = FindFProperty<FObjectPropertyBase>(Class, TEXT("mDismantleNumberText"));
    UFunction* Refresh = Class->FindFunctionByName(TEXT("UpdateMassDismantleFeedback"));
    if (!TestNotNull(TEXT("Native counter panel property exists"), PanelProperty) ||
        !TestNotNull(TEXT("Native counter text property exists"), NumberProperty) ||
        !TestNotNull(TEXT("Native counter refresh function exists"), Refresh)) return false;
    TestEqual(TEXT("Native counter refresh takes no parameters"), Refresh->NumParms, static_cast<uint8>(0));
    if (!TestTrue(TEXT("Native panel is a widget"), PanelProperty->PropertyClass->IsChildOf(UWidget::StaticClass())) ||
        !TestTrue(TEXT("Native number is a text block"), NumberProperty->PropertyClass->IsChildOf(UTextBlock::StaticClass()))) return false;
    auto* Target = NewObject<UUserWidget>(GetTransientPackage(), Class);
    auto* Panel = NewObject<UWidget>(Target, PanelProperty->PropertyClass);
    auto* Number = NewObject<UTextBlock>(Target, NumberProperty->PropertyClass);
    PanelProperty->SetObjectPropertyValue_InContainer(Target, Panel);
    NumberProperty->SetObjectPropertyValue_InContainer(Target, Number);
    const FText Original = FText::FromString(TEXT("Original native text"));
    Panel->SetVisibility(ESlateVisibility::Collapsed);
    Number->SetText(Original);
    ON_SCOPE_EXIT {
        UMISHUDHooks::UpdateDismantleCounter(Target, 0, 100000, false);
        UMISHUDHooks::ClearNativeHintState();
    };
    UMISHUDHooks::UpdateDismantleCounter(Target, 4094, 100000, false);
    TestEqual(TEXT("A selection reveals the native panel"), Panel->GetVisibility(), ESlateVisibility::HitTestInvisible);
    TestTrue(TEXT("The native text shows selected count and limit"), Number->GetText().EqualTo(
        FText::Format(FText::FromString(TEXT("{0} / {1}")), 4094, 100000)));
    UMISHUDHooks::UpdateDismantleCounter(Target, 12, 50000, false);
    TestTrue(TEXT("Removing selections and changing the limit updates the counter"), Number->GetText().EqualTo(
        FText::Format(FText::FromString(TEXT("{0} / {1}")), 12, 50000)));
    UMISHUDHooks::UpdateDismantleCounter(Target, 0, 50000, false);
    TestEqual(TEXT("An empty selection restores native panel visibility"), Panel->GetVisibility(), ESlateVisibility::Collapsed);
    TestTrue(TEXT("Releasing the counter restores unmodified native text"), Number->GetText().EqualTo(Original));
    UMISHUDHooks::UpdateDismantleCounter(Target, 2, 100000, false);
    UMISHUDHooks::UpdateDismantleCounter(Target, 2, 100000, true);
    TestEqual(TEXT("Native multi-select retains control of its visible panel"), Panel->GetVisibility(), ESlateVisibility::HitTestInvisible);
    Panel->SetVisibility(ESlateVisibility::Collapsed);
    auto* BoundText = NewObject<UTextBlock>(Target);
    BoundText->SetText(Original);
    Number->TextDelegate.BindUFunction(BoundText, TEXT("GetText"));
    UMISHUDHooks::UpdateDismantleCounter(Target, 3, 100000, false);
    TestTrue(TEXT("Native text bindings remain intact"), Number->TextDelegate.IsBound());
    Number->TextDelegate.Unbind();
    const FText NewNativeText = FText::FromString(TEXT("New native text"));
    Number->SetText(NewNativeText);
    Panel->SetVisibility(ESlateVisibility::Hidden);
    UMISHUDHooks::UpdateDismantleCounter(Target, 0, 100000, false);
    TestTrue(TEXT("Cleanup preserves newer native text"), Number->GetText().EqualTo(NewNativeText));
    TestEqual(TEXT("Cleanup preserves newer native visibility"), Panel->GetVisibility(), ESlateVisibility::Hidden);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISNativeHintGuardTest, "MassItemSelector.Selection.NativeHintGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISNativeHintGuardTest::RunTest(const FString&)
{
    auto* Class = Cast<UHookBlueprintGeneratedClass>(LoadClass<UObject>(nullptr,
        TEXT("/MassItemSelector/Hooks/Hook_DismantleHints_MassItemSelector.Hook_DismantleHints_MassItemSelector_C")));
    if (!TestNotNull(TEXT("The Blueprint Hook asset loads"), Class)) return false;
    if (!TestEqual(TEXT("One narrow native hint hook is registered"), Class->HookDescriptors.Num(), 1)) return false;
    const auto& Hook = Class->HookDescriptors[0];
    TestEqual(TEXT("The hook targets the native hint setter"), Hook.TargetFunction->GetFName(), FName(TEXT("SetKeybindingHints")));
    TestEqual(TEXT("The hook runs after flow-stack initialization"), Hook.InsertLocation, EBlueprintFunctionHookInsertLocation::AfterTarget);
    TestTrue(TEXT("The hook guards the function entry"), Hook.TargetSpecifier->IsA<UBlueprintHookTargetSpecifier_FunctionEntry>());
    TestTrue(TEXT("The client Game Instance Module registers the hook"),
        GetDefault<UMISGameInstanceModule>()->ClientBlueprintHooks.ContainsByPredicate([Class](const auto& Ref) {
            return Ref.ToSoftObjectPath() == FSoftObjectPath(Class);
        }));
    UFunction* Function = Hook.HookFunction;
    FStructOnScope Args(Function);
    auto* TargetProperty = FindFProperty<FObjectPropertyBase>(Function, TEXT("Target"));
    auto* Return = CastField<FBoolProperty>(Function->GetReturnProperty());
    FArrayProperty* Array = nullptr;
    for (TFieldIterator<FArrayProperty> It(Function); It; ++It)
        if (It->HasAnyPropertyFlags(CPF_Parm)) { Array = *It; break; }
    if (!TestNotNull(TEXT("The Blueprint captures the target"), TargetProperty) ||
        !TestNotNull(TEXT("The Blueprint captures the typed native hint array"), Array) ||
        !TestNotNull(TEXT("The Blueprint returns the rebuild decision"), Return)) return false;
    auto* Target = NewObject<UUserWidget>(GetTransientPackage(), Hook.TargetFunction->GetOuterUClass());
    TargetProperty->SetObjectPropertyValue_InContainer(Args.GetStructMemory(), Target);
    void* Data = Array->ContainerPtrToValuePtr<void>(Args.GetStructMemory());
    FScriptArrayHelper Hints(Array, Data);
    auto* Struct = CastFieldChecked<FStructProperty>(Array->Inner);
    FTextProperty* Action = nullptr;
    FTextProperty* Key = nullptr;
    for (TFieldIterator<FTextProperty> It(Struct->Struct); It; ++It)
    {
        if (It->GetName().StartsWith(TEXT("Action_"))) Action = *It;
        if (It->GetName().StartsWith(TEXT("KeyBinding_"))) Key = *It;
    }
    if (!TestNotNull(TEXT("The native hint action field is typed correctly"), Action) ||
        !TestNotNull(TEXT("The native hint key field is typed correctly"), Key)) return false;
    Hints.AddValue();
    Action->SetPropertyValue_InContainer(Hints.GetRawPtr(0), FText::FromString(TEXT("Dismantle")));
    Key->SetPropertyValue_InContainer(Hints.GetRawPtr(0), FText::FromString(TEXT("Hold LMB")));
    UMISHUDHooks::TrackNativeHints(Target, Array, Data);
    auto Invoke = [&] {
        Class->GetDefaultObject()->ProcessEvent(Function, Args.GetStructMemory());
        return Return->GetPropertyValue_InContainer(Args.GetStructMemory());
    };
    TestFalse(TEXT("The actual Blueprint hook skips identical native hints"), Invoke());
    Hints.AddValue();
    Action->SetPropertyValue_InContainer(Hints.GetRawPtr(1), FText::FromString(TEXT("Lasso selection")));
    Key->SetPropertyValue_InContainer(Hints.GetRawPtr(1), FText::FromString(TEXT("Hold Left Alt")));
    {
        TGuardValue<bool> ModUpdate(UMISHUDHooks::bUpdatingHints, true);
        TestTrue(TEXT("Adding the mod's hints updates the native row"), Invoke());
        TestFalse(TEXT("Identical mod hints do not rebuild the row again"), Invoke());
    }
    Hints.RemoveValues(1);
    TestFalse(TEXT("Native base hints do not erase and re-add the mod hints"), Invoke());
    Key->SetPropertyValue_InContainer(Hints.GetRawPtr(0), FText::FromString(TEXT("Hold F7")));
    TestTrue(TEXT("A real native binding change rebuilds the row"), Invoke());
    TestFalse(TEXT("The same changed native hints are skipped afterward"), Invoke());
    TestEqual(TEXT("The guard leaves its input array unchanged"), Hints.Num(), 1);
    FStructOnScope LatestNative(Function);
    void* Restored = Array->ContainerPtrToValuePtr<void>(LatestNative.GetStructMemory());
    TestTrue(TEXT("The latest native hints are available for restoration"),
        UMISHUDHooks::CopyNativeHints(Target, Array, Restored));
    Hints.AddValue();
    Action->SetPropertyValue_InContainer(Hints.GetRawPtr(1), FText::FromString(TEXT("Select items")));
    {
        TGuardValue<bool> ModUpdate(UMISHUDHooks::bUpdatingHints, true);
        TestTrue(TEXT("Lasso can replace the rendered hints"), Invoke());
    }
    TestTrue(TEXT("Lasso rendering preserves the latest native hints"),
        UMISHUDHooks::CopyNativeHints(Target, Array, Data));
    TestTrue(TEXT("Restoring hints uses the native update received during lasso"),
        UMISHUDHooks::HaveSameHints(Array, Data, Restored));
    {
        TGuardValue<bool> ModUpdate(UMISHUDHooks::bUpdatingHints, true);
        TestTrue(TEXT("Restoring the latest native hints rebuilds the row once"), Invoke());
        TestFalse(TEXT("Repeated restoration does not rebuild the row"), Invoke());
    }
    auto* OtherHUD = NewObject<UUserWidget>(GetTransientPackage(), Hook.TargetFunction->GetOuterUClass());
    TargetProperty->SetObjectPropertyValue_InContainer(Args.GetStructMemory(), OtherHUD);
    TestTrue(TEXT("Other native hint containers are unaffected"), Invoke());
    TargetProperty->SetObjectPropertyValue_InContainer(Args.GetStructMemory(), Target);
    UFunction* Setter = Hook.TargetFunction;
    auto* Marker = FindFProperty<FBoolProperty>(UUserWidget::StaticClass(), TEXT("bIsFocusable"));
    if (!TestNotNull(TEXT("The execution marker exists"), Marker)) return false;
    auto ReturnStatement = MakeShared<FScriptExpr>(EX_Return);
    ReturnStatement->Operands.Add(FScriptExprOperand(MakeShared<FScriptExpr>(EX_Nothing)));
    auto Push = MakeShared<FScriptExpr>(EX_PushExecutionFlow);
    Push->Operands.Add(FScriptExprOperand::CreateUnresolvedLabel(ReturnStatement));
    auto Variable = MakeShared<FScriptExpr>(EX_InstanceVariable);
    Variable->Operands.Add(Marker);
    auto Body = MakeShared<FScriptExpr>(EX_LetBool);
    Body->Operands.Add(FScriptExprOperand(Variable));
    Body->Operands.Add(FScriptExprOperand(MakeShared<FScriptExpr>(EX_True)));
    TArray<TSharedPtr<FScriptExpr>> Script = {
        Push, Body, MakeShared<FScriptExpr>(EX_PopExecutionFlow), ReturnStatement, MakeShared<FScriptExpr>(EX_EndOfScript)
    };
    FHookCodeGenFunctionContext Context(Setter, Script);
    Context.ResolveHookTargets({Hook});
    TArray<TSharedPtr<FScriptExpr>> Patched;
    TMap<UFunction*, TSharedPtr<FScriptExpr>> EntryPoints;
    if (!TestTrue(TEXT("SML generates the patched function"), Context.GenerateFunctionCode(Patched, EntryPoints))) return false;
    TestEqual(TEXT("Flow-stack initialization stays first"), Patched[0]->Opcode, static_cast<uint8>(EX_PushExecutionFlow));
    TestEqual(TEXT("The guard runs after the stack is initialized"), Patched[1]->Opcode, static_cast<uint8>(EX_PopExecutionFlowIfNot));
    TArray<uint8> Bytecode;
    FScriptExprAssembler::AssembleFunction(Patched, Bytecode);
    TGuardValue<TArray<uint8>> RestoreScript(Setter->Script, MoveTemp(Bytecode));
    FStructOnScope NativeArgs(Setter);
    auto* NativeArray = FindFProperty<FArrayProperty>(Setter, TEXT("mKeyBindingHints"));
    if (!TestNotNull(TEXT("The native setter parameter exists"), NativeArray)) return false;
    auto RunPatchedSetter = [&] {
        NativeArray->CopyCompleteValue(NativeArray->ContainerPtrToValuePtr<void>(NativeArgs.GetStructMemory()), Data);
        Marker->SetPropertyValue_InContainer(Target, false);
        Target->ProcessEvent(Setter, NativeArgs.GetStructMemory());
        return Marker->GetPropertyValue_InContainer(Target);
    };
    TestFalse(TEXT("Identical hints skip the patched body without an empty-stack pop"), RunPatchedSetter());
    Key->SetPropertyValue_InContainer(Hints.GetRawPtr(0), FText::FromString(TEXT("Hold F8")));
    TestTrue(TEXT("Changed hints execute the patched body"), RunPatchedSetter());
    TestFalse(TEXT("Repeated changed hints skip the patched body"), RunPatchedSetter());
    UMISHUDHooks::ClearNativeHintState();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISSelectionIdentityTest, "MassItemSelector.Selection.Identity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISSelectionIdentityTest::RunTest(const FString&)
{
    FMISSelectionObject Original;
    Original.Actor = GetMutableDefault<AFGBuildable>();
    Original.Class = AFGBuildable::StaticClass();
    Original.LightweightIndex = 7;
    Original.Transform = FTransform::Identity;

    TMap<FMISSelectionKey, FMISSelectionObject> Excluded;
    Excluded.Add(Original.GetKey(), Original);
    auto Recreated = Original;
    Recreated.Actor = GetMutableDefault<AFGBuildableWire>();
    TestTrue(TEXT("A new temporary actor still identifies the original instance"), Original.SameObject(Recreated));
    TestNotNull(TEXT("The original exclusion is found through its instance key"), Excluded.Find(Recreated.GetKey()));

    auto Recycled = Recreated;
    Recycled.Transform.SetTranslation(FVector(1000, 0, 0));
    const auto* Previous = Excluded.Find(Recycled.GetKey());
    TestNotNull(TEXT("A reused index finds the old record for validation"), Previous);
    TestFalse(TEXT("A different transform cannot reuse that exclusion"), Previous && Previous->SameObject(Recycled));

    auto OtherClass = Original;
    OtherClass.Class = AFGBuildableWire::StaticClass();
    TestNull(TEXT("The same instance index on another class is distinct"), Excluded.Find(OtherClass.GetKey()));
    auto OtherIndex = Original;
    ++OtherIndex.LightweightIndex;
    TestNull(TEXT("Another index on the same class is distinct"), Excluded.Find(OtherIndex.GetKey()));

    auto Actor = Original;
    Actor.LightweightIndex = INDEX_NONE;
    auto MovedActor = Actor;
    MovedActor.Transform.SetTranslation(FVector(1000, 0, 0));
    TestTrue(TEXT("Moving a regular actor preserves its identity"), Actor.SameObject(MovedActor));
    auto OtherActor = Actor;
    OtherActor.Actor = GetMutableDefault<AFGBuildableWire>();
    TestFalse(TEXT("Different regular actors have distinct keys"), Actor.GetKey() == OtherActor.GetKey());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISSelectionSnapshotTest, "MassItemSelector.Selection.Snapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISSelectionSnapshotTest::RunTest(const FString&)
{
    FMISSelectionSnapshotEntry Original;
    Original.Object.Actor = GetMutableDefault<AFGBuildable>();
    Original.Object.Class = AFGBuildable::StaticClass();
    Original.Object.LightweightIndex = 7;
    Original.Object.Transform = FTransform::Identity;
    Original.bEligible = true;
    TestTrue(TEXT("An unchanged selection needs no regrouping"), Original.Matches(Original));

    auto Changed = Original;
    Changed.bEligible = false;
    TestFalse(TEXT("Dismantle eligibility changes trigger regrouping"), Original.Matches(Changed));
    Changed = Original;
    Changed.Object.Transform.SetTranslation(FVector(1000, 0, 0));
    TestFalse(TEXT("A recycled lightweight instance changes the snapshot"), Original.Matches(Changed));
    Changed = Original;
    Changed.Object.Actor = GetMutableDefault<AFGBuildableWire>();
    TestFalse(TEXT("A new temporary actor invalidates the actor lookup"), Original.Matches(Changed));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISSelectionNamesTest, "MassItemSelector.Selection.Names",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISSelectionNamesTest::RunTest(const FString&)
{
    auto CheckName = [this](const TCHAR* Name, const TCHAR* Material, const TCHAR* Expected) {
        TestEqual(Name, MISFormatSelectionTypeName(FText::FromString(Name), FText::FromString(Material)).ToString(), FString(Expected));
    };
    CheckName(TEXT("Foundation (2m)"), TEXT("Asphalt Foundation"), TEXT("Foundation (2m) (Asphalt Foundation)"));
    CheckName(TEXT("Foundation (2m)"), TEXT("Coated Concrete Foundation"), TEXT("Foundation (2m) (Coated Concrete Foundation)"));
    CheckName(TEXT("Foundation (2m)"), TEXT("Concrete Foundation"), TEXT("Foundation (2m) (Concrete Foundation)"));
    CheckName(TEXT("Foundation (2m)"), TEXT("FICSIT Foundation"), TEXT("Foundation (2m) (FICSIT Foundation)"));
    CheckName(TEXT("Foundation (2m)"), TEXT("Grip Metal Foundation"), TEXT("Foundation (2m) (Grip Metal Foundation)"));
    CheckName(TEXT("Inverted Ramp (4m)"), TEXT("Concrete Foundation"), TEXT("Inverted Ramp (4m) (Concrete Foundation)"));
    CheckName(TEXT("Wall (4m)"), TEXT("Concrete Wall"), TEXT("Wall (4m) (Concrete Wall)"));
    CheckName(TEXT("Roof (2m)"), TEXT("Tar Roof"), TEXT("Roof (2m) (Tar Roof)"));
    CheckName(TEXT("Wall (Conveyor x3)"), TEXT("Steel Wall"), TEXT("Wall (Conveyor x3) (Steel Wall)"));
    CheckName(TEXT("Foundation 2m"), TEXT("Asphalt Foundation"), TEXT("Foundation 2m (Asphalt Foundation)"));
    CheckName(TEXT("Refinery"), TEXT(""), TEXT("Refinery"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISSelectionLookupTest, "MassItemSelector.Selection.Lookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISSelectionLookupTest::RunTest(const FString&)
{
    FMISModule Module;
    auto* State = NewObject<UFGBuildGunStateDismantle>();
    auto* Property = FindFProperty<FArrayProperty>(State->GetClass(), TEXT("mPendingDismantleActors"));
    if (!TestNotNull(TEXT("Native selection array is available"), Property)) return false;
    auto* Element = CastFieldChecked<FObjectPropertyBase>(Property->Inner);
    FScriptArrayHelper Pending(Property, Property->ContainerPtrToValuePtr<void>(State));
    auto Append = [&](AActor* Actor) {
        Element->SetObjectPropertyValue(Pending.GetRawPtr(Pending.AddValue()), Actor);
    };
    auto* First = GetMutableDefault<AFGBuildable>();
    auto* Second = GetMutableDefault<AFGBuildableWire>();
    Append(First);
    Module.RefreshSelectedActorLookup(State);
    TestTrue(TEXT("Existing selections are found before adding"), Module.SelectedActorLookup.Contains(First));

    Append(Second);
    Module.MarkSelectionChanged();
    Module.UpdateSelectedActorLookup(State, Second, 1);
    TestEqual(TEXT("An accepted addition retains existing selections"), Module.SelectedActorLookup.Num(), 2);
    TestTrue(TEXT("An accepted addition is found by the lookup"), Module.SelectedActorLookup.Contains(Second));
    TestFalse(TEXT("A normal addition leaves the lookup current"), Module.bSelectedActorLookupDirty);

    Pending.RemoveValues(0);
    Module.MarkSelectionChanged();
    Module.UpdateSelectedActorLookup(State, First, 2);
    TestFalse(TEXT("Native removals discard stale entries"), Module.SelectedActorLookup.Contains(First));
    TestTrue(TEXT("Unchanged native selections remain in the lookup"), Module.SelectedActorLookup.Contains(Second));

    Pending.EmptyValues();
    Module.MarkSelectionChanged();
    Module.RefreshSelectedActorLookup(State);
    TestTrue(TEXT("Clearing native selection clears the lookup"), Module.SelectedActorLookup.IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISLassoBindingsTest, "MassItemSelector.Selection.LassoBindings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISLassoBindingsTest::RunTest(const FString&)
{
    auto* Action = MIS::GetLassoAction();
    auto* Context = GetLassoContext();
    if (!TestNotNull(TEXT("Native lasso action is packaged"), Action) ||
        !TestNotNull(TEXT("Native child mapping context is packaged"), Context)) return false;
    TestFalse(TEXT("Lasso leaves other input actions available"), Action->bConsumeInput);
    auto* Settings = Action->GetPlayerMappableKeySettings().Get();
    if (!TestNotNull(TEXT("The action has player-mappable settings"), Settings)) return false;
    TestTrue(TEXT("Controls can rebind the action"), Settings->IsPlayerMappable);
    TestEqual(TEXT("The mapping name is stable and unique"), Settings->Name, MIS::LassoMappingName);
    const auto& Mappings = Context->GetMappings();
    if (!TestEqual(TEXT("The context contains the lasso action"), Mappings.Num(), 2)) return false;
    TestTrue(TEXT("The context maps the lasso action"), Mappings[0].Action.Get() == Action);
    TestEqual(TEXT("The default key is Left Alt"), Mappings[0].Key, EKeys::LeftAlt);
    TestEqual(TEXT("The context follows native dismantling"), Context->mParentContext.ToSoftObjectPath().GetAssetName(), FString(TEXT("MC_BuildGunDismantle")));
    auto* Feature = LoadObject<UFGGameFeatureData>(nullptr, TEXT("/MassItemSelector/MassItemSelector.MassItemSelector"));
    if (!TestNotNull(TEXT("Game Feature data is packaged"), Feature)) return false;
    TestTrue(TEXT("The saved feature uses MIS's primary asset identity implementation"), Feature->IsA<UMISGameFeatureData>());
    TestTrue(TEXT("The root module registers MIS configuration"),
        GetDefault<UMISGameInstanceModule>()->ModConfigurations.Contains(UMISModConfiguration::StaticClass()));
    TestEqual(TEXT("MIS uses the primary asset type registered by Unreal Game Features"),
        Feature->GetPrimaryAssetId(), FPrimaryAssetId(TEXT("GameFeatureData"), Feature->GetFName()));
    TestFalse(TEXT("The native class default object is not an asset"),
        Feature->GetClass()->GetDefaultObject()->GetPrimaryAssetId().IsValid());
    TestEqual(TEXT("Game Feature preload bundles can resolve the registered feature asset"),
        UAssetManager::Get().GetPrimaryAssetPath(Feature->GetPrimaryAssetId()), FSoftObjectPath(Feature));
    const bool bScanned = Feature->GetPrimaryAssetTypesToScan().ContainsByPredicate([](const FPrimaryAssetTypeInfo& Info) {
        return Info.PrimaryAssetType == FPrimaryAssetType(TEXT("FGChildInputMappingContext")) &&
            Info.GetDirectories().ContainsByPredicate([](const FDirectoryPath& Directory) {
                return Directory.Path == TEXT("/MassItemSelector/Inputs");
            });
    });
    TestTrue(TEXT("Game Feature discovery includes the input context"), bScanned);
    auto* FilterAction = LoadObject<UInputAction>(nullptr, TEXT("/MassItemSelector/Inputs/IA_SelectionFilter.IA_SelectionFilter"));
    if (!TestNotNull(TEXT("The filter action is packaged"), FilterAction)) return false;
    auto* FilterSettings = FilterAction->GetPlayerMappableKeySettings().Get();
    if (!TestNotNull(TEXT("The filter shortcut is player mappable"), FilterSettings)) return false;
    TestTrue(TEXT("Controls can rebind the filter"), FilterSettings->IsPlayerMappable);
    TestEqual(TEXT("The filter mapping name is unique"), FilterSettings->Name, FName(TEXT("MassItemSelector.SelectionFilter")));
    TestTrue(TEXT("The context maps the filter action"), Mappings[1].Action.Get() == FilterAction);
    TestEqual(TEXT("The filter defaults to N"), Mappings[1].Key, EKeys::N);
    TestFalse(TEXT("The filter does not consume other mod actions"), FilterAction->bConsumeInput);
    TestFalse(TEXT("Mod Options no longer contains a custom key picker"),
        GetDefault<UMISModConfiguration>()->RootSection->SectionProperties.Contains(TEXT("LassoKey")));
    TestFalse(TEXT("A chord requires its modifier"), MIS::AreModifiersDown(FModifierKeysState(), { EKeys::LeftControl }));
    const FModifierKeysState Control(false, false, true, false, false, false, false, false, false);
    TestTrue(TEXT("A chord accepts the held modifier"), MIS::AreModifiersDown(Control, { EKeys::LeftControl }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISAppliedBindingsTest, "MassItemSelector.Selection.AppliedBindings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISAppliedBindingsTest::RunTest(const FString&)
{
    auto* Action = NewObject<UInputAction>();
    auto* ModifierAction = NewObject<UInputAction>();
    MIS::FInputBinding Binding;
    Binding.Action.Reset(Action);
    TArray<FEnhancedActionKeyMapping> Mappings = {
        FEnhancedActionKeyMapping(Action, EKeys::Gamepad_RightTrigger),
        FEnhancedActionKeyMapping(Action, EKeys::LeftMouseButton)
    };
    Binding.ResolveMappings(Mappings);
    TestEqual(TEXT("An applied native mapping works without player-mappable metadata"), Binding.Key, EKeys::LeftMouseButton);
    TestTrue(TEXT("Unnamed native actions do not need an invented mapping identifier"), Binding.MappingName.IsNone());
    Mappings[1].Key = EKeys::RightMouseButton;
    Binding.ResolveMappings(Mappings);
    TestEqual(TEXT("Rebuilt mappings update the physical key"), Binding.Key, EKeys::RightMouseButton);
    auto* Chord = NewObject<UInputTriggerChordAction>();
    Chord->ChordAction = ModifierAction;
    Mappings[1].Triggers.Add(Chord);
    Mappings.Add(FEnhancedActionKeyMapping(ModifierAction, EKeys::LeftControl));
    Binding.ResolveMappings(Mappings);
    TestFalse(TEXT("Applied chord mappings require the modifier"), Binding.Matches(EKeys::RightMouseButton, FModifierKeysState()));
    const FModifierKeysState Control(false, false, true, false, false, false, false, false, false);
    TestTrue(TEXT("Applied chord mappings accept the complete binding"), Binding.Matches(EKeys::RightMouseButton, Control));
    Mappings[1].Key = EKeys::F7;
    Binding.ResolveMappings(Mappings);
    TestEqual(TEXT("Keyboard mappings also resolve from the applied input"), Binding.Key, EKeys::F7);
    Mappings.RemoveAt(1);
    Binding.ResolveMappings(Mappings);
    TestFalse(TEXT("Removing the keyboard/mouse mapping leaves the action unbound"), Binding.Key.IsValid());
    TestTrue(TEXT("Removing a binding clears old modifiers"), Binding.Modifiers.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISNativeShortcutsTest, "MassItemSelector.Selection.NativeShortcuts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISNativeShortcutsTest::RunTest(const FString&)
{
    FMISModule Module;
    auto& Bindings = Module.InputBindings;
    Bindings.Dismantle.Key = EKeys::F8;
    Bindings.Close.Key = EKeys::F9;
    Bindings.CycleMode.Key = EKeys::F10;
    Bindings.HideHUD.Key = EKeys::F11;
    Bindings.SelectionFilter.Key = EKeys::F12;
    Bindings.Primary.Key = EKeys::RightMouseButton;
    Bindings.Secondary.Key = EKeys::LeftMouseButton;
    const FGeometry Geometry;
    auto Overlay = SNew(SMISOverlay).Owner(&Module);
    auto Press = [](FKey Key) { return FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0); };
    for (FKey Key : { EKeys::F, EKeys::R, EKeys::Escape })
        TestFalse(TEXT("Old native defaults no longer act as shortcuts"), Module.HandleDismantleShortcut(Press(Key)));
    Overlay->OnPreviewKeyDown(Geometry, Press(EKeys::F8));
    TestTrue(TEXT("Remapped dismantle key exits through the widget"), Module.bExitDismantleRequested);
    Module.bExitDismantleRequested = false;
    Module.Overlay = Overlay;
    Overlay->OnPreviewKeyDown(Geometry, Press(EKeys::F9));
    TestTrue(TEXT("Remapped close key cancels lasso"), Module.bClearLassoRequested);
    TestFalse(TEXT("Cancelling lasso with the remapped close key keeps dismantle active"), Module.bExitDismantleRequested);
    Module.bClearLassoRequested = false;
    Overlay->OnPreviewKeyDown(Geometry, Press(EKeys::Escape));
    TestTrue(TEXT("Escape cancels lasso even when the native close action is rebound"), Module.bClearLassoRequested);
    TestFalse(TEXT("Escape does not request an exit from dismantle"), Module.bExitDismantleRequested);
    Module.bClearLassoRequested = false;
    Module.Overlay.Reset();
    Overlay->OnPreviewKeyDown(Geometry, Press(EKeys::F10));
    TestTrue(TEXT("Remapped cycle key changes mode"), Module.bCycleModeRequested);
    TestFalse(TEXT("Remapped Hide HUD passes to the game"), Overlay->OnKeyDown(Geometry, Press(EKeys::F11)).IsEventHandled());
    TestTrue(TEXT("Old Hide HUD key is no longer forwarded"), Overlay->OnKeyDown(Geometry, Press(EKeys::T)).IsEventHandled());
    Module.bSelectionInteractive = true;
    Module.HandleDismantleShortcut(Press(EKeys::F12));
    TestFalse(TEXT("The remapped filter shortcut closes its panel"), Module.bSelectionInteractive);
    Module.bSelectionInteractive = true;
    Module.HandleDismantleShortcut(Press(EKeys::F9));
    TestFalse(TEXT("The remapped close key closes the panel"), Module.bSelectionInteractive);
    TestFalse(TEXT("Closing the panel keeps dismantle active"), Module.bExitDismantleRequested);
    Bindings.Dismantle.Key = EKeys::Invalid;
    TestFalse(TEXT("Unbound actions do not retain their previous key"), Module.HandleDismantleShortcut(Press(EKeys::F8)));
    Bindings.CycleMode.Modifiers = { EKeys::LeftControl };
    Module.bCycleModeRequested = false;
    TestFalse(TEXT("A remapped chord requires its modifiers"), Module.HandleDismantleShortcut(Press(EKeys::F10)));
    const FModifierKeysState Control(false, false, true, false, false, false, false, false, false);
    Module.HandleDismantleShortcut(FKeyEvent(EKeys::F10, Control, 0, false, 0, 0));
    TestTrue(TEXT("The complete chord cycles modes"), Module.bCycleModeRequested);
    const FPointerEvent Draw(0, FVector2D(10, 10), FVector2D(10, 10),
        TSet<FKey>{ EKeys::RightMouseButton }, EKeys::RightMouseButton, 0.f, FModifierKeysState());
    Overlay->OnMouseButtonDown(Geometry, Draw);
    TestTrue(TEXT("The remapped primary mouse button starts drawing"), Overlay->IsDrawing());
    const FPointerEvent Clear(0, FVector2D(10, 10), FVector2D(10, 10),
        TSet<FKey>{ EKeys::LeftMouseButton }, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
    Overlay->OnMouseButtonDown(Geometry, Clear);
    TestFalse(TEXT("The remapped secondary mouse button clears the outline"), Overlay->HasOutline());
    Module.Overlay = Overlay;
    Module.bLassoInputCaptured = true;
    Bindings.Primary.Key = EKeys::F7;
    auto Processor = Module.CreateLassoInputProcessor();
    auto& Slate = FSlateApplication::Get();
    TestTrue(TEXT("A keyboard primary binding routes to the lasso"), Processor->HandleKeyDownEvent(Slate, Press(EKeys::F7)));
    TestTrue(TEXT("A keyboard primary binding starts drawing"), Overlay->IsDrawing());
    Processor->HandleKeyUpEvent(Slate, Press(EKeys::F7));
    TestFalse(TEXT("Releasing a keyboard primary binding ends drawing"), Overlay->IsDrawing());
    Bindings.Dismantle.Key = EKeys::MiddleMouseButton;
    const FPointerEvent MouseClose(0, FVector2D(10, 10), FVector2D(10, 10),
        TSet<FKey>{ EKeys::MiddleMouseButton }, EKeys::MiddleMouseButton, 0.f, FModifierKeysState());
    Module.bExitDismantleRequested = false;
    Processor->HandleMouseButtonDownEvent(Slate, MouseClose);
    TestTrue(TEXT("Native shortcuts can be rebound to mouse buttons"), Module.bExitDismantleRequested);
    Module.Overlay.Reset();
    Module.bLassoInputCaptured = false;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISLassoInputTest, "MassItemSelector.Selection.LassoInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISLassoInputTest::RunTest(const FString&)
{
    auto* Distance = IConsoleManager::Get().FindConsoleVariable(TEXT("ItemSelector.MaxDistanceMeters"));
    const float PreviousDistance = Distance->GetFloat();
    const auto PreviousPriority = static_cast<EConsoleVariableFlags>(Distance->GetFlags() & ECVF_SetByMask);
    ON_SCOPE_EXIT {
        Distance->Set(PreviousDistance, ECVF_SetByConsole);
        Distance->ClearFlags(ECVF_SetByMask);
        Distance->SetFlags(PreviousPriority);
    };
    FMISModule Module;
    Module.InputBindings.Dismantle.Key = EKeys::F;
    Module.InputBindings.Close.Key = EKeys::Escape;
    Module.InputBindings.CycleMode.Key = EKeys::R;
    Module.InputBindings.HideHUD.Key = EKeys::T;
    Module.InputBindings.Primary.Key = EKeys::LeftMouseButton;
    Module.InputBindings.Secondary.Key = EKeys::RightMouseButton;
    auto Overlay = SNew(SMISOverlay).Owner(&Module);
    const FGeometry Geometry;
    TestFalse(TEXT("Lasso leaves T unhandled for the native Hide HUD action"),
        Overlay->OnKeyDown(Geometry, FKeyEvent(EKeys::T, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled());
    for (const FKey Key : { EKeys::F, EKeys::Escape })
    {
        Module.Overlay = Overlay;
        Module.bClearLassoRequested = false;
        Module.bExitDismantleRequested = false;
        Overlay->OnPreviewKeyDown(Geometry, FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
        TestEqual(TEXT("Only F requests a full exit with no outline"), Module.bExitDismantleRequested, Key == EKeys::F);
        TestEqual(TEXT("Escape cancels lasso with no outline"), Module.bClearLassoRequested, Key == EKeys::Escape);
        Module.bClearLassoRequested = false;
        Module.bExitDismantleRequested = false;
        Overlay->Points = { FVector2D(10, 10), FVector2D(100, 10), FVector2D(100, 100) };
        Module.bPreviewReady = true;
        Overlay->OnPreviewKeyDown(Geometry, FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
        TestEqual(TEXT("Only F requests a full exit with a preview"), Module.bExitDismantleRequested, Key == EKeys::F);
        TestEqual(TEXT("Escape cancels a completed lasso preview"), Module.bClearLassoRequested, Key == EKeys::Escape);
        Overlay->ClearSelection();
    }
    Module.Overlay.Reset();
    Module.bClearLassoRequested = false;
    Module.bExitDismantleRequested = false;
    const TSet<FKey> Buttons = { EKeys::RightMouseButton };
    const FPointerEvent RightClick(0, FVector2D::ZeroVector, FVector2D::ZeroVector,
        Buttons, EKeys::RightMouseButton, 0.f, FModifierKeysState());
    Overlay->Points = { FVector2D(10, 10) };
    Overlay->bDrawing = true;
    Overlay->OnMouseButtonDown(Geometry, RightClick);
    TestFalse(TEXT("Right-click clears an unfinished stroke"), Overlay->HasOutline());
    TestFalse(TEXT("Clearing a stroke stays in dismantle mode"), Module.bExitDismantleRequested);
    Overlay->Points = { FVector2D(10, 10), FVector2D(100, 10), FVector2D(100, 100) };
    Overlay->OnMouseButtonDown(Geometry, RightClick);
    TestFalse(TEXT("A completed outline with no matches is cleared"), Overlay->HasOutline());
    TestFalse(TEXT("An outline with no matches does not cause an exit"), Module.bExitDismantleRequested);
    Overlay->Points = { FVector2D(10, 10), FVector2D(100, 10), FVector2D(100, 100) };
    Module.bPreviewReady = true;
    Overlay->OnMouseCaptureLost(FCaptureLostEvent());
    TestTrue(TEXT("Capture loss preserves a completed outline"), Overlay->HasOutline());
    TestTrue(TEXT("Capture loss preserves its preview"), Module.bPreviewReady);
    Overlay->OnMouseButtonDown(Geometry, RightClick);
    TestFalse(TEXT("Right-click clears a preview"), Module.bPreviewReady);
    Overlay->OnMouseButtonDown(Geometry, RightClick);
    TestTrue(TEXT("Right-click exits when there is no outline"), Module.bExitDismantleRequested);
    const TSet<FKey> HeldButtons = { EKeys::LeftMouseButton };
    const FPointerEvent HeldDrag(0, FVector2D(10, 10), FVector2D::ZeroVector,
        HeldButtons, EKeys::Invalid, 0.f, FModifierKeysState());
    Overlay->OnMouseMove(Geometry, HeldDrag);
    TestTrue(TEXT("A drag can begin when lasso key and LMB arrive in the same frame"), Overlay->bDrawing);
    TestTrue(TEXT("A held drag records the outline"), Overlay->HasOutline());
    Overlay->ClearSelection();
    Module.bExitDismantleRequested = false;
    Module.Overlay = Overlay;
    Module.bLassoInputCaptured = true;
    Module.StrokeSize = FVector2D(100, 100);
    Module.bPreviewReady = true;
    Overlay->Points = { FVector2D(10, 10), FVector2D(100, 10), FVector2D(100, 100) };
    Module.ReleaseLasso();
    TestTrue(TEXT("Releasing lasso key preserves a completed outline"), Module.Overlay.IsValid());
    TestTrue(TEXT("Releasing lasso key preserves the preview for confirmation"), Module.bPreviewReady);
    TestTrue(TEXT("The preview keeps camera input locked"), Module.bLassoInputCaptured);
    TestFalse(TEXT("Releasing lasso key switches from drawing to preview interaction"), Overlay->bDrawingEnabled);
    TestFalse(TEXT("Releasing lasso key never applies automatically"), Module.IsApplying());
    Module.CloseOverlay();

    Module.Overlay = Overlay;
    Module.bLassoInputCaptured = true;
    Module.StrokeSize = FVector2D(100, 100);
    Module.bScanning = true;
    Module.ReleaseLasso();
    TestTrue(TEXT("A completed outline may finish scanning after lasso key release"), Module.IsScanning());
    TestTrue(TEXT("A pending preview stays open"), Module.Overlay.IsValid());
    Module.CloseOverlay();

    Module.Overlay = Overlay;
    Module.bLassoInputCaptured = true;
    Module.StrokeSize = FVector2D(100, 100);
    Overlay->SetDrawingEnabled(true);
    Overlay->bDrawing = true;
    Module.ReleaseLasso();
    TestTrue(TEXT("Releasing lasso key finishes a valid stroke while LMB is still held"), Module.Overlay.IsValid());
    TestFalse(TEXT("lasso key release stops appending points"), Overlay->IsDrawing());
    TestFalse(TEXT("lasso key release enables preview interaction"), Overlay->IsDrawingEnabled());
    TestTrue(TEXT("A finished stroke keeps the camera locked"), Module.bLassoInputCaptured);
    const int32 PreviewPoints = Overlay->Points.Num();
    Overlay->OnMouseMove(Geometry, HeldDrag);
    TestEqual(TEXT("Mouse movement preserves the preview outline"), Overlay->Points.Num(), PreviewPoints);
    TestFalse(TEXT("Mouse movement does not restart drawing in preview"), Overlay->IsDrawing());
    Module.CloseOverlay();

    Module.Overlay = Overlay;
    Module.bLassoInputCaptured = true;
    Overlay->SetDrawingEnabled(true);
    Overlay->Points = { FVector2D(10, 10), FVector2D(50, 50), FVector2D(100, 100) };
    Overlay->bDrawing = true;
    Module.ReleaseLasso();
    TestFalse(TEXT("Releasing lasso key cancels a flat outline"), Module.Overlay.IsValid());
    TestFalse(TEXT("An invalid outline leaves no preview"), Module.bPreviewReady);
    TestFalse(TEXT("Cancelling an invalid outline stays in dismantle mode"), Module.bExitDismantleRequested);
    Overlay->ClearSelection();
    Overlay->SetDrawingEnabled(true);
    Module.Overlay = Overlay;
    Module.bLassoInputCaptured = true;
    auto Input = Module.CreateLassoInputProcessor();
    auto& Slate = FSlateApplication::Get();
    Module.LassoKey = GetLassoContext()->GetMappings()[0].Key;
    TestEqual(TEXT("The default lasso binding is Left Alt"), Module.LassoKey, EKeys::LeftAlt);
    Input->HandleKeyDownEvent(Slate, FKeyEvent(EKeys::LeftShift, FModifierKeysState(), 0, false, 0, 0));
    TestFalse(TEXT("Shift does not activate the default lasso binding"), Module.bLassoKeyDown);
    Input->HandleKeyDownEvent(Slate, FKeyEvent(EKeys::LeftAlt, FModifierKeysState(), 0, false, 0, 0));
    TestTrue(TEXT("Left Alt key-down starts the hold"), Module.bLassoKeyDown);
    Input->HandleKeyUpEvent(Slate, FKeyEvent(EKeys::LeftShift, FModifierKeysState(), 0, false, 0, 0));
    TestTrue(TEXT("Releasing Shift does not release Left Alt"), Module.bLassoKeyDown);
    Input->HandleKeyUpEvent(Slate, FKeyEvent(EKeys::LeftAlt, FModifierKeysState(), 0, false, 0, 0));
    TestFalse(TEXT("Left Alt key-up ends the hold"), Module.bLassoKeyDown);
    TestTrue(TEXT("Left Alt release requests preview validation"), Module.bReleaseLassoRequested);
    Module.bReleaseLassoRequested = false;
    Module.LassoKey = EKeys::F8;
    Input->HandleKeyDownEvent(Slate, FKeyEvent(EKeys::LeftAlt, FModifierKeysState(), 0, false, 0, 0));
    TestFalse(TEXT("Rebinding disables the old activation key"), Module.bLassoKeyDown);
    Input->HandleKeyDownEvent(Slate, FKeyEvent(EKeys::F8, FModifierKeysState(), 0, false, 0, 0));
    TestTrue(TEXT("A configured key starts the hold"), Module.bLassoKeyDown);
    Input->HandleKeyUpEvent(Slate, FKeyEvent(EKeys::F8, FModifierKeysState(), 0, false, 0, 0));
    TestFalse(TEXT("The configured key releases the hold"), Module.bLassoKeyDown);
    const FPointerEvent Press(0, FVector2D(100, 100), FVector2D(100, 100),
        HeldButtons, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
    TestTrue(TEXT("Held-key mouse presses route through the input processor"), Input->HandleMouseButtonDownEvent(Slate, Press));
    TestTrue(TEXT("The routed press starts drawing"), Overlay->IsDrawing());
    TestTrue(TEXT("The routed press records an outline point"), Overlay->HasOutline());
    TestTrue(TEXT("Held-key movement routes through the input processor"), Input->HandleMouseMoveEvent(Slate, HeldDrag));
    const TSet<FKey> NoButtons;
    const FPointerEvent Release(0, FVector2D(100, 100), FVector2D(100, 100),
        NoButtons, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
    TestTrue(TEXT("The routed mouse release completes the stroke"), Input->HandleMouseButtonUpEvent(Slate, Release));
    TestFalse(TEXT("Drawing stops on the routed mouse release"), Overlay->IsDrawing());
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    Module.Player = World->SpawnActor<APlayerController>();
    auto* State = NewObject<UFGBuildGunStateDismantle>();
    auto* DefaultMode = FindFProperty<FClassProperty>(State->GetClass(), TEXT("mDefaultDismantleMode"));
    if (!TestNotNull(TEXT("The native Default mode property exists"), DefaultMode)) return false;
    DefaultMode->SetObjectPropertyValue_InContainer(State, UFGDismantleModeDescriptor::StaticClass());
    Module.ActiveState = State;
    Module.LastDismantleMode = UFGDismantleModeDescriptor::StaticClass();
    for (const FKey MouseKey : { EKeys::ThumbMouseButton, EKeys::ThumbMouseButton2 })
    {
        Module.LassoKey = MouseKey;
        Module.bReleaseLassoRequested = false;
        const FPointerEvent MousePress(0, FVector2D(100, 100), FVector2D(100, 100),
            TSet<FKey>{ MouseKey }, MouseKey, 0.f, FModifierKeysState());
        Input->HandleMouseButtonDownEvent(Slate, MousePress);
        TestTrue(TEXT("A mouse binding starts the lasso hold"), Module.bLassoKeyDown);
        const FPointerEvent MouseRelease(0, FVector2D(100, 100), FVector2D(100, 100),
            TSet<FKey>(), MouseKey, 0.f, FModifierKeysState());
        Input->HandleMouseButtonUpEvent(Slate, MouseRelease);
        TestFalse(TEXT("A mouse binding releases the lasso while drawing input is captured"), Module.bLassoKeyDown);
        TestTrue(TEXT("A mouse release requests stroke completion"), Module.bReleaseLassoRequested);
    }
    Module.LassoKey = Module.InputBindings.Primary.Key;
    Overlay->ClearSelection();
    Overlay->SetDrawingEnabled(true);
    const FPointerEvent OverlappingPress(0, FVector2D(100, 100), FVector2D(100, 100),
        HeldButtons, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
    TestTrue(TEXT("Lasso activation reserves an overlapping primary button"),
        Input->HandleMouseButtonDownEvent(Slate, OverlappingPress));
    TestTrue(TEXT("An overlapping primary binding starts the hold"), Module.bLassoKeyDown);
    TestFalse(TEXT("An overlapping primary binding does not also start drawing"), Overlay->IsDrawing());
    Input->HandleMouseMoveEvent(Slate, HeldDrag);
    TestFalse(TEXT("Dragging the activation button does not bypass its priority"), Overlay->IsDrawing());
    Input->HandleMouseButtonUpEvent(Slate, Release);
    TestFalse(TEXT("An overlapping primary release ends the hold"), Module.bLassoKeyDown);
    Module.LassoKey = EKeys::F8;
    Module.ActiveState.Reset();
    Module.LastDismantleMode.Reset();
    Overlay->SetDrawingEnabled(false);
    Module.bLassoKeyDown = Module.bLassoKeyWasDown = true;
    Module.bPreviewReady = true;
    TestTrue(TEXT("A completed preview accepts confirmation while the lasso key stays held"),
        Input->HandleMouseButtonDownEvent(Slate, Press));
    TestTrue(TEXT("The held-key confirm click requests selection"), Module.bApplyLassoRequested);
    TestFalse(TEXT("The confirm click does not restart drawing"), Overlay->IsDrawing());
    Module.bApplyLassoRequested = false;
    Module.bPreviewReady = false;
    Module.bScanning = true;
    Input->HandleMouseButtonDownEvent(Slate, Press);
    TestTrue(TEXT("Confirmation during preview scanning is queued"), Module.bApplyLassoRequested);
    Module.bApplyLassoRequested = false;
    Module.bLassoKeyWasDown = false;
    Input->HandleMouseButtonDownEvent(Slate, Press);
    TestFalse(TEXT("Pressing the lasso key again preserves redraw behavior"), Module.bApplyLassoRequested);
    Module.CloseOverlay();
    for (bool bHeld : { false, true })
    {
        Module.BeginStroke();
        Module.Overlay = Overlay;
        Module.bLassoInputCaptured = true;
        Module.bLassoKeyDown = Module.bLassoKeyWasDown = bHeld;
        Module.Player->SetIgnoreLookInput(true);
        Module.Player->SetIgnoreMoveInput(true);
        Module.Player->bShowMouseCursor = true;
        Overlay->SetDrawingEnabled(false);
        Overlay->Points = { FVector2D(10, 10), FVector2D(100, 10), FVector2D(100, 100) };
        Module.Polygon = { {10, 10}, {100, 10}, {100, 100} };
        Module.StrokeSize = FVector2D(100, 100);
        Module.bScanning = true;
        Module.FinishPreview();
        TestTrue(TEXT("An empty scan keeps a completed preview"), Module.bPreviewReady);
        TestFalse(TEXT("An empty scan finishes scanning"), Module.IsScanning());
        TestTrue(TEXT("An empty scan preserves its outline"), Overlay->HasOutline());
        TestFalse(TEXT("An empty scan does not automatically enable redrawing"), Overlay->IsDrawingEnabled());
        TestTrue(TEXT("Escape is consumed for an empty preview"),
            Input->HandleKeyDownEvent(Slate, FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0)));
        TestTrue(TEXT("Escape queues cancellation of an empty preview"), Module.bClearLassoRequested);
        TestFalse(TEXT("Escape keeps native dismantle active"), Module.bExitDismantleRequested);
        Input->HandleKeyUpEvent(Slate, FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
        Module.bClearLassoRequested = false;
        Module.bLassoKeyDown = false;
        Module.ReleaseLasso();
        TestTrue(TEXT("Releasing activation preserves an empty preview"), Module.Overlay.IsValid());
        TestTrue(TEXT("An empty preview keeps the camera frozen"), Module.Player->IsLookInputIgnored());
        TestEqual(TEXT("An empty preview reports zero matches"), Module.LastResult.ToString(), FString(TEXT("Found 0 objects.")));
        Module.AdjustDistance(Module.GetWheelStep());
        TestTrue(TEXT("Changing distance queues a rescan of the empty preview"), Module.bRefreshPreview);
        TestTrue(TEXT("Changing distance preserves the outline"), Overlay->HasOutline());
        TestEqual(TEXT("Changing distance preserves the scan polygon"), Module.Polygon.Num(), 3);
        TestFalse(TEXT("A distance rescan does not start redrawing"), Overlay->IsDrawingEnabled());
        Module.bRefreshPreview = false;
        Module.bScanning = true;
        Input->HandleKeyDownEvent(Slate, FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
        TestTrue(TEXT("Escape can cancel a pending preview scan"), Module.bClearLassoRequested);
        Input->HandleKeyUpEvent(Slate, FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
        Module.bClearLassoRequested = false;
        Module.PreviewCandidates.AddDefaulted();
        Module.FinishPreview();
        TestEqual(TEXT("A rescan can populate a previously empty preview"), Module.PreviewCandidates.Num(), 1);
        TestTrue(TEXT("A recovered preview retains its original outline"), Overlay->HasOutline());
        TestFalse(TEXT("A recovered preview remains in preview interaction"), Overlay->IsDrawingEnabled());
        Module.InputBindings.Close.Key = EKeys::F9;
        Module.bLassoKeyDown = Module.bLassoKeyWasDown = bHeld;
        Input->HandleKeyDownEvent(Slate, FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, true, 0, 0));
        TestFalse(TEXT("Repeated Escape does not queue another cancellation"), Module.bClearLassoRequested);
        Input->HandleKeyDownEvent(Slate, FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
        TestTrue(TEXT("Escape cancels a nonempty preview with activation held or released"), Module.bClearLassoRequested);
        TestFalse(TEXT("Cancelling a nonempty preview does not apply it"), Module.bApplyLassoRequested);
        TestFalse(TEXT("Cancelling a nonempty preview keeps dismantle active"), Module.bExitDismantleRequested);
        Module.CloseOverlay();
        TestFalse(TEXT("Cancelling removes the lasso overlay"), Module.Overlay.IsValid());
        TestFalse(TEXT("Cancelling stops the scan"), Module.IsScanning());
        TestTrue(TEXT("Cancelling drops unconfirmed matches"), Module.PreviewCandidates.IsEmpty());
        TestTrue(TEXT("Held Escape stays consumed after the lasso closes"),
            Input->HandleKeyDownEvent(Slate, FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, true, 0, 0)));
        TestTrue(TEXT("The cancellation key release is consumed"),
            Input->HandleKeyUpEvent(Slate, FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0)));
        TestFalse(TEXT("A new Escape press outside lasso returns to native input"),
            Input->HandleKeyDownEvent(Slate, FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0)));
        TestFalse(TEXT("Cancelling the empty preview restores camera input"), Module.Player->IsLookInputIgnored());
        TestFalse(TEXT("Cancelling the empty preview restores movement"), Module.Player->IsMoveInputIgnored());
        TestFalse(TEXT("Cancelling the empty preview restores the game cursor"), Module.Player->bShowMouseCursor);
    }
    Module.SelectionPanel = SNew(SMISSelectionPanel).Owner(&Module);
    Module.ActiveState = State;
    Module.LastDismantleMode = UFGDismantleModeDescriptor::StaticClass();
    Module.InputBindings.SelectionFilter.Key = EKeys::N;
    auto KeyPress = [](FKey Key, bool bRepeat = false) {
        return FKeyEvent(Key, FModifierKeysState(), 0, bRepeat, 0, 0);
    };
    TestTrue(TEXT("The filter shortcut reaches the queue without an Enhanced Input callback"),
        Input->HandleKeyDownEvent(Slate, KeyPress(EKeys::N)));
    TestTrue(TEXT("The filter shortcut queues opening the panel"), Module.bSelectionRequested);
    Module.bSelectionRequested = false;
    Input->HandleKeyDownEvent(Slate, KeyPress(EKeys::N, true));
    TestFalse(TEXT("Holding the filter shortcut does not repeatedly toggle the panel"), Module.bSelectionRequested);
    Module.InputBindings.SelectionFilter.Key = EKeys::F6;
    Input->HandleKeyDownEvent(Slate, KeyPress(EKeys::N));
    TestFalse(TEXT("The old filter key stops opening the panel after rebinding"), Module.bSelectionRequested);
    Input->HandleKeyDownEvent(Slate, KeyPress(EKeys::F6));
    TestTrue(TEXT("The configured filter key opens the panel"), Module.bSelectionRequested);
    Module.bSelectionRequested = false;
    Module.Player->bShowMouseCursor = true;
    TestFalse(TEXT("Typing in another UI does not trigger the filter shortcut"),
        Input->HandleKeyDownEvent(Slate, KeyPress(EKeys::F6)));
    TestFalse(TEXT("Another UI leaves no pending panel request"), Module.bSelectionRequested);
    Module.bSelectionInteractive = true;
    Input->HandleKeyDownEvent(Slate, KeyPress(EKeys::F6));
    TestTrue(TEXT("The shortcut also queues closing the interactive panel"), Module.bSelectionRequested);
    Module.bSelectionInteractive = false;
    Module.Player->bShowMouseCursor = false;
    Module.bSelectionRequested = false;
    Module.InputBindings.SelectionFilter.Key = EKeys::MiddleMouseButton;
    const FPointerEvent FilterClick(0, FVector2D(100, 100), FVector2D(100, 100),
        TSet<FKey>{ EKeys::MiddleMouseButton }, EKeys::MiddleMouseButton, 0.f, FModifierKeysState());
    Input->HandleMouseButtonDownEvent(Slate, FilterClick);
    TestTrue(TEXT("A filter shortcut rebound to a mouse button also queues opening"), Module.bSelectionRequested);
    Module.bSelectionRequested = false;
    Module.bSelectionInteractive = true;
    Module.Player->bShowMouseCursor = true;
    const FPointerEvent OutsideClick(0, FVector2D(10000, 10000), FVector2D(10000, 10000),
        TSet<FKey>{ EKeys::LeftMouseButton }, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
    Module.SelectionOperations.SetNum(2000);
    Module.NextSelectionOperation = 500;
    Module.SelectionOperationChanged = 450;
    Module.Player->SetIgnoreLookInput(true);
    Module.Player->SetIgnoreMoveInput(true);
    TestTrue(TEXT("An outside click is consumed when closing the filter interaction"),
        Input->HandleMouseButtonDownEvent(Slate, OutsideClick));
    TestFalse(TEXT("Clicking outside returns to normal dismantle interaction"), Module.bSelectionInteractive);
    TestFalse(TEXT("Closing ADF releases camera input during a large update"), Module.Player->IsLookInputIgnored());
    TestFalse(TEXT("Closing ADF releases movement input during a large update"), Module.Player->IsMoveInputIgnored());
    TestEqual(TEXT("Closing ADF preserves queued selection work"), Module.SelectionOperations.Num(), 2000);
    TestEqual(TEXT("Closing ADF preserves the next operation"), Module.NextSelectionOperation, 500);
    TestEqual(TEXT("Closing ADF preserves completed progress"), Module.SelectionOperationChanged, 450);
    TestTrue(TEXT("The update remains busy after closing ADF"), Module.IsSelectionBusy());
    TestFalse(TEXT("An outside click does not queue reopening the filter"), Module.bSelectionRequested);
    TestFalse(TEXT("An outside click does not confirm a lasso selection"), Module.bApplyLassoRequested);
    Module.bSelectionInteractive = true;
    Module.Player->bShowMouseCursor = true;
    Module.InputBindings.Close.Key = EKeys::F9;
    TestTrue(TEXT("Escape is consumed when closing ADF interaction"),
        Input->HandleKeyDownEvent(Slate, KeyPress(EKeys::Escape)));
    TestFalse(TEXT("Escape returns to normal dismantle interaction"), Module.bSelectionInteractive);
    TestFalse(TEXT("Escape restores the game cursor state"), Module.Player->bShowMouseCursor);
    TestEqual(TEXT("Escape preserves background reselection work"), Module.SelectionOperations.Num(), 2000);
    TestTrue(TEXT("Closing ADF does not report a stopped update"), Module.SelectionMessage.IsEmpty());
    Module.SelectionOperations.Reset();
    TestFalse(TEXT("Closing ADF with Escape keeps dismantle mode active"), Module.bExitDismantleRequested);
    Module.SelectionPanel.Reset();
    Module.Player.Reset();
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISSelectionModeTest, "MassItemSelector.Selection.DefaultModeOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISSelectionModeTest::RunTest(const FString&)
{
    FMISModule Module;
    TestFalse(TEXT("ADF requires an active dismantle state"), Module.IsDefaultDismantleMode());
    auto* State = NewObject<UFGBuildGunStateDismantle>();
    auto* DefaultMode = FindFProperty<FClassProperty>(State->GetClass(), TEXT("mDefaultDismantleMode"));
    if (!TestNotNull(TEXT("The native Default mode property exists"), DefaultMode)) return false;
    DefaultMode->SetObjectPropertyValue_InContainer(State, UFGDismantleModeDescriptor::StaticClass());
    Module.ActiveState = State;
    Module.LastDismantleMode = UFGDismantleModeDescriptor::StaticClass();
    TestTrue(TEXT("ADF accepts the native Default mode"), Module.IsDefaultDismantleMode());
    Module.LastDismantleMode = UFGBuildGunModeDescriptor::StaticClass();
    TestFalse(TEXT("ADF rejects another dismantle mode"), Module.IsDefaultDismantleMode());
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    Module.Player = World->SpawnActor<APlayerController>();
    Module.SelectionPanel = SNew(SMISSelectionPanel).Owner(&Module);
    Module.InputBindings.SelectionFilter.Key = EKeys::N;
    TestFalse(TEXT("A filter shortcut outside Default mode is not queued"),
        Module.QueueSelectionInteraction(EKeys::N, FModifierKeysState(), false));
    auto Row = MakeShared<FMISSelectionType>();
    Row->Selected = 2;
    Module.SelectionTypes.Add(Row);
    Module.ToggleSelectionInteraction();
    TestFalse(TEXT("An Enhanced Input request cannot open ADF outside Default mode"), Module.IsSelectionInteractive());
    Module.bSelectionInteractive = true;
    Module.Player->SetIgnoreLookInput(true);
    Module.Player->SetIgnoreMoveInput(true);
    Module.bSelectionRequested = true;
    Module.SelectionOperations.AddDefaulted();
    Module.TickSelectionPanel();
    TestFalse(TEXT("A mode change closes interactive ADF"), Module.IsSelectionInteractive());
    TestTrue(TEXT("A mode change cancels pending selection operations"), Module.SelectionOperations.IsEmpty());
    TestFalse(TEXT("A mode change discards queued input"), Module.bSelectionRequested);
    TestFalse(TEXT("A mode change releases look input"), Module.Player->IsLookInputIgnored());
    TestEqual(TEXT("The ADF panel stays hidden outside Default mode"), Module.SelectionPanel->GetVisibility(), EVisibility::Collapsed);
    Module.SelectionPanel.Reset();
    Module.Player.Reset();
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISScanBudgetTest, "MassItemSelector.Selection.ScanBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISScanBudgetTest::RunTest(const FString&)
{
    FMISModule Module;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    Module.Player = World->SpawnActor<APlayerController>();
    Module.LightweightScans.SetNum(3);
    Module.bGatheringCandidates = Module.bScanning = true;
    TestFalse(TEXT("Candidate gathering yields after an expired deadline"), Module.GatherCandidates(-1));
    TestEqual(TEXT("An expired budget still makes bounded progress"), Module.NextLightweightClass, 1);
    TestTrue(TEXT("Yielding preserves the gathering phase"), Module.bGatheringCandidates);
    TestFalse(TEXT("The next frame resumes candidate gathering"), Module.GatherCandidates(-1));
    TestEqual(TEXT("Resuming does not restart the scan"), Module.NextLightweightClass, 2);
    TestTrue(TEXT("The final batch finishes candidate gathering"), Module.GatherCandidates(TNumericLimits<double>::Max()));
    TestFalse(TEXT("A finished gather leaves the gathering phase"), Module.bGatheringCandidates);
    TestTrue(TEXT("A finished gather releases its class snapshot"), Module.LightweightScans.IsEmpty());
    Module.LightweightScans.SetNum(3);
    Module.bGatheringCandidates = true;
    Module.BuildableCount = 10;
    Module.CancelScan();
    TestFalse(TEXT("Cancelling stops candidate gathering"), Module.bGatheringCandidates);
    TestTrue(TEXT("Cancelling drops the class snapshot"), Module.LightweightScans.IsEmpty());
    TestEqual(TEXT("Cancelling resets the actor scan"), Module.BuildableCount, 0);
    TestEqual(TEXT("Cancelling resets the lightweight scan"), Module.NextLightweightClass, 0);
    Module.Player.Reset();
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISLowRiskPerformanceTest, "MassItemSelector.Selection.LowRiskPerformance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISLowRiskPerformanceTest::RunTest(const FString&)
{
    auto* Distance = IConsoleManager::Get().FindConsoleVariable(TEXT("ItemSelector.MaxDistanceMeters"));
    if (!TestNotNull(TEXT("The distance setting is registered"), Distance)) return false;
    const float OriginalDistance = Distance->GetFloat();
    ON_SCOPE_EXIT { Distance->Set(OriginalDistance, ECVF_SetByConsole); };
    FMISModule Module;
    Distance->Set(100.f, ECVF_SetByConsole);
    Module.bScanning = true;
    Module.Candidates.SetNum(30);
    Module.NextCandidate = 17;
    Module.SetFarDistance(100.f);
    TestTrue(TEXT("An unchanged distance preserves an active preview scan"), Module.bScanning);
    TestEqual(TEXT("An unchanged distance preserves scan progress"), Module.NextCandidate, 17);
    TestEqual(TEXT("An unchanged distance preserves gathered candidates"), Module.Candidates.Num(), 30);
    TestTrue(TEXT("An unchanged manual edit still overrides automatic native range tracking"), Module.bDistanceWasEdited);
    Module.CancelScan();
    Module.bRefreshPreview = true;
    Module.RefreshPreviewAt = 123.0;
    Distance->Set(500.f, ECVF_SetByConsole);
    Module.SetFarDistance(550.f);
    TestEqual(TEXT("Scrolling past the maximum does not postpone a pending refresh"), Module.RefreshPreviewAt, 123.0);
    TestTrue(TEXT("Scrolling past the maximum keeps a pending refresh queued"), Module.bRefreshPreview);
    Distance->Set(5.f, ECVF_SetByConsole);
    Module.SetFarDistance(-10.f);
    TestEqual(TEXT("Scrolling past the minimum does not postpone a pending refresh"), Module.RefreshPreviewAt, 123.0);
    Distance->Set(600.f, ECVF_SetByConsole);
    Module.SetFarDistance(500.f);
    TestEqual(TEXT("Manual edits still normalize out-of-range console values"), Distance->GetFloat(), 500.f);
    TestEqual(TEXT("Normalizing an unchanged effective distance does not rescan"), Module.RefreshPreviewAt, 123.0);
    Module.bRefreshPreview = false;
    Module.bScanning = true;
    Module.NextCandidate = 17;
    Module.SetFarDistance(400.f);
    TestFalse(TEXT("A changed distance still cancels the old scan"), Module.bScanning);
    TestEqual(TEXT("A changed distance resets scan progress"), Module.NextCandidate, 0);
    TestEqual(TEXT("A changed distance updates the setting"), Distance->GetFloat(), 400.f);

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    ON_SCOPE_EXIT { Module.Player.Reset(); World->DestroyWorld(false); };
    Module.Player = World->SpawnActor<APlayerController>();
    Module.NextHintScan = TNumericLimits<double>::Max();
    Module.UpdateDismantleHints();
    TestTrue(TEXT("The test has no native hint widget target"), Module.DismantleHintTargets.IsEmpty());
    TestEqual(TEXT("A missing hint widget still honors the scan deadline"), Module.NextHintScan, TNumericLimits<double>::Max());
    Module.RemoveDismantleHints();
    TestEqual(TEXT("HUD reset makes discovery immediately eligible"), Module.NextHintScan, 0.0);
    Module.UpdateDismantleHints();
    const double NextScan = Module.NextHintScan;
    TestTrue(TEXT("Discovery schedules its next attempt when no widget exists"), NextScan > 0.0 && NextScan < TNumericLimits<double>::Max());
    Module.NextHintUpdate = 0;
    Module.UpdateDismantleHints();
    TestEqual(TEXT("Repeated empty-widget updates do not repeat discovery early"), Module.NextHintScan, NextScan);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISSelectionApplyTest, "MassItemSelector.Selection.Apply",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISSelectionApplyTest::RunTest(const FString&)
{
    FMISModule Module;
    auto* State = NewObject<UFGBuildGunStateDismantle>();
    auto* DefaultMode = FindFProperty<FClassProperty>(State->GetClass(), TEXT("mDefaultDismantleMode"));
    if (!TestNotNull(TEXT("The native Default mode property exists"), DefaultMode)) return false;
    DefaultMode->SetObjectPropertyValue_InContainer(State, UFGDismantleModeDescriptor::StaticClass());
    Module.ActiveState = State;
    Module.LastDismantleMode = UFGDismantleModeDescriptor::StaticClass();
    Module.bSelectionInteractive = true;

    auto* Selected = GetMutableDefault<AFGBuildable>();
    auto* Excluded = GetMutableDefault<AFGBuildableWire>();
    auto* Property = FindFProperty<FArrayProperty>(State->GetClass(), TEXT("mPendingDismantleActors"));
    if (!TestNotNull(TEXT("Native selection array is available"), Property)) return false;
    FScriptArrayHelper Pending(Property, Property->ContainerPtrToValuePtr<void>(State));
    CastFieldChecked<FObjectPropertyBase>(Property->Inner)->SetObjectPropertyValue(Pending.GetRawPtr(Pending.AddValue()), Selected);
    auto AddRow = [&](UClass* Class, int32 SelectedCount, int32 ExcludedCount) {
        auto Row = MakeShared<FMISSelectionType>();
        Row->Class = Class;
        Row->Selected = SelectedCount;
        Row->Excluded = ExcludedCount;
        Module.SelectionTypes.Add(Row);
        Module.SelectionTypeLookup.Add(Class, Row);
        return Row;
    };
    auto SelectedRow = AddRow(Selected->GetClass(), 2000, 0);
    auto ExcludedRow = AddRow(Excluded->GetClass(), 0, 1);
    const auto ExcludedObject = Module.CaptureSelectionObject(Excluded);
    Module.ExcludedSelection.Add(ExcludedObject.GetKey(), ExcludedObject);

    Module.SetSelectionTypeEnabled(Selected->GetClass(), false);
    Module.SetSelectionTypeEnabled(Excluded->GetClass(), true);
    TestTrue(TEXT("The removal choice is staged on its row"), SelectedRow->PendingEnabled.IsSet() && !SelectedRow->PendingEnabled.GetValue());
    TestTrue(TEXT("The restoration choice is staged independently"), ExcludedRow->PendingEnabled.IsSet() && ExcludedRow->PendingEnabled.GetValue());
    TestTrue(TEXT("Several building types can be edited before Apply"), Module.HasSelectionEdits());
    TestTrue(TEXT("Checkbox edits do not start native selection work"), Module.SelectionOperations.IsEmpty());
    TestFalse(TEXT("Staged edits do not make the selection busy"), Module.IsSelectionBusy());
    TestEqual(TEXT("Staged edits leave native selection unchanged"), Pending.Num(), 1);
    Module.SetSelectionTypeEnabled(Selected->GetClass(), true);
    Module.SetSelectionTypeEnabled(Excluded->GetClass(), false);
    TestFalse(TEXT("Reversing checkbox edits removes unnecessary work"), Module.HasSelectionEdits());
    Module.SetSelectionTypeEnabled(Selected->GetClass(), false);
    Module.SetSelectionTypeEnabled(Excluded->GetClass(), true);
    Module.CloseSelectionInteraction();
    TestFalse(TEXT("Closing ADF discards unapplied edits"), Module.HasSelectionEdits());
    TestTrue(TEXT("Discarded edits leave no queued work"), Module.SelectionOperations.IsEmpty());
    TestEqual(TEXT("Discarding edits preserves excluded buildings"), Module.ExcludedSelection.Num(), 1);

    Module.bSelectionInteractive = true;
    Module.SetSelectionTypeEnabled(Selected->GetClass(), false);
    Module.SetSelectionTypeEnabled(Excluded->GetClass(), true);
    Module.ApplySelectionEdits();
    TestTrue(TEXT("Apply reserves capacity from all staged row counts"), Module.SelectionOperations.Max() >= 2001);
    if (!TestEqual(TEXT("Apply queues restoration while skipping ineligible SDK placeholder actors"), Module.SelectionOperations.Num(), 1)) return false;
    TestTrue(TEXT("The queued operation restores selection"), Module.SelectionOperations[0].bRestore);
    TestTrue(TEXT("The restoration targets the excluded building"), Module.SelectionOperations[0].Object.Actor.Get() == Excluded);
    TestFalse(TEXT("Apply consumes the staged edits"), Module.HasSelectionEdits());
    Module.ApplySelectionEdits();
    TestEqual(TEXT("Repeated Apply cannot duplicate in-flight work"), Module.SelectionOperations.Num(), 1);
    Module.CloseSelectionInteraction();
    TestEqual(TEXT("Closing ADF after Apply preserves background work"), Module.SelectionOperations.Num(), 1);
    Module.ResetSelectionPanel();
    TestTrue(TEXT("Ending the selection cancels outstanding applied work"), Module.SelectionOperations.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISLocalizationTest, "MassItemSelector.Selection.LocalizationResources",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISLocalizationTest::RunTest(const FString&)
{
    FString Resource;
    TestTrue(TEXT("MIS localization is loaded through the normal plugin loading policy"),
        FTextLocalizationManager::Get().GetLocResID(TEXT("ItemSelector"), TEXT("ApplyLasso"), Resource));
    TestTrue(TEXT("The lasso text comes from MIS's compiled localization resource"),
        Resource.Contains(TEXT("MassItemSelector")) && Resource.EndsWith(TEXT(".locres")));
    TestTrue(TEXT("The Apply button has a compiled localization entry"),
        FTextLocalizationManager::Get().GetLocResID(TEXT("ItemSelector"), TEXT("SelectionApply"), Resource));
    TestTrue(TEXT("The pending-edits message has a compiled localization entry"),
        FTextLocalizationManager::Get().GetLocResID(TEXT("ItemSelector"), TEXT("SelectionPending"), Resource));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISCrashRegressionTest, "MassItemSelector.Selection.CrashRegression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISCrashRegressionTest::RunTest(const FString&)
{
    FMISModule Module;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    Module.ActiveWorld = World;
    auto* BuildableClass = LoadClass<AFGBuildable>(nullptr,
        TEXT("/Game/FactoryGame/Buildable/Building/Foundation/Build_Foundation_8x2_01.Build_Foundation_8x2_01_C"));
    if (!TestNotNull(TEXT("A concrete buildable class is available"), BuildableClass)) return false;
    auto* Buildable = World->SpawnActor<AFGBuildable>(BuildableClass);
    if (!TestNotNull(TEXT("A temporary-spawn test actor can be created"), Buildable)) return false;
    TestFalse(TEXT("Ordinary recipe recovery is unchanged"), bool(Module.GetLightweightSpawnRecipe(Buildable)));
    {
        TGuardValue<TSubclassOf<AFGBuildable>> ClassGuard(Module.LightweightSpawnClass, BuildableClass);
        TGuardValue<TSubclassOf<UFGRecipe>> RecipeGuard(Module.LightweightSpawnRecipe, UFGRecipe::StaticClass());
        TGuardValue<TWeakObjectPtr<UWorld>> WorldGuard(Module.LightweightSpawnWorld, World);
        TestTrue(TEXT("A matching pool spawn uses the recorded instance recipe"),
            Module.GetLightweightSpawnRecipe(Buildable) == UFGRecipe::StaticClass());
        TestFalse(TEXT("Unrelated actors retain native recipe recovery"),
            bool(Module.GetLightweightSpawnRecipe(World->SpawnActor<AActor>())));
        Module.ActiveWorld.Reset();
        TestTrue(TEXT("Native spawns remain protected without an active MIS world or selection"),
            Module.GetLightweightSpawnRecipe(Buildable) == UFGRecipe::StaticClass());
        Module.LightweightSpawnWorld.Reset();
        TestFalse(TEXT("Recipe overrides cannot cross worlds"), bool(Module.GetLightweightSpawnRecipe(Buildable)));
        Module.LightweightSpawnWorld = World;
        {
            TGuardValue<TSubclassOf<UFGRecipe>> NestedRecipeGuard(Module.LightweightSpawnRecipe, nullptr);
            TestFalse(TEXT("Nested spawns without a recipe cannot inherit the outer recipe"),
                bool(Module.GetLightweightSpawnRecipe(Buildable)));
        }
        TestTrue(TEXT("Finishing a nested spawn restores the outer recipe"),
            Module.GetLightweightSpawnRecipe(Buildable) == UFGRecipe::StaticClass());
        Module.ActiveWorld = World;
    }
    TestFalse(TEXT("The recipe override ends with the synchronous spawn"), bool(Module.GetLightweightSpawnRecipe(Buildable)));

    auto* State = NewObject<UFGBuildGunStateDismantle>();
    auto* OtherState = NewObject<UFGBuildGunStateDismantle>();
    Module.ActiveState = State;
    TestTrue(TEXT("Native refund previews are deferred even when MIS selection work is idle"), Module.DeferRefundUpdate(State, false));
    TestFalse(TEXT("Other players' refund updates are unchanged"), Module.DeferRefundUpdate(OtherState, false));
    for (int32 Index = 0; Index < 10000; ++Index)
        Module.DeferRefundUpdate(State, Index == 9999);
    TestTrue(TEXT("Native dismantle removal callbacks coalesce into one pending preview update"), Module.bRefundUpdatePending);
    TestTrue(TEXT("The final request preserves the no-build-cost setting"), Module.bRefundNoBuildCost);
    Module.ResetSelectionPanel();
    TestTrue(TEXT("Clearing the selection after dismantling preserves the final preview refresh"), Module.bRefundUpdatePending);
    {
        TGuardValue<bool> FlushGuard(Module.bFlushingRefundUpdate, true);
        TestFalse(TEXT("The final native preview refresh passes through without being deferred again"), Module.DeferRefundUpdate(State, true));
    }
    Module.FlushRefundUpdate();
    TestFalse(TEXT("Flushing consumes the pending refund update"), Module.bRefundUpdatePending);
    Module.FlushRefundUpdate();
    TestFalse(TEXT("An unchanged batch has no refund work to repeat"), Module.bRefundUpdatePending);
    Module.bScanning = Module.bApplying = true;
    Module.NextRefundUpdate = TNumericLimits<double>::Max();
    TestTrue(TEXT("Native aim updates between batches are also deferred"), Module.DeferRefundUpdate(State, false));
    Module.FlushRefundUpdate();
    TestTrue(TEXT("Busy selections throttle refund widget rebuilds"), Module.bRefundUpdatePending);
    Module.bScanning = Module.bApplying = false;
    Module.FlushRefundUpdate();
    TestFalse(TEXT("Finishing a selection flushes immediately despite the throttle"), Module.bRefundUpdatePending);
    Module.DeferRefundUpdate(State, false);
    Module.ActiveState.Reset();
    Module.FlushRefundUpdate();
    TestFalse(TEXT("An ended state drops pending refund work"), Module.bRefundUpdatePending);
    return true;
}

#endif
