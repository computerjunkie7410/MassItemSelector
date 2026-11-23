#include "MISModule.h"
#include "MISInputBindings.h"
#include "FGInputLibrary.h"
#include "MISOverlay.h"
#include "MISHUDHooks.h"
#include "MISConfiguration.h"
#include "MISSelectionObserver.h"
#include "MISSelectionRange.h"
#include "MISSelectionBudget.h"
#include "MISBuildableEligibility.h"
#include "FGRecipe.h"
#include "FGSchematicManager.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableWire.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SplineComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "FGBuildableSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGHUD.h"
#include "UI/FGBaseUI.h"
#include "Blueprint/UserWidget.h"
#include "EnhancedInputComponent.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGSplineBuildableInterface.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "InputCoreTypes.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "Patching/NativeHookManager.h"
#include "UObject/UObjectArray.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "ItemSelector"

DEFINE_LOG_CATEGORY_STATIC(LogMIS, Log, All);

namespace
{
TAutoConsoleVariable<float> MaxDistance(TEXT("ItemSelector.MaxDistanceMeters"), 100.f,
    TEXT("Maximum selection distance in meters, clamped to 5-500. Follows current build-gun range until manually edited; retained for this session."));
TAutoConsoleVariable<float> BatchMilliseconds(TEXT("ItemSelector.BatchMilliseconds"), 2.f,
    TEXT("Approximate selection processing budget per frame; minimum 0.25ms."));

FVector CenterOf(AFGBuildable* Actor)
{
    if (const auto* Wire = Cast<AFGBuildableWire>(Actor))
        return (Wire->GetConnectionLocation(0) + Wire->GetConnectionLocation(1)) * 0.5;
    if (const auto* SplineBuildable = Cast<IFGSplineBuildableInterface>(Actor))
        if (const USplineComponent* Spline = SplineBuildable->GetSplineComponent())
            return Spline->GetLocationAtDistanceAlongSpline(Spline->GetSplineLength() * 0.5f,
                ESplineCoordinateSpace::World);
    const FBox Bounds = Actor->GetCachedBounds();
    return Bounds.IsValid ? Bounds.GetCenter() : Actor->GetActorLocation();
}
}

void FMISModule::StartupModule()
{
    WorldCleanupHook = FWorldDelegates::OnWorldCleanup.AddLambda([this](UWorld* World, bool, bool) {
        if (ActiveWorld.Get() == World) EndState(ActiveState.Get());
    });
#if !WITH_EDITOR
    auto* CDO = GetMutableDefault<UFGBuildGunStateDismantle>();
    PrimaryFireHook = SUBSCRIBE_METHOD_VIRTUAL(UFGBuildGunStateDismantle::PrimaryFire_Implementation,
        CDO, [this](auto& Scope, UFGBuildGunStateDismantle* State) {
            if (ActiveState.Get() == State && !bSelectionInteractive && !IsApplying() &&
                State->GetBuildGun()->GetCurrentBuildGunMode() == State->mDefaultDismantleMode &&
                bLassoKeyDown) Scope.Cancel();
        });
    TickHook = SUBSCRIBE_METHOD_VIRTUAL_AFTER(UFGBuildGunStateDismantle::TickState_Implementation,
        CDO, [this](UFGBuildGunStateDismantle* State, float) { TickState(State); });
    EndHook = SUBSCRIBE_METHOD_VIRTUAL_AFTER(UFGBuildGunStateDismantle::EndState_Implementation,
        CDO, [this](UFGBuildGunStateDismantle* State) { EndState(State); });
    AimHook = SUBSCRIBE_METHOD(UFGBuildGunStateDismantle::SetAimedAtActor,
        [this](auto& Scope, UFGBuildGunStateDismantle* State, AActor* Actor) {
            if ((Overlay.IsValid() || bSelectionInteractive) && ActiveState.Get() == State && Actor) Scope.Cancel();
        });
    ClearHook = SUBSCRIBE_METHOD_AFTER(UFGBuildGunStateDismantle::ClearPendingSelectedActors,
        [this](UFGBuildGunStateDismantle* State) {
            if (ActiveState.Get() != State) return;
            MarkSelectionChanged();
            if (!bInsideSelectionMutation) ResetSelectionPanel();
            if (IsApplying())
                FinishApply(LOCTEXT("NativeSelectionCleared", "Selection stopped: native dismantle selection was cleared."));
        });
    RecipeFixupHook = SUBSCRIBE_METHOD(AFGSchematicManager::FixupSave_FindBuiltByRecipe,
        [this](auto& Scope, AFGSchematicManager*, AActor* Actor) {
            if (const auto Recipe = GetLightweightSpawnRecipe(Actor)) Scope.Override(Recipe);
        });
    LightweightSpawnHook = SUBSCRIBE_METHOD(AFGLightweightBuildableSubsystem::FindOrSpawnBuildableForRuntimeData,
        [this](auto& Scope, AFGLightweightBuildableSubsystem* Subsystem, TSubclassOf<AFGBuildable> Class,
            FRuntimeBuildableInstanceData* Data, int32 Index, bool& bSpawned) {
            // BeginPlay can request recipe recovery before the pool assigns the instance recipe.
            TGuardValue<TSubclassOf<AFGBuildable>> ClassGuard(LightweightSpawnClass, Class);
            TGuardValue<TSubclassOf<UFGRecipe>> RecipeGuard(LightweightSpawnRecipe,
                Data && IsValid(Data->BuiltWithRecipe.Get()) ? Data->BuiltWithRecipe : nullptr);
            TGuardValue<TWeakObjectPtr<UWorld>> WorldGuard(LightweightSpawnWorld, Subsystem->GetWorld());
            Scope(Subsystem, Class, Data, Index, bSpawned);
        });
    RefundHook = SUBSCRIBE_METHOD(UFGBuildGunStateDismantle::UpdatePeekDismantleRefunds,
        [this](auto& Scope, UFGBuildGunStateDismantle* State, bool bNoBuildCost) {
            if (DeferRefundUpdate(State, bNoBuildCost)) Scope.Cancel();
        });
    UE_LOG(LogMIS, Log, TEXT("Item Selector runtime hooks installed; hold the configured lasso key (Left Alt by default) in Default dismantle mode."));
#else
    UE_LOG(LogMIS, Log, TEXT("Item Selector editor module loaded; runtime hooks require the packaged game."));
#endif
}

void FMISModule::ShutdownModule()
{
    FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHook);
    EndState(ActiveState.Get());
#if !WITH_EDITOR
    if (PrimaryFireHook.IsValid()) UNSUBSCRIBE_METHOD(UFGBuildGunStateDismantle::PrimaryFire_Implementation, PrimaryFireHook);
    if (TickHook.IsValid()) UNSUBSCRIBE_METHOD(UFGBuildGunStateDismantle::TickState_Implementation, TickHook);
    if (EndHook.IsValid()) UNSUBSCRIBE_METHOD(UFGBuildGunStateDismantle::EndState_Implementation, EndHook);
    if (AimHook.IsValid()) UNSUBSCRIBE_METHOD(UFGBuildGunStateDismantle::SetAimedAtActor, AimHook);
    if (ClearHook.IsValid()) UNSUBSCRIBE_METHOD(UFGBuildGunStateDismantle::ClearPendingSelectedActors, ClearHook);
    if (RecipeFixupHook.IsValid()) UNSUBSCRIBE_METHOD(AFGSchematicManager::FixupSave_FindBuiltByRecipe, RecipeFixupHook);
    if (LightweightSpawnHook.IsValid()) UNSUBSCRIBE_METHOD(AFGLightweightBuildableSubsystem::FindOrSpawnBuildableForRuntimeData, LightweightSpawnHook);
    if (RefundHook.IsValid()) UNSUBSCRIBE_METHOD(UFGBuildGunStateDismantle::UpdatePeekDismantleRefunds, RefundHook);
#endif
}

void FMISModule::TickState(UFGBuildGunStateDismantle* State)
{
    if (Feedback.IsValid() && FeedbackExpires > 0 && FPlatformTime::Seconds() >= FeedbackExpires) RemoveFeedback();
    AFGBuildGun* Gun = State->GetBuildGun();
    AFGCharacterPlayer* Character = Gun ? Gun->GetInstigatorCharacter() : nullptr;
    APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
    if (!PC || !PC->IsLocalController() || !PC->GetLocalPlayer()) return;
    ON_SCOPE_EXIT { if (!bDeferringRefundUpdates) FlushRefundUpdate(); };
    TGuardValue<bool> RefundGuard(bDeferringRefundUpdates, true);
    if (ActiveState.Get() != State)
    {
        if (ActiveState.IsValid()) EndState(ActiveState.Get());
        BuildableEligibility.Reset();
        ActiveState = State;
        Player = PC;
        ActiveWorld = PC->GetWorld();
        SelectionObserver.Reset(NewObject<UMISSelectionObserver>());
        SelectionObserver->OnChanged = [this] { MarkSelectionChanged(); };
        State->OnPendingDismantleActorsChanged.AddDynamic(SelectionObserver.Get(), &UMISSelectionObserver::SelectionChanged);
        bSelectionDirty = true;
        MarkSelectionChanged();
    }
    InstallDismantleInput();
    UpdateDismantleHints();
    if (bExitDismantleRequested)
    {
        bExitDismantleRequested = false;
        CloseSelectionInteraction();
        CloseOverlay();
        PC->GetWorld()->GetTimerManager().SetTimerForNextTick(
            FTimerDelegate::CreateWeakLambda(Character, [Character] { Character->UnequipBuildGun(); }));
        return;
    }
    if (bCycleModeRequested)
    {
        bCycleModeRequested = false;
        Gun->CycleBuildMode(1);
    }
    const auto Mode = Gun->GetCurrentBuildGunMode();
    if (LastDismantleMode.Get() != Mode.Get())
    {
        LastDismantleMode = Mode.Get();
        if (IsApplying()) FinishApply(LOCTEXT("DismantleModeChanged", "Selection stopped: dismantle mode changed."));
        CloseSelectionInteraction();
        CloseOverlay();
    }
    const bool bLassoKeyPressed = bLassoKeyDown && !bLassoKeyWasDown;
    bLassoKeyWasDown = bLassoKeyDown;
    TickSelectionPanel();
    if (IsApplying())
    {
        if (!PC->IsPaused()) ProcessBatch();
        return;
    }
    if (Overlay.IsValid())
    {
        if (!FSlateApplication::Get().IsActive() || PC->IsPaused())
        {
            bOverlaySuspended = true;
            return;
        }
        if (Overlay->IsDrawingEnabled() && (!bLassoKeyDown || bReleaseLassoRequested))
        {
            ReleaseLasso();
            if (!Overlay.IsValid()) return;
        }
        else if (bLassoKeyPressed)
        {
            bReleaseLassoRequested = false;
            Overlay->ClearSelection();
            Overlay->SetDrawingEnabled(true);
        }
        if (bClearLassoRequested)
        {
            CloseOverlay();
            return;
        }
        if (PendingLassoWheel != 0)
        {
            AdjustDistance(PendingLassoWheel * GetWheelStep());
            PendingLassoWheel = 0;
        }
        if (bApplyLassoRequested && !bScanning && !bRefreshPreview)
        {
            bApplyLassoRequested = false;
            ApplyPreview();
            if (!Overlay.IsValid()) return;
        }
        if (bOverlaySuspended)
        {
            bOverlaySuspended = false;
            if (bLassoInputCaptured) FSlateApplication::Get().SetKeyboardFocus(Overlay);
        }
        if (Overlay->HasOutline() && !ViewIsUnchanged())
        {
            if (!Overlay->IsDrawingEnabled())
            {
                CloseOverlay();
                return;
            }
            Overlay->ClearSelection();
            LastResult = LOCTEXT("ViewChangedRedraw", "View changed. Draw a new lasso from this position.");
            ShowFeedback(LastResult);
        }
        if (Overlay->IsDrawingEnabled()) Overlay->ResumeInput();
        if (bRefreshPreview && FPlatformTime::Seconds() >= RefreshPreviewAt)
        {
            TArray<FVector2D> Points;
            for (const auto& P : Polygon) Points.Add(FVector2D(P.X, P.Y));
            StartSelection(Points, StrokeSize);
        }
        if (bScanning) ProcessBatch();
    }
    else if (!bSelectionInteractive && SelectionOperations.IsEmpty() && !PC->bShowMouseCursor && !PC->IsPaused() &&
        bLassoKeyPressed && Mode == State->mDefaultDismantleMode && FSlateApplication::Get().IsActive())
    {
        OpenOverlay();
    }
}

void FMISModule::EndState(UFGBuildGunStateDismantle* State)
{
    if (ActiveState.Get() != State) return;
    UnbindSelectionObserver();
    ResetSelectionPanel();
    CloseOverlay();
    RemoveDismantleInput();
    RemoveDismantleHints();
    RemoveFeedback();
    ReleaseTemporaryPins(true);
    ActiveState.Reset();
    bRefundUpdatePending = false;
    NextRefundUpdate = 0;
    Player.Reset();
    ActiveWorld.Reset();
    SelectedActorLookup.Empty();
    bSelectedActorLookupDirty = true;
    BuildableEligibility.Reset();
    LastDismantleMode.Reset();
    bCycleModeRequested = false;
    bExitDismantleRequested = false;
    bLassoKeyDown = bLassoKeyWasDown = false;
}

void FMISModule::ExitDismantleMode()
{
    bExitDismantleRequested = true;
}

bool FMISModule::HandleDismantleShortcut(const FKeyEvent& Event)
{
    const FKey Key = Event.GetKey();
    const auto& Modifiers = Event.GetModifierKeys();
    if (Key == EKeys::Escape && Overlay.IsValid())
    {
        if (!Event.IsRepeat()) bClearLassoRequested = true;
        return true;
    }
    if (InputBindings.Dismantle.Matches(Key, Modifiers))
    {
        if (!Event.IsRepeat()) ExitDismantleMode();
        return true;
    }
    if (InputBindings.Close.Matches(Key, Modifiers))
    {
        if (!Event.IsRepeat())
        {
            if (bSelectionInteractive) CloseSelectionInteraction();
            else if (Overlay.IsValid()) bClearLassoRequested = true;
            else ExitDismantleMode();
        }
        return true;
    }
    if (InputBindings.CycleMode.Matches(Key, Modifiers))
    {
        if (!Event.IsRepeat()) bCycleModeRequested = true;
        return true;
    }
    if (bSelectionInteractive && InputBindings.SelectionFilter.Matches(Key, Modifiers))
    {
        if (!Event.IsRepeat()) CloseSelectionInteraction();
        return true;
    }
    return false;
}

void FMISModule::InstallDismantleInput()
{
    auto* PC = Player.Get();
    auto* Input = PC ? Cast<UEnhancedInputComponent>(PC->InputComponent) : nullptr;
    if (!Input || DismantleInput.Get() == Input) return;
    RemoveDismantleInput();
    InputBindings.Update(PC);
    SelectionPanelBinding = Input->BindActionValueLambda(InputBindings.SelectionFilter.Action.Get(), ETriggerEvent::Started,
        [this](const FInputActionValue&) { bSelectionRequested = true; }).GetHandle();
    DismantleInput = Input;
    LassoAction.Reset(MIS::GetLassoAction());
    UFGInputLibrary::GetCurrentMappingForAction(PC, MIS::LassoMappingName, LassoKey, LassoModifiers);
    LassoBindings.Add(Input->BindActionValueLambda(LassoAction.Get(), ETriggerEvent::Started,
        [this](const FInputActionValue&) { bLassoKeyDown = true; }).GetHandle());
    auto ReleaseKey = [this](const FInputActionValue&) { bLassoKeyDown = false; bReleaseLassoRequested = true; };
    LassoBindings.Add(Input->BindActionValueLambda(LassoAction.Get(), ETriggerEvent::Completed, ReleaseKey).GetHandle());
    LassoBindings.Add(Input->BindActionValueLambda(LassoAction.Get(), ETriggerEvent::Canceled, ReleaseKey).GetHandle());
    LassoInputProcessor = CreateLassoInputProcessor();
    FSlateApplication::Get().RegisterInputPreProcessor(LassoInputProcessor);
}

void FMISModule::RemoveDismantleInput()
{
    if (LassoInputProcessor) FSlateApplication::Get().UnregisterInputPreProcessor(LassoInputProcessor);
    LassoInputProcessor.Reset();
    if (auto* Input = DismantleInput.Get())
    {
        Input->RemoveBindingByHandle(SelectionPanelBinding);
        for (uint32 Binding : LassoBindings) Input->RemoveBindingByHandle(Binding);
    }
    LassoBindings.Reset();
    LassoAction.Reset();
    DismantleInput.Reset();
    SelectionPanelBinding = 0;
    bSelectionRequested = false;
}

void FMISModule::RemoveDismantleHints()
{
    for (auto& Pair : DismantleHintTargets)
        if (auto* Target = Pair.Key.Get())
            if (Target->GetWorld() && !Target->GetWorld()->bIsTearingDown)
                UMISHUDHooks::UpdateDismantleHints(Target, EMISDismantleHints::Native, Pair.Value, Player.Get(), InputBindings);
    DismantleHintTargets.Reset();
    UMISHUDHooks::ClearNativeHintState();
    NextHintScan = 0;
    NextHintUpdate = 0;
    HintHUDRoot.Reset();
}

void FMISModule::UpdateDismantleHints()
{
    auto* PC = Player.Get();
    const double Now = FPlatformTime::Seconds();
    if (!PC || Now < NextHintUpdate) return;
    auto* HUD = Cast<AFGHUD>(PC->GetHUD());
    UUserWidget* Root = HUD ? HUD->GetBaseUI() : nullptr;
    if (Root != HintHUDRoot.Get())
    {
        RemoveDismantleHints();
        HintHUDRoot = Root;
    }
    NextHintUpdate = Now + 0.25;
    InputBindings.Update(PC);
    FKey ConfiguredKey;
    TArray<FKey> ConfiguredModifiers;
    UFGInputLibrary::GetCurrentMappingForAction(PC, MIS::LassoMappingName, ConfiguredKey, ConfiguredModifiers);
    if (ConfiguredKey != LassoKey || ConfiguredModifiers != LassoModifiers)
    {
        LassoKey = ConfiguredKey;
        LassoModifiers = MoveTemp(ConfiguredModifiers);
        bLassoKeyDown = bLassoKeyWasDown = false;
        bReleaseLassoRequested = Overlay.IsValid();
    }
    for (auto It = DismantleHintTargets.CreateIterator(); It; ++It)
    {
        auto* Target = It.Key().Get();
        if (!Target || Target->GetWorld() != PC->GetWorld() ||
            (Target->GetOwningPlayer() && Target->GetOwningPlayer() != PC)) It.RemoveCurrent();
    }
    auto* State = ActiveState.Get();
    EMISDismantleHints Mode = EMISDismantleHints::Native;
    if (Overlay.IsValid())
        Mode = Overlay->IsDrawingEnabled() ? EMISDismantleHints::Draw : EMISDismantleHints::Preview;
    else if (State && State->GetBuildGun()->GetCurrentBuildGunMode() == State->mDefaultDismantleMode)
        Mode = bSelectionInteractive || State->GetNumPendingDismantleActors(false) > 1
            ? EMISDismantleHints::Selection : EMISDismantleHints::Default;
    for (auto& Pair : DismantleHintTargets)
        UMISHUDHooks::UpdateDismantleHints(Pair.Key.Get(), Mode, Pair.Value, PC, InputBindings);
    if (!DismantleHintTargets.IsEmpty() && Now < NextHintScan) return;
    NextHintScan = Now + 2.0;
    for (TObjectIterator<UUserWidget> It; It; ++It)
    {
        auto* Target = *It;
        if (Target->IsTemplate() || Target->GetClass()->GetFName() != TEXT("Widget_DismantleMode_C") ||
            Target->GetWorld() != PC->GetWorld() || (Target->GetOwningPlayer() && Target->GetOwningPlayer() != PC)) continue;
        if (!DismantleHintTargets.Contains(Target))
            UMISHUDHooks::UpdateDismantleHints(Target, Mode, DismantleHintTargets.Add(Target), PC, InputBindings);
    }
}

void FMISModule::OpenOverlay()
{
    CloseSelectionInteraction();
    APlayerController* PC = Player.Get();
    UFGBuildGunStateDismantle* State = ActiveState.Get();
    UGameViewportClient* Viewport = PC && PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr;
    if (!Viewport || !State || !FSlateApplication::IsInitialized()) return;
    bDistanceWasEdited |= MaxDistance.GetValueOnGameThread() != LastAutomaticFar;
    AFGBuildGun* Gun = State->GetBuildGun();
    if (!Gun) return;
    if (!bDistanceWasEdited)
    {
        const float NativeRange = Gun->GetBuildGunRange();
        if (!FMath::IsFinite(NativeRange) || NativeRange <= 0)
        {
            UE_LOG(LogMIS, Warning, TEXT("Cannot open lasso: invalid native build-gun range %f."), NativeRange);
            return;
        }
        LastAutomaticFar = MIS::ClampSelectionDistanceMeters(NativeRange / 100.f);
        MaxDistance.AsVariable()->Set(LastAutomaticFar, ECVF_SetByConsole);
    }
    RemoveFeedback();
    State->ResetBuildGunDelay();
    State->SetAimedAtActor(nullptr);
    bPreviousCursor = PC->bShowMouseCursor;
    Overlay = SNew(SMISOverlay).Owner(this);
    if (SelectionPanel.IsValid()) SelectionPanel->SetVisibility(EVisibility::Collapsed);
    OpenViewport = Viewport;
    Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 100);
    SetLassoInputCaptured(true);
    LastResult = FText::GetEmpty();
    NextHintUpdate = 0;
    UpdateDismantleHints();
}

void FMISModule::CloseOverlay()
{
    DiscardPreview();
    ReleaseOverlay();
}

void FMISModule::ReleaseOverlay()
{
    bOverlaySuspended = false;
    if (!Overlay.IsValid()) return;
    SetLassoInputCaptured(false);
    bReleaseLassoRequested = bApplyLassoRequested = bClearLassoRequested = false;
    PendingLassoWheel = 0;
    TSharedPtr<SMISOverlay> Closing = Overlay;
    Overlay.Reset();
    NextHintUpdate = 0;
    UpdateDismantleHints();
    if (OpenViewport.IsValid()) OpenViewport->RemoveViewportWidgetContent(Closing.ToSharedRef());
    OpenViewport.Reset();
}

void FMISModule::SetLassoInputCaptured(bool bCapture)
{
    if (bLassoInputCaptured == bCapture) return;
    bLassoInputCaptured = bCapture;
    if (bCapture) bReleaseLassoRequested = false;
    Overlay->SetDrawingEnabled(bCapture);
    Overlay->SetVisibility(bCapture ? EVisibility::Visible : EVisibility::HitTestInvisible);
    if (auto* PC = Player.Get())
    {
        PC->SetIgnoreLookInput(bCapture);
        PC->SetIgnoreMoveInput(bCapture);
        PC->bShowMouseCursor = bCapture ? true : bPreviousCursor;
        if (bCapture)
        {
            FInputModeGameAndUI Mode;
            Mode.SetWidgetToFocus(Overlay);
            Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            PC->SetInputMode(Mode);
            FSlateApplication::Get().SetKeyboardFocus(Overlay);
        }
        else
        {
            FSlateApplication::Get().ReleaseAllPointerCapture();
            PC->SetInputMode(FInputModeGameOnly());
        }
    }
}

void FMISModule::ReleaseLasso()
{
    bReleaseLassoRequested = false;
    if (!Overlay->FinishStroke() || StrokeSize.X <= 0)
    {
        ReleaseOverlay();
        return;
    }
    Overlay->SetDrawingEnabled(false);
}

void FMISModule::BeginStroke()
{
    DiscardPreview();
    if (APlayerController* PC = Player.Get())
    {
        PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
        PC->GetViewportSize(ViewportSize.X, ViewportSize.Y);
        ViewFOV = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 0;
    }
}

bool FMISModule::IsDefaultDismantleMode() const
{
    const auto* State = ActiveState.Get();
    return State && LastDismantleMode.IsValid() && LastDismantleMode.Get() == State->mDefaultDismantleMode.Get();
}

bool FMISModule::ViewIsUnchanged() const
{
    APlayerController* PC = Player.Get();
    if (!PC) return false;
    FVector Location;
    FRotator Rotation;
    FIntPoint Size;
    PC->GetPlayerViewPoint(Location, Rotation);
    PC->GetViewportSize(Size.X, Size.Y);
    const float FOV = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 0;
    return Size == ViewportSize && Location.Equals(ViewLocation, 1.f) &&
        Rotation.Equals(ViewRotation, 0.05f) && FMath::IsNearlyEqual(FOV, ViewFOV, 0.01f);
}

void FMISModule::StartSelection(const TArray<FVector2D>& Points, FVector2D LocalSize)
{
    CancelScan();
    APlayerController* PC = Player.Get();
    if (!PC || !ActiveState.IsValid() || LocalSize.X <= 0 || LocalSize.Y <= 0) return;
    Polygon.Reset();
    for (const FVector2D P : Points) Polygon.Add({ P.X, P.Y });
    if (!MIS::IsUsable({ Polygon.GetData(), static_cast<size_t>(Polygon.Num()) }))
    {
        LastResult = LOCTEXT("InvalidOutline", "Outline too small or flat; draw an area.");
        ShowFeedback(LastResult);
        return;
    }
    if (!ViewIsUnchanged() || ViewportSize.X <= 0 || ViewportSize.Y <= 0)
    {
        LastResult = LOCTEXT("CameraChanged", "Camera moved or viewport resized. Stop moving and redraw.");
        ShowFeedback(LastResult);
        return;
    }
    StrokeSize = LocalSize;
    StrokeFarMeters = GetFarDistance();
    PolygonMin = PolygonMax = Points[0];
    for (const FVector2D P : Points)
    {
        PolygonMin.X = FMath::Min(PolygonMin.X, P.X - 0.5);
        PolygonMin.Y = FMath::Min(PolygonMin.Y, P.Y - 0.5);
        PolygonMax.X = FMath::Max(PolygonMax.X, P.X + 0.5);
        PolygonMax.Y = FMath::Max(PolygonMax.Y, P.Y + 0.5);
    }
    if (auto* Subsystem = AFGBuildableSubsystem::Get(PC))
        BuildableCount = Subsystem->GetAllBuildablesRef().Num();
    if (auto* Subsystem = AFGLightweightBuildableSubsystem::Get(PC->GetWorld()))
        for (const auto& Pair : Subsystem->GetAllLightweightBuildableInstances())
            LightweightScans.Add({ Pair.Key.Get(), Pair.Value.Num() });
    bGatheringCandidates = true;
    NextCandidate = Added = Rejected = 0;
    bScanning = true;
    bApplying = false;
    LastResult = LOCTEXT("GatheringCandidates", "Counting objects in range; nothing added yet.");
}

void FMISModule::ApplyPreview()
{
    if (!bPreviewReady || !ActiveState.IsValid()) return;
    if (!ViewIsUnchanged())
    {
        CancelScan();
        LastResult = LOCTEXT("ViewChangedApply", "View changed. Draw a new lasso before adding objects.");
        ShowFeedback(LastResult);
        return;
    }
    if (StrokeFarMeters != GetFarDistance())
    {
        QueuePreviewRefresh();
        return;
    }
    auto* State = ActiveState.Get();
    const int32 Limit = State->GetMaxNumPendingDismantleActors();
    if (MIS::RemainingCapacity(Limit, State->GetNumPendingDismantleActors(false)) == 0)
    {
        FinishApply(FText::Format(LOCTEXT("SelectionAlreadyFull", "Added 0 objects: dismantle limit ({0}) already reached. Clear some selection before adding more."), Limit));
        return;
    }
    Candidates = MoveTemp(PreviewCandidates);
    NextCandidate = Added = Rejected = 0;
    bPreviewReady = false;
    bApplying = bScanning = true;
    ReleaseOverlay();
    ShowFeedback(FText::GetEmpty(), true);
}

bool FMISModule::HasObjectHeadroom() const
{
    return GUObjectArray.GetObjectArrayCapacity() - GUObjectArray.GetObjectArrayNumMinusAvailable() >= 65536;
}

bool FMISModule::IsCenterInRange(const FVector& Center) const
{
    return MIS::WithinMaxDistance(
        FVector::DistSquared(ViewLocation, Center), StrokeFarMeters * 100.0);
}

bool FMISModule::IsCenterInsideLasso(APlayerController* PC, const FVector& Center,
    const FVector& ViewForward) const
{
    if (FVector::DotProduct(Center - ViewLocation, ViewForward) <= 0) return false;
    FVector2D Screen;
    if (!PC->ProjectWorldLocationToScreen(Center, Screen, false)) return false;
    if (Screen.X < 0 || Screen.Y < 0 || Screen.X > ViewportSize.X || Screen.Y > ViewportSize.Y) return false;
    const FVector2D Local(Screen.X * StrokeSize.X / ViewportSize.X, Screen.Y * StrokeSize.Y / ViewportSize.Y);
    if (Local.X < PolygonMin.X || Local.Y < PolygonMin.Y || Local.X > PolygonMax.X || Local.Y > PolygonMax.Y) return false;
    return MIS::Contains({ Polygon.GetData(), static_cast<size_t>(Polygon.Num()) }, { Local.X, Local.Y });
}

void FMISModule::RefreshSelectedActorLookup(const UFGBuildGunStateDismantle* State)
{
    SelectedActorLookup.Reset();
    bSelectedActorLookupDirty = false;
    if (!State) return;
    SelectedActorLookup.Reserve(State->mPendingDismantleActors.Num());
    for (AActor* Actor : State->mPendingDismantleActors) SelectedActorLookup.Add(Actor);
}

void FMISModule::UpdateSelectedActorLookup(const UFGBuildGunStateDismantle* State, AActor* Actor, int32 PreviousCount)
{
    if (State->mPendingDismantleActors.Num() == PreviousCount + 1 && State->mPendingDismantleActors.Last() == Actor)
    {
        SelectedActorLookup.Add(Actor);
        bSelectedActorLookupDirty = false;
    }
    else RefreshSelectedActorLookup(State);
}

bool FMISModule::GatherCandidates(double Deadline)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ItemSelector_Snapshot);
    auto* PC = Player.Get();
    auto* Buildables = AFGBuildableSubsystem::Get(PC);
    auto* Lightweight = AFGLightweightBuildableSubsystem::Get(PC->GetWorld());
    int32 Examined = 0;
    while (NextBuildable < BuildableCount || NextLightweightClass < LightweightScans.Num())
    {
        if (Examined++ > 0 && FPlatformTime::Seconds() >= Deadline) return false;
        FCandidate Candidate;
        if (NextBuildable < BuildableCount)
        {
            const auto* Actors = Buildables ? &Buildables->GetAllBuildablesRef() : nullptr;
            if (!Actors || !Actors->IsValidIndex(NextBuildable))
            {
                NextBuildable = BuildableCount;
                continue;
            }
            AFGBuildable* Actor = (*Actors)[NextBuildable++];
            if (!IsValid(Actor) || Actor->GetIsLightweightTemporary()) continue;
            Candidate.Center = CenterOf(Actor);
            if (!IsCenterInRange(Candidate.Center) || !BuildableEligibility.IsActorSelectable(Actor)) continue;
            Candidate.Actor = Actor;
        }
        else
        {
            const auto& Scan = LightweightScans[NextLightweightClass];
            UClass* Class = Scan.Class.Get();
            const auto* Instances = Lightweight && Class
                ? Lightweight->GetAllLightweightBuildableInstances().Find(Class) : nullptr;
            if (!Instances || NextLightweightInstance >= FMath::Min(Scan.Count, Instances->Num()))
            {
                ++NextLightweightClass;
                NextLightweightInstance = 0;
                continue;
            }
            const int32 Index = NextLightweightInstance++;
            const auto& Data = (*Instances)[Index];
            if (!Data.IsValid()) continue;
            Candidate.Center = Data.BoundingBox.IsValid
                ? Data.Transform.TransformPosition(Data.BoundingBox.GetCenter()) : Data.Transform.GetLocation();
            if (!IsCenterInRange(Candidate.Center) || !BuildableEligibility.IsTypeSelectable(Class,
                UFGRecipe::GetDescriptorForRecipe(Data.BuiltWithRecipe))) continue;
            Candidate.LightweightClass = Class;
            Candidate.LightweightIndex = Index;
            Candidate.Transform = Data.Transform;
        }
        Candidates.Add(Candidate);
    }
    LightweightScans.Reset();
    bGatheringCandidates = false;
    return true;
}

void FMISModule::ProcessBatch()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ItemSelector_ProcessBatch);
    APlayerController* PC = Player.Get();
    UFGBuildGunStateDismantle* State = ActiveState.Get();
    if (!PC || !State || (!bApplying && !ViewIsUnchanged()))
    {
        const bool bWasApplying = bApplying;
        CancelScan();
        LastResult = bWasApplying
            ? LOCTEXT("ApplyStateUnavailable", "Selection stopped: player or dismantle state unavailable. Already-added objects remain selected.")
            : LOCTEXT("PreviewViewChanged", "Preview stopped: view changed. Selection unchanged; draw a new lasso.");
        if (bWasApplying) FinishApply(LastResult);
        else ShowFeedback(LastResult);
        return;
    }
    const double Deadline = FPlatformTime::Seconds() +
        FMath::Clamp(BatchMilliseconds.GetValueOnGameThread(), 0.25f, 16.f) / 1000.0;
    if (bGatheringCandidates)
    {
        if (!GatherCandidates(Deadline) || FPlatformTime::Seconds() >= Deadline) return;
    }
    const FVector ViewForward = ViewRotation.Vector();
    int32 Examined = 0;
    while (NextCandidate < Candidates.Num())
    {
        const int32 Limit = State->GetMaxNumPendingDismantleActors();
        if (bApplying && MIS::RemainingCapacity(Limit, State->GetNumPendingDismantleActors(false)) == 0)
        {
            const int32 AddedBeforeStop = Added;
            CancelScan();
            LastResult = FText::Format(LOCTEXT("SelectionLimitReached", "Added {0} objects. Dismantle limit ({1}) reached; remaining lasso objects were not added."), AddedBeforeStop, Limit);
            FinishApply(LastResult);
            return;
        }
        if (Examined++ > 0 && FPlatformTime::Seconds() >= Deadline) break;
        const FCandidate Candidate = Candidates[NextCandidate++];
        if (!bApplying)
        {
            if (!IsCenterInsideLasso(PC, Candidate.Center, ViewForward)) continue;

            UClass* Class = Candidate.LightweightIndex != INDEX_NONE ? Candidate.LightweightClass.Get()
                : (Candidate.Actor.IsValid() ? Candidate.Actor->GetClass() : nullptr);
            const auto Filter = State->GetCurrentDismantleClassFilter();
            if (!Class || (Filter && !Class->IsChildOf(Filter))) continue;
            AFGBuildable* Existing = Candidate.Actor.Get();
            if (Candidate.LightweightIndex != INDEX_NONE)
                if (auto* Subsystem = AFGLightweightBuildableSubsystem::Get(PC->GetWorld()))
                    Existing = Subsystem->FindTemporaryByBuildableClassAndIndex(Class, Candidate.LightweightIndex);
            if (Existing)
            {
                if (bSelectedActorLookupDirty) RefreshSelectedActorLookup(State);
                if (SelectedActorLookup.Contains(Existing)) continue;
            }
            PreviewCandidates.Add(Candidate);
            continue;
        }

        if (!HasObjectHeadroom())
        {
            CancelScan();
            LastResult = LOCTEXT("ObjectBudgetLow", "Stopped: engine object budget is low. Partial additions remain; clear selection and save/reload before retrying.");
            UE_LOG(LogMIS, Warning, TEXT("%s"), *LastResult.ToString());
            FinishApply(LastResult);
            return;
        }

        AFGBuildable* Actor = Candidate.Actor.Get();
        if (Candidate.LightweightIndex != INDEX_NONE)
        {
            Actor = ResolveLightweightBuildable(Candidate.LightweightClass.Get(), Candidate.LightweightIndex, Candidate.Transform);
        }
        if (!IsApplying()) return;
        if (IsValid(Actor)) AddActor(Actor);
        if (!IsApplying()) return;
    }
    if (NextCandidate >= Candidates.Num())
    {
        if (!bApplying)
        {
            FinishPreview();
            return;
        }
        bScanning = bApplying = false;
        UE_LOG(LogMIS, Verbose, TEXT("%d objects rejected by native dismantle rules/filter."), Rejected);
        LastResult = FText::Format(LOCTEXT("SelectionAdded", "Added {0} objects."), Added);
        FinishApply(LastResult);
    }
}

void FMISModule::RemoveFeedback()
{
    if (Feedback.IsValid() && FeedbackViewport.IsValid())
        FeedbackViewport->RemoveViewportWidgetContent(Feedback.ToSharedRef());
    Feedback.Reset();
    FeedbackViewport.Reset();
}

void FMISModule::FinishPreview()
{
    bScanning = false;
    bPreviewReady = true;
    NextHintUpdate = 0;
    LastResult = FText::Format(LOCTEXT("SelectionFound", "Found {0} objects."), PreviewCandidates.Num());
    Candidates.Reset();
    ShowFeedback(LastResult);
}

void FMISModule::FinishApply(const FText& Message)
{
    const FText Result = Message;
    CloseOverlay();
    LastResult = Result;
    UE_LOG(LogMIS, Log, TEXT("%s"), *Result.ToString());
    ShowFeedback(Result);
}

void FMISModule::ShowFeedback(const FText& Message, bool bProgress)
{
    const auto* PC = Player.Get();
    UGameViewportClient* Viewport = PC && PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr;
    RemoveFeedback();
    if (!Viewport) return;
    Feedback = SNew(SConstraintCanvas)
        .Visibility(EVisibility::HitTestInvisible)
        + SConstraintCanvas::Slot().AutoSize(true)
        .Anchors(FAnchors(0.5f, 0.75f)).Alignment(FVector2D(0.5f, 0.5f))
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.9f)).Padding(12)
            [ SNew(STextBlock).WrapTextAt(850).Justification(ETextJustify::Center)
                .ColorAndOpacity(FLinearColor::White)
                .Text_Lambda([this, FixedMessage = Message, bProgress] {
                    return bProgress ? GetActivityStatus() : FixedMessage;
                }) ]
        ];
    FeedbackViewport = Viewport;
    FeedbackExpires = bProgress ? 0 : FPlatformTime::Seconds() + 6;
    Viewport->AddViewportWidgetContent(Feedback.ToSharedRef(), 101);
}

AFGBuildable* FMISModule::ResolveLightweightBuildable(UClass* Class, int32 Index, const FTransform& Transform)
{
    auto* PC = Player.Get();
    auto* Subsystem = PC ? AFGLightweightBuildableSubsystem::Get(PC->GetWorld()) : nullptr;
    auto* Data = Subsystem && Class ? Subsystem->GetRuntimeDataForBuildableClassAndIndex(Class, Index) : nullptr;
    if (!Data || !Data->IsValid() || !Data->Transform.Equals(Transform) || !IsValid(Data->BuiltWithRecipe.Get())) return nullptr;

    bool bSpawned = false;
    auto* Temporary = Subsystem->FindOrSpawnBuildableForRuntimeData(Class, Data, Index, bSpawned);
    return Temporary ? Temporary->Buildable.Get() : nullptr;
}

TSubclassOf<UFGRecipe> FMISModule::GetLightweightSpawnRecipe(const AActor* Actor) const
{
    return Actor && Actor->GetClass() == LightweightSpawnClass && Actor->GetWorld() == LightweightSpawnWorld.Get()
        ? LightweightSpawnRecipe : nullptr;
}

bool FMISModule::DeferRefundUpdate(UFGBuildGunStateDismantle* State, bool bNoBuildCost)
{
    if (bFlushingRefundUpdate || ActiveState.Get() != State || (!bDeferringRefundUpdates && !IsSelectionBusy())) return false;
    bRefundUpdatePending = true;
    bRefundNoBuildCost = bNoBuildCost;
    return true;
}

void FMISModule::FlushRefundUpdate()
{
    if (!bRefundUpdatePending) return;
    const double Now = FPlatformTime::Seconds();
    if (IsSelectionBusy() && Now < NextRefundUpdate) return;
    bRefundUpdatePending = false;
    NextRefundUpdate = Now + 0.25;
    auto* State = ActiveState.Get();
    auto* World = ActiveWorld.Get();
    if (State && World && !World->bIsTearingDown)
    {
        TGuardValue<bool> FlushGuard(bFlushingRefundUpdate, true);
        State->UpdatePeekDismantleRefunds(bRefundNoBuildCost);
    }
}

void FMISModule::AddActor(AFGBuildable* Actor)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ItemSelector_AddActor);
    UFGBuildGunStateDismantle* State = ActiveState.Get();
    if (!State || !BuildableEligibility.IsActorSelectable(Actor)) return;
    if (bSelectedActorLookupDirty) RefreshSelectedActorLookup(State);
    if (SelectedActorLookup.Contains(Actor)) return;
    if (MIS::RemainingCapacity(State->GetMaxNumPendingDismantleActors(), State->GetNumPendingDismantleActors(false)) == 0) return;
    const TSubclassOf<AActor> Filter = State->GetCurrentDismantleClassFilter();
    if ((Filter && !Actor->IsA(Filter)) || !State->CanDismantleActor(Actor)) { ++Rejected; return; }
    const bool bPin = Actor->GetIsLightweightTemporary() && !Actor->GetBlockCleanupOfTemporary();
    if (bPin) Actor->SetBlockCleanupOfTemporary(true);
    const int32 PreviousCount = State->mPendingDismantleActors.Num();
    {
        TGuardValue<bool> RemoveGuard(State->mShouldRemoveFromMultiSelect, false);
        State->AddPendingDismantleActor(Actor);
    }
    MarkSelectionChanged();
    UpdateSelectedActorLookup(State, Actor, PreviousCount);
    if (SelectedActorLookup.Contains(Actor))
    {
        ++Added;
        if (bPin) PinnedTemporaries.Add(Actor);
    }
    else
    {
        ++Rejected;
        if (bPin) Actor->SetBlockCleanupOfTemporary(false);
    }
}

void FMISModule::ReleaseTemporaryPins(bool bAll)
{
    if (!bAll && !bTemporaryPinsDirty) return;
    bTemporaryPinsDirty = false;
    if (PinnedTemporaries.IsEmpty()) return;
    TRACE_CPUPROFILER_EVENT_SCOPE(ItemSelector_ReleasePins);
    if (!bAll && bSelectedActorLookupDirty) RefreshSelectedActorLookup(ActiveState.Get());
    for (int32 I = PinnedTemporaries.Num() - 1; I >= 0; --I)
    {
        AFGBuildable* Actor = PinnedTemporaries[I].Get();
        if (!Actor || bAll || !SelectedActorLookup.Contains(Actor))
        {
            if (Actor) Actor->SetBlockCleanupOfTemporary(false);
            PinnedTemporaries.RemoveAtSwap(I);
        }
    }
}

void FMISModule::CancelScan()
{
    if (bScanning && bApplying) LastResult = FText::Format(LOCTEXT("LassoApplyStopped", "Stopped. {0} objects already added remain selected."), Added);
    else if (bScanning || bPreviewReady || bRefreshPreview) LastResult = LOCTEXT("PreviewCancelled", "Preview cancelled. Selection unchanged.");
    bScanning = false;
    bApplying = bPreviewReady = bRefreshPreview = false;
    Candidates.Reset();
    PreviewCandidates.Reset();
    LightweightScans.Reset();
    NextBuildable = BuildableCount = NextLightweightClass = NextLightweightInstance = 0;
    bGatheringCandidates = false;
    NextCandidate = 0;
}

void FMISModule::AdjustDistance(float DeltaMeters)
{
    SetFarDistance(GetFarDistance() + DeltaMeters);
}

void FMISModule::DiscardPreview()
{
    RemoveFeedback();
    NextHintUpdate = 0;
    CancelScan();
    Polygon.Reset();
    StrokeSize = FVector2D::ZeroVector;
}

float FMISModule::GetWheelStep() const
{
    return UMISConfigurationLibrary::GetWheelStep(Player.Get());
}

bool FMISModule::IsDistancePanelOnRight() const
{
    return UMISConfigurationLibrary::GetPanelSide(Player.Get()) == EMISDistancePanelSide::Right;
}

float FMISModule::GetFarDistance() const
{
    const float Value = MaxDistance.GetValueOnGameThread();
    return MIS::ClampSelectionDistanceMeters(Value);
}

void FMISModule::QueuePreviewRefresh()
{
    if (IsApplying()) return;
    CancelScan();
    bRefreshPreview = !Polygon.IsEmpty() && StrokeSize.X > 0 && ViewIsUnchanged();
    RefreshPreviewAt = FPlatformTime::Seconds() + 0.2;
    LastResult = FText::GetEmpty();
    RemoveFeedback();
}

void FMISModule::SetFarDistance(float Meters)
{
    if (IsApplying() || !FMath::IsFinite(Meters)) return;
    bDistanceWasEdited = true;
    const float Far = MIS::ClampSelectionDistanceMeters(Meters);
    MaxDistance.AsVariable()->Set(Far, ECVF_SetByConsole);
    QueuePreviewRefresh();
}

FText FMISModule::GetActivityStatus() const
{
    if (bGatheringCandidates) return LastResult;
    return ActivityTextCache.Get(bScanning, bApplying, NextCandidate,
        Candidates.Num(), PreviewCandidates.Num(), LastResult);
}

IMPLEMENT_MODULE(FMISModule, MassItemSelector)

#undef LOCTEXT_NAMESPACE
