#include "MISOverlay.h"
#include "MISModule.h"
#include "MISSelectionRange.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Framework/Application/SlateApplication.h"
#include "FGGameUserSettings.h"

#define LOCTEXT_NAMESPACE "ItemSelector"

void SMISOverlay::Construct(const FArguments& Args)
{
    Owner = Args._Owner;
    ForceVolatile(true); 
    SetCursor(EMouseCursor::None);
    if (auto* Settings = UFGGameUserSettings::GetFGGameUserSettings())
    {
        const FVector Color = Settings->GetHologramDismantleColor();
        const double Peak = FMath::Max3(Color.X, Color.Y, Color.Z);
        if (!Color.ContainsNaN() && Peak > 0)
            Accent = FLinearColor(Color.X / FMath::Max(1.0, Peak), Color.Y / FMath::Max(1.0, Peak), Color.Z / FMath::Max(1.0, Peak));
    }
    auto Hint = [this](const TCHAR* Key, const TCHAR* Action) {
        return SNew(SVerticalBox)
            .Visibility(EVisibility::HitTestInvisible)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.8f)).Padding(FMargin(12, 8))
                [ SNew(STextBlock).ColorAndOpacity(FLinearColor::White).Text(FText::FromString(Action)) ]
            ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(Accent).Padding(FMargin(8, 2))
                [ SNew(STextBlock).ColorAndOpacity(FLinearColor::White)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)).Text(FText::FromString(Key)) ]
            ];
    };
    const FString DistanceHint = FString::Printf(TEXT("Distance +/-%gm"), Owner->GetWheelStep());
    ChildSlot
    [
    SNew(SOverlay)
    + SOverlay::Slot().HAlign(Owner->IsDistancePanelOnRight() ? HAlign_Right : HAlign_Left).VAlign(VAlign_Top).Padding(20)
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.9f))
        .Padding(12)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(LOCTEXT("DistanceLabel", "Distance (m) ")) ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SSpinBox<float>).MinValue(MIS::MinSelectionDistanceMeters).MaxValue(MIS::MaxSelectionDistanceMeters)
                    .MinSliderValue(5.f).MaxSliderValue(500.f).Delta(Owner->GetWheelStep()).MinDesiredWidth(90)
                    .Value_Lambda([this] { return Owner->GetFarDistance(); })
                    .IsEnabled_Lambda([this] { return !Owner->IsApplying() && !bDrawing; })
                    .OnValueChanged_Lambda([this](float Value) { Owner->SetFarDistance(Value); })
                ]
            ]
        ]
    ]
    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(20, 20, 20, 80)
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("NoBrush")).Padding(0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.9f)).Padding(FMargin(12, 6))
                [
                SNew(STextBlock).ColorAndOpacity(Accent)
                .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
                .Text(LOCTEXT("SelectionModeLasso", "Selection Mode: Lasso"))
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.9f)).Padding(FMargin(12, 8))
                [
                SNew(STextBlock).ColorAndOpacity(FLinearColor::White)
                .WrapTextAt(1000).Justification(ETextJustify::Center)
                .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
                .Text_Lambda([this] { return Owner->GetActivityStatus(); })
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 12, 0, 0)
            [
                SNew(SWrapBox).PreferredSize(1200).InnerSlotPadding(FVector2D(12, 8))
                + SWrapBox::Slot()
                [
                    SNew(SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
                    .IsFocusable(false)
                    .ContentPadding(0)
                    .Visibility_Lambda([this] { return Owner->HasPreview() ? EVisibility::Visible : EVisibility::Collapsed; })
                    .OnClicked_Lambda([this] { Owner->ApplyPreview(); return FReply::Handled(); })
                    [ Hint(TEXT("LMB"), TEXT("Add to dismantler")) ]
                ]
                + SWrapBox::Slot() [ Hint(TEXT("Drag LMB"), TEXT("Select items")) ]
                + SWrapBox::Slot() [ Hint(TEXT("Wheel"), *DistanceHint) ]
                + SWrapBox::Slot() [ Hint(TEXT("RMB"), TEXT("Cancel selection")) ]
                + SWrapBox::Slot()
                [
                    SNew(SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
                    .ContentPadding(0)
                    .OnClicked_Lambda([this] { Owner->CloseOverlay(); return FReply::Handled(); })
                    [ Hint(TEXT("M"), TEXT("Exit selection mode")) ]
                ]
            ]
        ]
    ]
    ];
}

FReply SMISOverlay::OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::M)
        return OnKeyDown(Geometry, Event);
    return FReply::Unhandled();
}

FReply SMISOverlay::OnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
    const FKey Key = Event.GetKey();
    if (Key == EKeys::Escape && (bDrawing || bPendingClickToApply || Owner->IsScanning() || Owner->HasPreview()))
    {
        bDrawing = false;
        bPendingClickToApply = false;
        ResetPoints();
        Owner->DiscardPreview();
        return FReply::Handled().ReleaseMouseCapture();
    }
    if (Key == EKeys::M || Key == EKeys::Escape)
    {
        Owner->CloseOverlay();
        return FReply::Handled().ReleaseMouseCapture();
    }
    return FReply::Handled();
}

void SMISOverlay::ResetPoints()
{
    Points.Reset();
    DrawingCache.InvalidateOutline();
}

void SMISOverlay::AppendPoint(const FGeometry& Geometry, const FPointerEvent& Event)
{
    FVector2D P = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
    const FVector2D Size = Geometry.GetLocalSize();
    P.X = FMath::Clamp(P.X, 0.0, Size.X);
    P.Y = FMath::Clamp(P.Y, 0.0, Size.Y);
    if (Points.IsEmpty() || FVector2D::DistSquared(Points.Last(), P) >= 4.0)
    {
        if (Points.Num() >= 4096)
        {
            TArray<FVector2D> Reduced;
            for (int32 I = 0; I < Points.Num(); I += 2) Reduced.Add(Points[I]);
            Points = MoveTemp(Reduced);
        }
        Points.Add(P);
        DrawingCache.InvalidateOutline();
    }
}

FReply SMISOverlay::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (Event.GetEffectingButton() == EKeys::RightMouseButton)
    {
        bDrawing = false;
        bPendingClickToApply = false;
        ResetPoints();
        Owner->DiscardPreview();
        return FReply::Handled().ReleaseMouseCapture();
    }
    if (Event.GetEffectingButton() == EKeys::LeftMouseButton && !Owner->IsScanning())
    {
        if (Owner->HasPreview())
        {
            bPendingClickToApply = true;
            ClickStart = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
            return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
        }
        ResetPoints();
        bDrawing = true;
        Owner->BeginStroke();
        AppendPoint(Geometry, Event);
        return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
    }
    return FReply::Handled();
}

FReply SMISOverlay::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
    const FVector2D Current = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
    if (bPendingClickToApply && FVector2D::DistSquared(Current, ClickStart) > 36.0)
    {
        bPendingClickToApply = false;
        ResetPoints();
        bDrawing = true;
        Owner->BeginStroke();
        Points.Add(ClickStart);
    }
    if (bDrawing) AppendPoint(Geometry, Event);
    return FReply::Handled();
}

FReply SMISOverlay::OnMouseWheel(const FGeometry&, const FPointerEvent& Event)
{
    if (!bDrawing && !Owner->IsApplying())
    {
        const float Step = Owner->GetWheelStep();
        Owner->AdjustDistance(Event.GetWheelDelta() * Step);
    }
    return FReply::Handled();
}

FReply SMISOverlay::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (bPendingClickToApply && Event.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        bPendingClickToApply = false;
        const FVector2D Current = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
        if (FVector2D::DistSquared(Current, ClickStart) <= 36.0) Owner->ApplyPreview();
        return FReply::Handled().ReleaseMouseCapture();
    }
    if (bDrawing && Event.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        AppendPoint(Geometry, Event);
        bDrawing = false;
        Owner->StartSelection(Points, Geometry.GetLocalSize());
        return FReply::Handled().ReleaseMouseCapture();
    }
    return FReply::Handled();
}

void SMISOverlay::OnMouseCaptureLost(const FCaptureLostEvent& Event)
{
    SCompoundWidget::OnMouseCaptureLost(Event);
    bPendingClickToApply = false;
    if (bDrawing) { bDrawing = false; ResetPoints(); }
}

int32 SMISOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
    const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 Layer,
    const FWidgetStyle& Style, bool bEnabled) const
{
    const int32 Top = SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements, Layer, Style, bEnabled);
    if (Points.Num() > 1)
    {
        const auto& Closed = DrawingCache.GetClosedOutline(Points);
        FSlateDrawElement::MakeLines(Elements, Top + 1, Geometry.ToPaintGeometry(), Closed,
            ESlateDrawEffect::None, FLinearColor::Black, true, 5.f);
        FSlateDrawElement::MakeLines(Elements, Top + 2, Geometry.ToPaintGeometry(), Closed,
            ESlateDrawEffect::None, Accent, true, 2.f);
    }
    const FVector2D Cursor = Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
    const auto& CursorGeometry = DrawingCache.GetCursorGeometry(Cursor);
    auto DrawCursorLine = [&](const TArray<FVector2D>& Line) {
        FSlateDrawElement::MakeLines(Elements, Top + 3, Geometry.ToPaintGeometry(), Line,
            ESlateDrawEffect::None, FLinearColor::Black, true, 7.f);
        FSlateDrawElement::MakeLines(Elements, Top + 4, Geometry.ToPaintGeometry(), Line,
            ESlateDrawEffect::None, Accent, true, 3.f);
    };
    DrawCursorLine(CursorGeometry.Ring);
    for (const auto& Line : CursorGeometry.Crosshairs) DrawCursorLine(Line);
    return Top + 4;
}

#undef LOCTEXT_NAMESPACE
