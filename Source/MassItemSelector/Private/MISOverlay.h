#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "MISPresentationCache.h"

class FMISModule;

class SMISOverlay : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMISOverlay) {}
        SLATE_ARGUMENT(FMISModule*, Owner)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    bool IsDrawing() const { return bDrawing; }
    bool IsOverDistancePanel(const FPointerEvent& Event) const;
    bool HasOutline() const { return !Points.IsEmpty(); }
    bool IsDrawingEnabled() const { return bDrawingEnabled; }
    bool FinishStroke();
    void SetDrawingEnabled(bool bEnabled)
    {
        bDrawingEnabled = bEnabled;
        SetCursor(bEnabled ? EMouseCursor::None : EMouseCursor::Default);
    }
    void ClearSelection();
    void ResumeInput();
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent&) override;
    virtual FReply OnPreviewKeyDown(const FGeometry&, const FKeyEvent&) override;
    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
    virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent&) override;
    virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent&) override;
    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override;
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
        FSlateWindowElementList&, int32, const FWidgetStyle&, bool) const override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FMISLassoInputTest;
#endif
    void AppendPoint(const FGeometry&, const FPointerEvent&);
    void ResetPoints();
    FMISModule* Owner = nullptr;
    TSharedPtr<SWidget> DistancePanel;
    TArray<FVector2D> Points;
    mutable FMISDrawingCache DrawingCache;
    bool bDrawing = false;
    bool bStrokeInterrupted = false;
    bool bDrawingEnabled = true;
    FLinearColor Accent = FLinearColor(1.f, 0.3f, 0.01f);
};
