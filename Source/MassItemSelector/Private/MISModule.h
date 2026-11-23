#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "MISSelectionGeometry.h"
#include "MISPresentationCache.h"
#include "MISSelectionPanel.h"
#include "MISBuildableEligibility.h"
#include "UObject/StrongObjectPtr.h"
#include "InputCoreTypes.h"
#include "InputAction.h"
#include "MISInputBindings.h"

class UFGBuildGunStateDismantle;
class AFGBuildable;
class APlayerController;
class UGameViewportClient;
class SMISOverlay;
class UUserWidget;
class UEnhancedInputComponent;
class UMISSelectionObserver;
class FStructOnScope;
class IInputProcessor;

class FMISModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    virtual bool SupportsDynamicReloading() override { return false; }
    void ExitDismantleMode();
    bool HandleDismantleShortcut(const FKeyEvent& Event);
    const MIS::FDismantleBindings& GetInputBindings() const { return InputBindings; }
    void BeginStroke();
    void StartSelection(const TArray<FVector2D>& Points, FVector2D LocalSize);
    void DiscardPreview();
    void AdjustDistance(float DeltaMeters);
    void SetFarDistance(float Meters);
    float GetFarDistance() const;
    float GetWheelStep() const;
    bool IsDistancePanelOnRight() const;
    bool IsApplying() const { return bScanning && bApplying; }
    bool IsScanning() const { return bScanning; }
    FText GetActivityStatus() const;
    const TArray<TSharedPtr<FMISSelectionType>>& GetSelectionTypes() const { return SelectionTypes; }
    void SetSelectionTypeEnabled(UClass* Class, bool bEnabled);
    bool IsSelectionInteractive() const { return bSelectionInteractive; }
    bool IsSelectionBusy() const { return !SelectionOperations.IsEmpty() || IsApplying(); }
    FText GetSelectionStatus() const;

private:
    friend class FMISLassoInputProcessor;
#if WITH_DEV_AUTOMATION_TESTS
    friend class FMISLassoInputTest;
    friend class FMISNativeShortcutsTest;
    friend class FMISSelectionLookupTest;
    friend class FMISSelectionModeTest;
    friend class FMISScanBudgetTest;
#endif
    struct FCandidate
    {
        TWeakObjectPtr<AFGBuildable> Actor;
        TWeakObjectPtr<UClass> LightweightClass;
        int32 LightweightIndex = INDEX_NONE;
        FTransform Transform;
        FVector Center = FVector::ZeroVector;
    };
    struct FLightweightScan
    {
        TWeakObjectPtr<UClass> Class;
        int32 Count = 0;
    };
    void TickState(UFGBuildGunStateDismantle* State);
    void EndState(UFGBuildGunStateDismantle* State);
    void OpenOverlay();
    void CloseOverlay();
    void ReleaseOverlay();
    void ReleaseLasso();
    void SetLassoInputCaptured(bool bCapture);
    TSharedRef<IInputProcessor> CreateLassoInputProcessor();
    void ProcessBatch();
    void CancelScan();
    void ApplyPreview();
    bool GatherCandidates(double Deadline);
    bool IsCenterInRange(const FVector& Center) const;
    bool IsCenterInsideLasso(APlayerController* PC, const FVector& Center, const FVector& ViewForward) const;
    void RefreshSelectedActorLookup(const UFGBuildGunStateDismantle* State);
    void UpdateSelectedActorLookup(const UFGBuildGunStateDismantle* State, AActor* Actor, int32 PreviousCount);
    void AddActor(AFGBuildable* Actor);
    bool ViewIsUnchanged() const;
    bool IsDefaultDismantleMode() const;
    void ReleaseTemporaryPins(bool bAll);
    void QueuePreviewRefresh();
    bool HasObjectHeadroom() const;
    void FinishPreview();
    void FinishApply(const FText& Message);
    void ShowFeedback(const FText& Message, bool bProgress = false);
    void RemoveFeedback();
    void InstallDismantleInput();
    void RemoveDismantleInput();
    void UpdateDismantleHints();
    void RemoveDismantleHints();
    void TickSelectionPanel();
    void MarkSelectionChanged();
    void CheckSelectionChanges();
    void RefreshSelectionPanel();
    void ResetSelectionPanel();
    void ReleaseSelectionPanel();
    void ToggleSelectionInteraction();
    void CloseSelectionInteraction();
    bool QueueSelectionInteraction(FKey Key, const FModifierKeysState& Modifiers, bool bRepeat);
    void ProcessSelectionOperations();
    FMISSelectionObject CaptureSelectionObject(AActor* Actor) const;
    bool IsSelectionObjectValid(const FMISSelectionObject& Object) const;
    void UnbindSelectionObserver();
    int32 GetPanelSelectedCount() const;

    TWeakObjectPtr<UFGBuildGunStateDismantle> ActiveState;
    TWeakObjectPtr<APlayerController> Player;
    TWeakObjectPtr<UGameViewportClient> OpenViewport;
    TWeakObjectPtr<UWorld> ActiveWorld;
    TSharedPtr<SMISOverlay> Overlay;
    TSharedPtr<SMISSelectionPanel> SelectionPanel;
    TWeakObjectPtr<UGameViewportClient> SelectionViewport;
    TStrongObjectPtr<UMISSelectionObserver> SelectionObserver;
    TArray<TSharedPtr<FMISSelectionType>> SelectionTypes;
    TArray<TSharedPtr<FMISSelectionType>> UpdatedSelectionTypes;
    TMap<TWeakObjectPtr<UClass>, TSharedPtr<FMISSelectionType>> SelectionTypeLookup;
    TArray<FMISSelectionSnapshotEntry> NativeSelectionSnapshot;
    TArray<FMISSelectionSnapshotEntry> UpdatedSelectionSnapshot;
    MIS::FBuildableEligibilityCache BuildableEligibility;
    TMap<FMISSelectionKey, FMISSelectionObject> ExcludedSelection;
    TArray<FMISSelectionObject> SelectionOperations;
    int32 NextSelectionOperation = 0;
    int32 SelectionOperationChanged = 0;
    bool bRestoreSelection = false;
    bool bInsideSelectionMutation = false;
    bool bSelectionDirty = true;
    bool bSelectionSnapshotDirty = true;
    bool bSelectedActorLookupDirty = true;
    bool bTemporaryPinsDirty = true;
    bool bSelectionInteractive = false;
    bool bSelectionPreviousCursor = false;
    bool bSelectionRequested = false;
    double NextSelectionCheck = 0;
    uint16 SelectionTextRevision = 0;
    FText SelectionMessage;
    uint32 SelectionPanelBinding = 0;
    MIS::FDismantleBindings InputBindings;
    TStrongObjectPtr<UInputAction> LassoAction;
    TArray<uint32> LassoBindings;
    TWeakObjectPtr<UEnhancedInputComponent> DismantleInput;
    bool bCycleModeRequested = false;
    bool bExitDismantleRequested = false;
    bool bOverlaySuspended = false;
    FKey LassoKey;
    TArray<FKey> LassoModifiers;
    bool bLassoKeyDown = false;
    bool bLassoKeyWasDown = false;
    bool bLassoInputCaptured = false;
    bool bReleaseLassoRequested = false;
    bool bApplyLassoRequested = false;
    bool bClearLassoRequested = false;
    float PendingLassoWheel = 0;
    TSharedPtr<IInputProcessor> LassoInputProcessor;
    TWeakObjectPtr<UClass> LastDismantleMode;
    TMap<TWeakObjectPtr<UUserWidget>, TSharedPtr<FStructOnScope>> DismantleHintTargets;
    double NextHintScan = 0;
    double NextHintUpdate = 0;
    TWeakObjectPtr<UUserWidget> HintHUDRoot;
    bool bDistanceWasEdited = false;
    float LastAutomaticFar = 100.f;
    TSharedPtr<SWidget> Feedback;
    TWeakObjectPtr<UGameViewportClient> FeedbackViewport;
    double FeedbackExpires = 0;
    TArray<FCandidate> Candidates;
    TArray<FCandidate> PreviewCandidates;
    TArray<FLightweightScan> LightweightScans;
    int32 BuildableCount = 0;
    int32 NextBuildable = 0;
    int32 NextLightweightClass = 0;
    int32 NextLightweightInstance = 0;
    bool bGatheringCandidates = false;
    TArray<MIS::FPoint> Polygon;
    TArray<TWeakObjectPtr<AFGBuildable>> PinnedTemporaries;
    TSet<TWeakObjectPtr<AActor>> SelectedActorLookup;
    FVector2D StrokeSize = FVector2D::ZeroVector;
    FVector2D PolygonMin = FVector2D::ZeroVector;
    FVector2D PolygonMax = FVector2D::ZeroVector;
    FIntPoint ViewportSize = FIntPoint::ZeroValue;
    FVector ViewLocation = FVector::ZeroVector;
    FRotator ViewRotation = FRotator::ZeroRotator;
    float ViewFOV = 0;
    int32 NextCandidate = 0;
    int32 Added = 0;
    int32 Rejected = 0;
    bool bScanning = false;
    bool bApplying = false;
    bool bPreviewReady = false;
    bool bRefreshPreview = false;
    double RefreshPreviewAt = 0;
    float StrokeFarMeters = 100;
    bool bPreviousCursor = false;
    FText LastResult;
    mutable FMISActivityTextCache ActivityTextCache;
    FDelegateHandle PrimaryFireHook;
    FDelegateHandle TickHook;
    FDelegateHandle EndHook;
    FDelegateHandle AimHook;
    FDelegateHandle ClearHook;
    FDelegateHandle WorldCleanupHook;
};
