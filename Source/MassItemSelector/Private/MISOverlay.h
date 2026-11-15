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
    void AppendPoint(const FGeometry&, const FPointerEvent&);
    void ResetPoints();
    FMISModule* Owner = nullptr;
    TArray<FVector2D> Points;
    mutable FMISDrawingCache DrawingCache;
    bool bDrawing = false;
    bool bPendingClickToApply = false;
    FVector2D ClickStart = FVector2D::ZeroVector;
    FLinearColor Accent = FLinearColor(1.f, 0.3f, 0.01f);
};
