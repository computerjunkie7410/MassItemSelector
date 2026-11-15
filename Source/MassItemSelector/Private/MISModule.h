#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "MISSelectionGeometry.h"
#include "MISPresentationCache.h"
#include "Components/SlateWrapperTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "InputAction.h"
#include "InputMappingContext.h"

class UFGBuildGunStateDismantle;
class AFGBuildable;
class APlayerController;
class UGameViewportClient;
class SMISOverlay;
class UUserWidget;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;

class FMISModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    virtual bool SupportsDynamicReloading() override { return false; }
    void CloseOverlay();
    void BeginStroke();
    void StartSelection(const TArray<FVector2D>& Points, FVector2D LocalSize);
    void CancelScan();
    void DiscardPreview();
    void AdjustDistance(float DeltaMeters);
    void SetFarDistance(float Meters);
    float GetFarDistance() const;
    float GetWheelStep() const;
    bool IsDistancePanelOnRight() const;
    void ApplyPreview();
    bool HasPreview() const { return bPreviewReady; }
    bool IsApplying() const { return bScanning && bApplying; }
    bool IsScanning() const { return bScanning; }
    FText GetActivityStatus() const;

private:
    struct FCandidate
    {
        TWeakObjectPtr<AFGBuildable> Actor;
        TWeakObjectPtr<UClass> LightweightClass;
        int32 LightweightIndex = INDEX_NONE;
        FTransform Transform;
        FVector Center = FVector::ZeroVector;
    };
    void TickState(UFGBuildGunStateDismantle* State);
    void EndState(UFGBuildGunStateDismantle* State);
    void OpenOverlay();
    void ReleaseOverlay();
    void ProcessBatch();
    bool IsCenterInRange(const FVector& Center) const;
    bool IsCenterInsideLasso(APlayerController* PC, const FVector& Center, const FVector& ViewForward) const;
    void RefreshSelectedActorLookup(const UFGBuildGunStateDismantle* State);
    void AddActor(AFGBuildable* Actor);
    bool ViewIsUnchanged() const;
    void ReleaseTemporaryPins(bool bAll);
    void QueuePreviewRefresh();
    bool HasObjectHeadroom() const;
    void FinishApply(const FString& Message);
    void ShowFeedback(const FString& Message, bool bProgress = false);
    void RemoveFeedback();
    void HideNativeHUD();
    void RestoreNativeHUD();
    void InstallMassSelectionInput();
    void RemoveMassSelectionInput();
    void UpdateMassSelectionHints();
    void RemoveMassSelectionHints();

    TWeakObjectPtr<UFGBuildGunStateDismantle> ActiveState;
    TWeakObjectPtr<APlayerController> Player;
    TWeakObjectPtr<UGameViewportClient> OpenViewport;
    TWeakObjectPtr<UWorld> ActiveWorld;
    TSharedPtr<SMISOverlay> Overlay;
    TStrongObjectPtr<UInputAction> MassSelectionAction;
    TStrongObjectPtr<UInputMappingContext> MassSelectionContext;
    TWeakObjectPtr<UEnhancedInputComponent> MassSelectionInput;
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> MassSelectionSubsystem;
    uint32 MassSelectionBinding = 0;
    bool bMassSelectionRequested = false;
    TArray<TWeakObjectPtr<UUserWidget>> MassSelectionHintTargets;
    double NextHintScan = 0;
    TWeakObjectPtr<UUserWidget> HiddenHUDWidget;
    ESlateVisibility PreviousHUDVisibility = ESlateVisibility::SelfHitTestInvisible;
    bool bDistanceWasEdited = false;
    float LastAutomaticFar = 100.f;
    TSharedPtr<SWidget> Feedback;
    TWeakObjectPtr<UGameViewportClient> FeedbackViewport;
    double FeedbackExpires = 0;
    TArray<FCandidate> Candidates;
    TArray<FCandidate> PreviewCandidates;
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
    FString LastResult;
    mutable FMISActivityTextCache ActivityTextCache;
    FDelegateHandle TickHook;
    FDelegateHandle EndHook;
    FDelegateHandle AimHook;
    FDelegateHandle ClearHook;
    FDelegateHandle WorldCleanupHook;
};
