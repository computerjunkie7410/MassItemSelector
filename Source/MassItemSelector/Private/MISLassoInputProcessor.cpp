#include "MISModule.h"
#include "MISOverlay.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"

class FMISLassoInputProcessor : public IInputProcessor
{
public:
    explicit FMISLassoInputProcessor(FMISModule& InOwner) : Owner(InOwner) {}
    virtual void Tick(float, FSlateApplication& Slate, TSharedRef<ICursor>) override
    {
        if (!Slate.IsActive())
        {
            ConsumedCloseKey = EKeys::Invalid;
            Owner.bLassoKeyDown = false;
            Owner.bReleaseLassoRequested = true;
        }
    }
    virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Event) override
    {
        if (Event.GetKey() == Owner.LassoKey || Owner.LassoModifiers.Contains(Event.GetKey()))
        {
            Owner.bLassoKeyDown = false;
            Owner.bReleaseLassoRequested = true;
        }
        if (Event.GetKey() == ConsumedCloseKey)
        {
            ConsumedCloseKey = EKeys::Invalid;
            return true;
        }
        if (IsDrawingActive() && Event.GetKey() == Owner.InputBindings.Primary.Key)
            return HandleMouseButtonUpEvent(FSlateApplication::Get(), MakePointerEvent(Event));
        return false;
    }
    virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
    {
        if (Event.GetKey() == ConsumedCloseKey) return true;
        if (Owner.bSelectionInteractive && Event.GetKey() == EKeys::Escape)
        {
            Owner.CloseSelectionInteraction();
            return true;
        }
        if (Owner.QueueSelectionInteraction(Event.GetKey(), Event.GetModifierKeys(), Event.IsRepeat())) return true;
        if (Event.GetKey() == Owner.LassoKey && !Event.IsRepeat() &&
            MIS::AreModifiersDown(Event.GetModifierKeys(), Owner.LassoModifiers)) Owner.bLassoKeyDown = true;
        if ((IsDrawingActive() || IsPreviewActive() || Owner.bSelectionInteractive) &&
            Owner.HandleDismantleShortcut(Event))
        {
            if (Owner.bClearLassoRequested) ConsumedCloseKey = Event.GetKey();
            return true;
        }
        if (Event.IsRepeat() || (!IsDrawingActive() && !IsPreviewActive())) return false;
        if (!Owner.InputBindings.Primary.Matches(Event.GetKey(), Event.GetModifierKeys()) &&
            !Owner.InputBindings.Secondary.Matches(Event.GetKey(), Event.GetModifierKeys())) return false;
        return HandleMouseButtonDownEvent(FSlateApplication::Get(), MakePointerEvent(Event));
    }
    virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& Event) override
    {
        if (Owner.bSelectionInteractive && !Owner.SelectionPanel->IsUnderPointer(Event))
        {
            Owner.CloseSelectionInteraction();
            return true;
        }
        if (Owner.QueueSelectionInteraction(Event.GetEffectingButton(), Event.GetModifierKeys(), false)) return true;
        const auto* PC = Owner.Player.Get();
        if (Event.GetEffectingButton() == Owner.LassoKey &&
            MIS::AreModifiersDown(Event.GetModifierKeys(), Owner.LassoModifiers) &&
            Owner.IsDefaultDismantleMode() && PC && !PC->IsPaused() &&
            (!PC->bShowMouseCursor || Owner.Overlay.IsValid()))
        {
            Owner.bLassoKeyDown = true;
            return true;
        }
        if ((IsDrawingActive() || IsPreviewActive() || Owner.bSelectionInteractive) &&
            Owner.HandleDismantleShortcut(FKeyEvent(Event.GetEffectingButton(), Event.GetModifierKeys(),
                Event.GetUserIndex(), false, 0, 0))) return true;
        if (Owner.InputBindings.HideHUD.Matches(Event.GetEffectingButton(), Event.GetModifierKeys())) return false;
        if (IsDrawingActive())
        {
            if (Owner.Overlay->IsOverDistancePanel(Event) && !Owner.Overlay->IsDrawing())
            {
                bUsingDistanceControl = true;
                return false;
            }
            Owner.Overlay->OnMouseButtonDown(Owner.Overlay->GetCachedGeometry(), Event);
            return true;
        }
        if (!IsPreviewActive() || Owner.Overlay->IsOverDistancePanel(Event)) return false;
        if (Owner.InputBindings.Primary.Matches(Event.GetEffectingButton(), Event.GetModifierKeys())) Owner.bApplyLassoRequested = true;
        else if (Owner.InputBindings.Secondary.Matches(Event.GetEffectingButton(), Event.GetModifierKeys())) Owner.bClearLassoRequested = true;
        else return false;
        return true;
    }
    virtual bool HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent& Event) override
    {
        if (Event.GetEffectingButton() == Owner.LassoKey &&
            (Owner.bLassoKeyDown || Owner.bLassoInputCaptured))
        {
            Owner.bLassoKeyDown = false;
            Owner.bReleaseLassoRequested = true;
            return true;
        }
        if (bUsingDistanceControl)
        {
            bUsingDistanceControl = false;
            return false;
        }
        if (!IsDrawingActive()) return false;
        Owner.Overlay->OnMouseButtonUp(Owner.Overlay->GetCachedGeometry(), Event);
        return true;
    }
    virtual bool HandleMouseMoveEvent(FSlateApplication&, const FPointerEvent& Event) override
    {
        if (!IsDrawingActive() || bUsingDistanceControl) return false;
        if (Owner.InputBindings.Primary.Key == Owner.LassoKey) return true;
        Owner.Overlay->OnMouseMove(Owner.Overlay->GetCachedGeometry(), Event);
        return true;
    }
    virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication&, const FPointerEvent& Event, const FPointerEvent*) override
    {
        if (IsDrawingActive() && !bUsingDistanceControl && !Owner.Overlay->IsOverDistancePanel(Event))
        {
            Owner.Overlay->OnMouseWheel(Owner.Overlay->GetCachedGeometry(), Event);
            return true;
        }
        if (!IsPreviewActive() || Owner.Overlay->IsOverDistancePanel(Event)) return false;
        Owner.PendingLassoWheel += Event.GetWheelDelta();
        return true;
    }
private:
    static FPointerEvent MakePointerEvent(const FKeyEvent& Event)
    {
        const auto& Slate = FSlateApplication::Get();
        return FPointerEvent(Event.GetUserIndex(), Slate.GetCursorPos(), Slate.GetLastCursorPos(),
            TSet<FKey>{ Event.GetKey() }, Event.GetKey(), 0.f, Event.GetModifierKeys());
    }
    bool IsDrawingActive() const
    {
        return Owner.Overlay.IsValid() && Owner.bLassoInputCaptured && Owner.Overlay->IsDrawingEnabled() &&
            !Owner.bOverlaySuspended;
    }
    bool IsPreviewActive() const
    {
        const auto* PC = Owner.Player.Get();
        return Owner.Overlay.IsValid() && !Owner.Overlay->IsDrawingEnabled() && PC && !PC->IsPaused() &&
            (!Owner.bLassoKeyDown || Owner.bLassoKeyWasDown);
    }
    bool bUsingDistanceControl = false;
    FKey ConsumedCloseKey;
    FMISModule& Owner;
};

TSharedRef<IInputProcessor> FMISModule::CreateLassoInputProcessor()
{
    return MakeShared<FMISLassoInputProcessor>(*this);
}

