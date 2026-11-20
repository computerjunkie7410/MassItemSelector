#include "MISModule.h"
#include "MISOverlay.h"
#include "MISHUDHooks.h"
#include "MISConfiguration.h"
#include "MISSelectionObserver.h"
#include "MISSelectionRange.h"
#include "MISSelectionBudget.h"
#include "MISBuildableEligibility.h"
#include "FGRecipe.h"
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
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGSplineBuildableInterface.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "InputCoreTypes.h"
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
        if (ActiveWorld.Get() != World) return;
        if (ActiveState.IsValid()) EndState(ActiveState.Get());
        else
        {
            UnbindSelectionObserver();
            ResetSelectionPanel();
            CloseOverlay();
            RemoveMassSelectionInput();
            RemoveMassSelectionHints();
            RemoveFeedback();
            ReleaseTemporaryPins(true);
            BuildableEligibility.Reset();
            Player.Reset();
            ActiveWorld.Reset();
        }
    });
#if !WITH_EDITOR
    auto* CDO = GetMutableDefault<UFGBuildGunStateDismantle>();
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
            if (ActiveState.Get() == State) MarkSelectionChanged();
            if (ActiveState.Get() == State && !bInsideSelectionMutation) ResetSelectionPanel();
            if (ActiveState.Get() == State && IsApplying())
                FinishApply(TEXT("Selection stopped: native dismantle selection was cleared."));
        });
    UE_LOG(LogMIS, Log, TEXT("Item Selector runtime hooks installed; enter dismantle mode and press M for Mass Selection."));
#else
    UE_LOG(LogMIS, Log, TEXT("Item Selector editor module loaded; runtime hooks require the packaged game."));
#endif
}

void FMISModule::ShutdownModule()
{
    FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHook);
    if (ActiveState.IsValid()) EndState(ActiveState.Get());
    else CloseOverlay();
    UnbindSelectionObserver();
    ResetSelectionPanel();
    BuildableEligibility.Reset();
    RemoveMassSelectionInput();
    RemoveMassSelectionHints();
    RemoveFeedback();
#if !WITH_EDITOR
    if (TickHook.IsValid()) UNSUBSCRIBE_METHOD(UFGBuildGunStateDismantle::TickState_Implementation, TickHook);
    if (EndHook.IsValid()) UNSUBSCRIBE_METHOD(UFGBuildGunStateDismantle::EndState_Implementation, EndHook);
    if (AimHook.IsValid()) UNSUBSCRIBE_METHOD(UFGBuildGunStateDismantle::SetAimedAtActor, AimHook);
    if (ClearHook.IsValid()) UNSUBSCRIBE_METHOD(UFGBuildGunStateDismantle::ClearPendingSelectedActors, ClearHook);
#endif
}

void FMISModule::TickState(UFGBuildGunStateDismantle* State)
{
    if (Feedback.IsValid() && FeedbackExpires > 0 && FPlatformTime::Seconds() >= FeedbackExpires) RemoveFeedback();
    AFGBuildGun* Gun = State->GetBuildGun();
    AFGCharacterPlayer* Character = Gun ? Gun->GetInstigatorCharacter() : nullptr;
    APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
    if (!PC || !PC->IsLocalController() || !PC->GetLocalPlayer()) return;
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
    InstallMassSelectionInput();
    UpdateMassSelectionHints();
    const bool bOpenRequested = bMassSelectionRequested;
    bMassSelectionRequested = false;
    TickSelectionPanel();
    if (IsApplying())
    {
        if (!PC->IsPaused()) ProcessBatch();
        return;
    }
    if (Overlay.IsValid())
    {
        if (!FSlateApplication::Get().IsActive() || PC->IsPaused()) { CloseOverlay(); return; }
        HideNativeHUD();
        if ((bPreviewReady || bRefreshPreview) && !ViewIsUnchanged())
        {
            CancelScan();
            LastResult = TEXT("View changed. Draw a new lasso from this position.");
        }
        if (bRefreshPreview && FPlatformTime::Seconds() >= RefreshPreviewAt)
        {
            TArray<FVector2D> Points;
            for (const auto& P : Polygon) Points.Add(FVector2D(P.X, P.Y));
            StartSelection(Points, StrokeSize);
        }
        if (bScanning) ProcessBatch();
    }
    else if (!bSelectionInteractive && SelectionOperations.IsEmpty() && !PC->bShowMouseCursor && !PC->IsPaused() &&
        bOpenRequested && !PC->IsInputKeyDown(EKeys::LeftMouseButton))
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
    RemoveMassSelectionInput();
    RemoveMassSelectionHints();
    RemoveFeedback();
    ReleaseTemporaryPins(true);
    ActiveState.Reset();
    Player.Reset();
    ActiveWorld.Reset();
    SelectedActorLookup.Empty();
    bSelectedActorLookupDirty = true;
    BuildableEligibility.Reset();
}

void FMISModule::InstallMassSelectionInput()
{
    auto* PC = Player.Get();
    auto* Input = PC ? Cast<UEnhancedInputComponent>(PC->InputComponent) : nullptr;
    auto* Subsystem = PC && PC->GetLocalPlayer()
        ? PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
    if (!Input || !Subsystem || MassSelectionInput.Get() == Input) return;
    RemoveMassSelectionInput();
    if (!MassSelectionAction.IsValid())
    {
        MassSelectionAction.Reset(NewObject<UInputAction>());
        MassSelectionAction->ActionDescription = LOCTEXT("MassSelection", "Mass Selection");
        MassSelectionAction->bConsumeInput = true;
        MassSelectionContext.Reset(NewObject<UInputMappingContext>());
        MassSelectionContext->MapKey(MassSelectionAction.Get(), EKeys::M);
        SelectionPanelAction.Reset(NewObject<UInputAction>());
        SelectionPanelAction->ActionDescription = LOCTEXT("SelectionPanelAction", "Advanced Dismantle Filter");
        SelectionPanelAction->bConsumeInput = true;
        MassSelectionContext->MapKey(SelectionPanelAction.Get(), EKeys::N);
    }
    MassSelectionBinding = Input->BindActionValueLambda(MassSelectionAction.Get(), ETriggerEvent::Started,
        [this](const FInputActionValue&) { bMassSelectionRequested = true; }).GetHandle();
    SelectionPanelBinding = Input->BindActionValueLambda(SelectionPanelAction.Get(), ETriggerEvent::Started,
        [this](const FInputActionValue&) { bSelectionRequested = true; }).GetHandle();
    MassSelectionInput = Input;
    MassSelectionSubsystem = Subsystem;
    Subsystem->AddMappingContext(MassSelectionContext.Get(), 10000);
}

void FMISModule::RemoveMassSelectionInput()
{
    if (auto* Input = MassSelectionInput.Get())
    {
        Input->RemoveBindingByHandle(MassSelectionBinding);
        Input->RemoveBindingByHandle(SelectionPanelBinding);
    }
    if (auto* Subsystem = MassSelectionSubsystem.Get())
        if (MassSelectionContext.IsValid()) Subsystem->RemoveMappingContext(MassSelectionContext.Get());
    MassSelectionInput.Reset();
    MassSelectionSubsystem.Reset();
    MassSelectionBinding = 0;
    bMassSelectionRequested = false;
    bSelectionRequested = false;
}

void FMISModule::RemoveMassSelectionHints()
{
    for (const auto& WeakTarget : MassSelectionHintTargets)
        if (auto* Target = WeakTarget.Get())
            if (Target->GetWorld() && !Target->GetWorld()->bIsTearingDown)
                UMISHUDHooks::UpdateMassSelectionHint(Target, true);
    MassSelectionHintTargets.Reset();
    NextHintScan = 0;
    NextHintUpdate = 0;
    HintHUDRoot.Reset();
}

void FMISModule::UpdateMassSelectionHints()
{
    auto* PC = Player.Get();
    const double Now = FPlatformTime::Seconds();
    if (!PC || Now < NextHintUpdate) return;
    auto* HUD = Cast<AFGHUD>(PC->GetHUD());
    UUserWidget* Root = HUD ? HUD->GetBaseUI() : nullptr;
    if (Root != HintHUDRoot.Get())
    {
        RemoveMassSelectionHints();
        HintHUDRoot = Root;
    }
    NextHintUpdate = Now + 0.25;
    MassSelectionHintTargets.RemoveAll([PC](const auto& Target) {
        return !Target.IsValid() || Target->GetWorld() != PC->GetWorld() ||
            (Target->GetOwningPlayer() && Target->GetOwningPlayer() != PC);
    });
    for (const auto& Target : MassSelectionHintTargets)
        UMISHUDHooks::UpdateMassSelectionHint(Target.Get(), false);
    if (!MassSelectionHintTargets.IsEmpty() && Now < NextHintScan) return;
    NextHintScan = Now + 2.0;
    for (TObjectIterator<UUserWidget> It; It; ++It)
    {
        auto* Target = *It;
        if (Target->IsTemplate() || Target->GetClass()->GetFName() != TEXT("Widget_DismantleMode_C") ||
            Target->GetWorld() != PC->GetWorld() || (Target->GetOwningPlayer() && Target->GetOwningPlayer() != PC)) continue;
        if (!MassSelectionHintTargets.Contains(Target))
        {
            MassSelectionHintTargets.Add(Target);
            UMISHUDHooks::UpdateMassSelectionHint(Target, false);
        }
    }
    MassSelectionHintTargets.RemoveAll([](const auto& Target) { return !Target.IsValid(); });
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
    HideNativeHUD();
    State->ResetBuildGunDelay();
    State->SetAimedAtActor(nullptr);
    bPreviousCursor = PC->bShowMouseCursor;
    PC->SetIgnoreLookInput(true);
    PC->SetIgnoreMoveInput(true);
    Overlay = SNew(SMISOverlay).Owner(this);
    if (SelectionPanel.IsValid()) SelectionPanel->SetVisibility(EVisibility::Collapsed);
    OpenViewport = Viewport;
    Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 100);
    PC->bShowMouseCursor = true;
    FInputModeUIOnly Mode;
    Mode.SetWidgetToFocus(Overlay);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(Mode);
    FSlateApplication::Get().SetKeyboardFocus(Overlay);
    LastResult = TEXT("Make a selection. Drag with the left mouse button.");
}

void FMISModule::CloseOverlay()
{
    DiscardPreview();
    ReleaseOverlay();
}

void FMISModule::ReleaseOverlay()
{
    RestoreNativeHUD();
    if (!Overlay.IsValid()) return;
    TSharedPtr<SMISOverlay> Closing = Overlay;
    Overlay.Reset();
    if (OpenViewport.IsValid()) OpenViewport->RemoveViewportWidgetContent(Closing.ToSharedRef());
    OpenViewport.Reset();
    if (APlayerController* PC = Player.Get())
    {
        PC->SetIgnoreLookInput(false);
        PC->SetIgnoreMoveInput(false);
        PC->bShowMouseCursor = bPreviousCursor;
        PC->SetInputMode(FInputModeGameOnly());
    }
}

void FMISModule::RestoreNativeHUD()
{
    if (auto* Widget = HiddenHUDWidget.Get()) Widget->SetVisibility(PreviousHUDVisibility);
    HiddenHUDWidget.Reset();
}

void FMISModule::HideNativeHUD()
{
    auto* PC = Player.Get();
    auto* HUD = PC ? Cast<AFGHUD>(PC->GetHUD()) : nullptr;
    UUserWidget* Widget = HUD ? HUD->GetBaseUI() : nullptr;
    if (Widget != HiddenHUDWidget.Get())
    {
        RestoreNativeHUD();
        if (Widget)
        {
            HiddenHUDWidget = Widget;
            PreviousHUDVisibility = Widget->GetVisibility();
            UE_LOG(LogMIS, Log, TEXT("Hiding native HUD root %s during lasso."), *Widget->GetName());
        }
    }
    if (Widget && Widget->GetVisibility() != ESlateVisibility::Hidden)
        Widget->SetVisibility(ESlateVisibility::Hidden);
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
        LastResult = TEXT("Outline too small or flat; draw an area.");
        return;
    }
    if (!ViewIsUnchanged() || ViewportSize.X <= 0 || ViewportSize.Y <= 0)
    {
        LastResult = TEXT("Camera moved or viewport resized. Stop moving and redraw.");
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
    TRACE_CPUPROFILER_EVENT_SCOPE(ItemSelector_Snapshot);
    if (AFGBuildableSubsystem* Subsystem = AFGBuildableSubsystem::Get(PC))
        for (AFGBuildable* Actor : Subsystem->GetAllBuildablesRef())
            if (IsValid(Actor) && !Actor->GetIsLightweightTemporary())
            {
                const FVector Center = CenterOf(Actor);
                if (!IsCenterInRange(Center) || !BuildableEligibility.IsActorSelectable(Actor)) continue;
                FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
                Candidate.Actor = Actor;
                Candidate.Center = Center;
            }
    if (auto* Lightweight = AFGLightweightBuildableSubsystem::Get(PC->GetWorld()))
        for (const auto& Pair : Lightweight->GetAllLightweightBuildableInstances())
            for (int32 Index = 0; Index < Pair.Value.Num(); ++Index)
            {
                const auto& Data = Pair.Value[Index];
                if (!Data.IsValid()) continue;
                const FVector Center = Data.BoundingBox.IsValid
                    ? Data.Transform.TransformPosition(Data.BoundingBox.GetCenter()) : Data.Transform.GetLocation();
                if (!IsCenterInRange(Center) || !BuildableEligibility.IsTypeSelectable(Pair.Key.Get(),
                    UFGRecipe::GetDescriptorForRecipe(Data.BuiltWithRecipe))) continue;
                FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
                Candidate.LightweightClass = Pair.Key.Get();
                Candidate.LightweightIndex = Index;
                Candidate.Transform = Data.Transform;
                Candidate.Center = Center;
            }
    NextCandidate = Added = Rejected = 0;
    bScanning = true;
    bApplying = false;
    LastResult = TEXT("Counting objects in range; nothing added yet.");
}

void FMISModule::ApplyPreview()
{
    if (!bPreviewReady || !ActiveState.IsValid()) return;
    if (!ViewIsUnchanged())
    {
        CancelScan();
        LastResult = TEXT("View changed. Draw a new lasso before adding objects.");
        return;
    }
    if (StrokeFarMeters != GetFarDistance())
    {
        QueuePreviewRefresh();
        return;
    }
    const int32 Limit = ActiveState->GetMaxNumPendingDismantleActors();
    if (MIS::RemainingCapacity(Limit, ActiveState->GetNumPendingDismantleActors(false)) == 0)
    {
        FinishApply(FString::Printf(TEXT("Added 0 objects: dismantle limit (%d) already reached. Clear some selection before adding more."), Limit));
        return;
    }
    Candidates = MoveTemp(PreviewCandidates);
    NextCandidate = Added = Rejected = 0;
    bPreviewReady = false;
    bApplying = bScanning = true;
    ReleaseOverlay();
    ShowFeedback(TEXT(""), true);
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
            ? TEXT("Selection stopped: player or dismantle state unavailable. Already-added objects remain selected.")
            : TEXT("Preview stopped: view changed. Selection unchanged; draw a new lasso.");
        if (bWasApplying) FinishApply(LastResult);
        return;
    }
    const double Deadline = FPlatformTime::Seconds() +
        FMath::Clamp(BatchMilliseconds.GetValueOnGameThread(), 0.25f, 16.f) / 1000.0;
    const FVector ViewForward = ViewRotation.Vector();
    int32 Examined = 0;
    while (NextCandidate < Candidates.Num())
    {
        const int32 Limit = State->GetMaxNumPendingDismantleActors();
        if (bApplying && MIS::RemainingCapacity(Limit, State->GetNumPendingDismantleActors(false)) == 0)
        {
            const int32 AddedBeforeStop = Added;
            CancelScan();
            LastResult = FString::Printf(TEXT("Added %d objects. Dismantle limit (%d) reached; remaining lasso objects were not added."), AddedBeforeStop, Limit);
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
            LastResult = TEXT("Stopped: engine object budget is low. Partial additions remain; clear selection and save/reload before retrying.");
            UE_LOG(LogMIS, Warning, TEXT("%s"), *LastResult);
            FinishApply(LastResult);
            return;
        }

        AFGBuildable* Actor = Candidate.Actor.Get();
        if (Candidate.LightweightIndex != INDEX_NONE)
        {
            auto* Subsystem = AFGLightweightBuildableSubsystem::Get(PC->GetWorld());
            UClass* Class = Candidate.LightweightClass.Get();
            auto* Data = Subsystem && Class ? Subsystem->GetRuntimeDataForBuildableClassAndIndex(Class, Candidate.LightweightIndex) : nullptr;
            if (!Data || !Data->IsValid() || !Data->Transform.Equals(Candidate.Transform)) continue;
            bool bSpawned = false;
            auto* Temporary = Subsystem->FindOrSpawnBuildableForRuntimeData(Class, Data, Candidate.LightweightIndex, bSpawned);
            Actor = Temporary ? Temporary->Buildable.Get() : nullptr;
        }
        if (!IsApplying()) return;
        if (IsValid(Actor)) AddActor(Actor);
        if (!IsApplying()) return;
    }
    if (NextCandidate >= Candidates.Num())
    {
        bScanning = false;
        if (!bApplying)
        {
            bPreviewReady = !PreviewCandidates.IsEmpty();
            const int32 Limit = State->GetMaxNumPendingDismantleActors();
            const int32 Remaining = MIS::RemainingCapacity(Limit, State->GetNumPendingDismantleActors(false));
            const int32 Addable = FMath::Min(PreviewCandidates.Num(), Remaining);
            const int32 AfterPreview = Remaining - Addable;
            LastResult = bPreviewReady
                ? FString::Printf(TEXT("Preview: up to %d new objects. If all are added: room for %d more (dismantle limit %d). Left-click to add, or drag to redraw."), Addable, AfterPreview, Limit)
                : TEXT("No new objects in this outline and distance band.");
            Candidates.Reset();
            return;
        }
        bApplying = false;
        LastResult = FString::Printf(TEXT("Added %d objects; %d rejected by native dismantle rules/filter."), Added, Rejected);
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

void FMISModule::FinishApply(const FString& Message)
{
    const FString Result = Message;
    CloseOverlay();
    LastResult = Result;
    UE_LOG(LogMIS, Log, TEXT("%s"), *Result);
    ShowFeedback(Result);
}

void FMISModule::ShowFeedback(const FString& Message, bool bProgress)
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
                .Text_Lambda([this, FixedMessage = FText::FromString(Message), bProgress] {
                    return bProgress ? GetActivityStatus() : FixedMessage;
                }) ]
        ];
    FeedbackViewport = Viewport;
    FeedbackExpires = bProgress ? 0 : FPlatformTime::Seconds() + 6;
    Viewport->AddViewportWidgetContent(Feedback.ToSharedRef(), 101);
}

void FMISModule::AddActor(AFGBuildable* Actor)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ItemSelector_AddActor);
    UFGBuildGunStateDismantle* State = ActiveState.Get();
    if (!State || !BuildableEligibility.IsActorSelectable(Actor) || State->mPendingDismantleActors.Contains(Actor)) return;
    if (MIS::RemainingCapacity(State->GetMaxNumPendingDismantleActors(), State->GetNumPendingDismantleActors(false)) == 0) return;
    const TSubclassOf<AActor> Filter = State->GetCurrentDismantleClassFilter();
    if ((Filter && !Actor->IsA(Filter)) || !State->CanDismantleActor(Actor)) { ++Rejected; return; }
    const bool bPin = Actor->GetIsLightweightTemporary() && !Actor->GetBlockCleanupOfTemporary();
    if (bPin) Actor->SetBlockCleanupOfTemporary(true);
    const bool bWasRemoving = State->mShouldRemoveFromMultiSelect;
    State->mShouldRemoveFromMultiSelect = false;
    State->AddPendingDismantleActor(Actor);
    State->mShouldRemoveFromMultiSelect = bWasRemoving;
    MarkSelectionChanged();
    if (State->mPendingDismantleActors.Contains(Actor))
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
    if (bScanning && bApplying) LastResult = FString::Printf(TEXT("Stopped. %d objects already added remain selected."), Added);
    else if (bScanning || bPreviewReady || bRefreshPreview) LastResult = TEXT("Preview cancelled. Selection unchanged.");
    bScanning = false;
    bApplying = bPreviewReady = bRefreshPreview = false;
    Candidates.Reset();
    PreviewCandidates.Reset();
    NextCandidate = 0;
}

void FMISModule::AdjustDistance(float DeltaMeters)
{
    SetFarDistance(GetFarDistance() + DeltaMeters);
}

void FMISModule::DiscardPreview()
{
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
    LastResult = bRefreshPreview ? TEXT("Range changed; refreshing preview...") : TEXT("Range changed. Draw a lasso from this view.");
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
    return ActivityTextCache.Get(bScanning, bApplying, NextCandidate,
        Candidates.Num(), PreviewCandidates.Num(), LastResult);
}

IMPLEMENT_MODULE(FMISModule, MassItemSelector)

#undef LOCTEXT_NAMESPACE
