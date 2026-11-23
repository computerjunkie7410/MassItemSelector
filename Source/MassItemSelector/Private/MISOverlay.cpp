#include "MISOverlay.h"
#include "MISModule.h"
#include "MISSelectionRange.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SOverlay.h"
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
    ChildSlot
    [
        SNew(SOverlay)
        + SOverlay::Slot().HAlign(Owner->IsDistancePanelOnRight() ? HAlign_Right : HAlign_Left).VAlign(VAlign_Top).Padding(20)
        [
            SAssignNew(DistancePanel, SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.9f))
            .Padding(12)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(LOCTEXT("DistanceLabel", "Distance (m) ")) ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SSpinBox<float>).MinValue(MIS::MinSelectionDistanceMeters).MaxValue(MIS::MaxSelectionDistanceMeters)
                    .MinSliderValue(MIS::MinSelectionDistanceMeters).MaxSliderValue(MIS::MaxSelectionDistanceMeters).Delta(Owner->GetWheelStep()).MinDesiredWidth(90)
                    .Value_Lambda([this] { return Owner->GetFarDistance(); })
                    .IsEnabled_Lambda([this] { return !Owner->IsApplying() && !bDrawing; })
                    .OnValueChanged_Lambda([this](float Value) { Owner->SetFarDistance(Value); })
                ]
            ]
        ]
    ];
}

bool SMISOverlay::IsOverDistancePanel(const FPointerEvent& Event) const
{
    return DistancePanel.IsValid() && DistancePanel->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition());
}

FReply SMISOverlay::OnPreviewKeyDown(const FGeometry&, const FKeyEvent& Event)
{
    return Owner->HandleDismantleShortcut(Event) ? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled();
}

FReply SMISOverlay::OnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
    if (Owner->GetInputBindings().HideHUD.Matches(Event.GetKey(), Event.GetModifierKeys())) return FReply::Unhandled();
    Owner->HandleDismantleShortcut(Event);
    return FReply::Handled();
}

void SMISOverlay::ClearSelection()
{
    bDrawing = false;
    ResetPoints();
    Owner->DiscardPreview();
}

bool SMISOverlay::FinishStroke()
{
    TArray<MIS::FPoint> Outline;
    Outline.Reserve(Points.Num());
    for (const auto& Point : Points) Outline.Add({ Point.X, Point.Y });
    if (!MIS::IsUsable({ Outline.GetData(), static_cast<size_t>(Outline.Num()) })) return false;
    if (bDrawing || bStrokeInterrupted)
    {
        bDrawing = false;
        bStrokeInterrupted = false;
        Owner->StartSelection(Points, GetCachedGeometry().GetLocalSize());
    }
    return true;
}

void SMISOverlay::ResumeInput()
{
    if (!bStrokeInterrupted) return;
    bStrokeInterrupted = false;
    Owner->StartSelection(Points, GetCachedGeometry().GetLocalSize());
}

void SMISOverlay::ResetPoints()
{
    bStrokeInterrupted = false;
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
    if (Owner->GetInputBindings().HideHUD.Matches(Event.GetEffectingButton(), Event.GetModifierKeys())) return FReply::Unhandled();
    if (!bDrawingEnabled) return FReply::Handled();
    if (Owner->GetInputBindings().Secondary.Matches(Event.GetEffectingButton(), Event.GetModifierKeys()))
    {
        if (HasOutline()) ClearSelection();
        else Owner->ExitDismantleMode();
        return FReply::Handled().ReleaseMouseCapture();
    }
    if (Owner->GetInputBindings().Primary.Matches(Event.GetEffectingButton(), Event.GetModifierKeys()) && !Owner->IsScanning())
    {
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
    const auto& Primary = Owner->GetInputBindings().Primary;
    if (!bDrawing && bDrawingEnabled && Event.IsMouseButtonDown(Primary.Key) &&
        MIS::AreModifiersDown(Event.GetModifierKeys(), Primary.Modifiers) && !Owner->IsScanning())
    {
        ResetPoints();
        bDrawing = true;
        Owner->BeginStroke();
    }
    if (bDrawing)
    {
        AppendPoint(Geometry, Event);
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }
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
    if (bDrawing && Event.GetEffectingButton() == Owner->GetInputBindings().Primary.Key)
    {
        AppendPoint(Geometry, Event);
        bDrawing = false;
        Owner->StartSelection(Points, Geometry.GetLocalSize());
        if (Owner->IsScanning()) SetDrawingEnabled(false);
        return FReply::Handled().ReleaseMouseCapture();
    }
    return FReply::Handled();
}

void SMISOverlay::OnMouseCaptureLost(const FCaptureLostEvent& Event)
{
    SCompoundWidget::OnMouseCaptureLost(Event);
    bStrokeInterrupted |= bDrawing;
    bDrawing = false;
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
    if (!bDrawingEnabled) return Top + 2;
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
