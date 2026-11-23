#include "MISSelectionPanel.h"
#include "MISModule.h"
#include "MISConfiguration.h"
#include "MISSelectionObserver.h"
#include "MISSelectionNames.h"
#include "MISBuildableEligibility.h"
#include "FGRecipe.h"
#include "FGCustomizationRecipe.h"
#include "FGFactoryColoringTypes.h"
#include "Buildables/FGBuildable.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "FGLightweightBuildableSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Resources/FGItemDescriptor.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ItemSelector"

static FText GetSelectionTypeName(const AFGBuildable* Buildable)
{
    FText Name = UFGItemDescriptor::GetItemName(Buildable->GetBuiltWithDescriptor());
    if (Name.IsEmpty()) Name = Buildable->mDisplayName;
    if (Name.IsEmpty()) Name = FText::FromString(Buildable->GetClass()->GetName());

    const auto Recipe = Buildable->GetBuiltWithRecipe();
    const auto MaterialRecipe = Recipe ? UFGRecipe::GetMaterialCustomizationRecipe(Recipe) : nullptr;
    const FText Material = MaterialRecipe
        ? UFGItemDescriptor::GetItemName(UFGCustomizationRecipe::GetCustomizationDescriptor(MaterialRecipe))
        : FText::GetEmpty();
    return MISFormatSelectionTypeName(Name, Material);
}

void UMISSelectionObserver::SelectionChanged()
{
    if (OnChanged) OnChanged();
}

void SMISSelectionPanel::Construct(const FArguments& Args)
{
    Owner = Args._Owner;
    RowFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SAssignNew(Body, SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.92f)).Padding(12)
            [
                SNew(SBox).WidthOverride_Lambda([this] { return PanelWidth; }).MaxDesiredHeight(520)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(LOCTEXT("SelectionTitle", "Selected Buildings"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16)) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
                    [ SNew(STextBlock).Text(LOCTEXT("SelectionHelp", "Uncheck a type to exclude it from selection"))
                        .AutoWrapText(true) ]
                    + SVerticalBox::Slot().MaxHeight(350)
                    [
                        SAssignNew(List, SListView<TSharedPtr<FMISSelectionType>>)
                        .ListItemsSource(&Owner->GetSelectionTypes())
                        .SelectionMode(ESelectionMode::None)
                        .OnGenerateRow(this, &SMISSelectionPanel::MakeRow)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
                    [ SNew(STextBlock).Text_Lambda([this] { return Owner->GetSelectionStatus(); }).AutoWrapText(true)
                        .Visibility_Lambda([this] { return Owner->GetSelectionStatus().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }) ]
                ]
            ]
        ]
    ];
}

TSharedRef<ITableRow> SMISSelectionPanel::MakeRow(TSharedPtr<FMISSelectionType> Item, const TSharedRef<STableViewBase>& Table)
{
    return SNew(STableRow<TSharedPtr<FMISSelectionType>>, Table)
    [
        SNew(SCheckBox)
        .IsEnabled_Lambda([this] { return Owner->IsSelectionInteractive() && !Owner->IsSelectionBusy(); })
        .IsChecked_Lambda([Item] {
            return Item->Selected == 0 ? ECheckBoxState::Unchecked
                : Item->Excluded > 0 ? ECheckBoxState::Undetermined : ECheckBoxState::Checked;
        })
        .OnCheckStateChanged_Lambda([this, Item](ECheckBoxState State) {
            Owner->SetSelectionTypeEnabled(Item->Class.Get(), State != ECheckBoxState::Unchecked);
        })
        [
            SNew(STextBlock).AutoWrapText(false)
            .Font_Lambda([this] { return RowFont; })
            .Text_Lambda([Item] { return Item->Label; })
        ]
    ];
}

void SMISSelectionPanel::Refresh(bool bRowsChanged)
{
    constexpr float RowControlsWidth = 64.f;
    const auto FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const auto Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
    const bool bFontChanged = !(RowFont == Font);
    RowFont = Font;
    const uint16 TextRevision = FTextLocalizationManager::Get().GetTextRevision();
    PanelWidth = 320.f;
    for (const auto& Item : Owner->GetSelectionTypes())
    {
        const int32 Count = Item->Selected + Item->Excluded;
        const bool bLabelChanged = Item->CachedCount != Count ||
            !Item->CachedName.EqualTo(Item->Name) || Item->TextRevision != TextRevision;
        if (bLabelChanged)
        {
            Item->Label = FText::Format(LOCTEXT("SelectionTypeSimpleCount", "{0} - {1}"), Item->Name, Count);
            Item->CachedName = Item->Name;
            Item->CachedCount = Count;
            Item->TextRevision = TextRevision;
        }
        if (bLabelChanged || bFontChanged)
            Item->LabelWidth = static_cast<float>(FontMeasure->Measure(Item->Label, RowFont).X);
        PanelWidth = FMath::Max(PanelWidth, Item->LabelWidth + RowControlsWidth);
    }
    if (bRowsChanged) List->RequestListRefresh();
}

bool SMISSelectionPanel::IsUnderPointer(const FPointerEvent& Event) const
{
    return Body->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition());
}

void SMISSelectionPanel::SetSide(bool bRight)
{
    ChildSlot.HAlign(bRight ? HAlign_Right : HAlign_Left).VAlign(VAlign_Center).Padding(20);
}

FReply SMISSelectionPanel::OnPreviewKeyDown(const FGeometry&, const FKeyEvent& Event)
{
    return Owner->HandleDismantleShortcut(Event) ? FReply::Handled() : FReply::Unhandled();
}

FReply SMISSelectionPanel::OnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
    if (Owner->GetInputBindings().HideHUD.Matches(Event.GetKey(), Event.GetModifierKeys())) return FReply::Unhandled();
    Owner->HandleDismantleShortcut(Event);
    return FReply::Handled();
}

FReply SMISSelectionPanel::OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
    return Owner->GetInputBindings().HideHUD.Matches(Event.GetEffectingButton(), Event.GetModifierKeys())
        ? FReply::Unhandled() : FReply::Handled();
}
FReply SMISSelectionPanel::OnMouseButtonUp(const FGeometry&, const FPointerEvent&) { return FReply::Handled(); }

FMISSelectionObject FMISModule::CaptureSelectionObject(AActor* Actor) const
{
    FMISSelectionObject Object;
    Object.Actor = Actor;
    Object.Class = Actor->GetClass();
    Object.Transform = Actor->GetActorTransform();
    if (const auto* Buildable = Cast<AFGBuildable>(Actor); Buildable && Buildable->GetIsLightweightTemporary())
    {
        Object.LightweightIndex = Buildable->GetRuntimeDataIndex();
        if (const auto* PC = Player.Get())
            if (auto* Subsystem = AFGLightweightBuildableSubsystem::Get(PC->GetWorld()))
                if (auto* Data = Subsystem->GetRuntimeDataForBuildableClassAndIndex(Object.Class.Get(), Object.LightweightIndex))
                    Object.Transform = Data->Transform;
    }
    return Object;
}

bool FMISModule::IsSelectionObjectValid(const FMISSelectionObject& Object) const
{
    if (Object.LightweightIndex == INDEX_NONE) return BuildableEligibility.IsActorSelectable(Object.Actor.Get());
    if (!Object.Class.IsValid()) return false;
    const auto* PC = Player.Get();
    auto* Subsystem = PC ? AFGLightweightBuildableSubsystem::Get(PC->GetWorld()) : nullptr;
    const auto* Data = Subsystem
        ? Subsystem->GetRuntimeDataForBuildableClassAndIndex(Object.Class.Get(), Object.LightweightIndex) : nullptr;
    return Data && Data->IsValid() && Data->Transform.Equals(Object.Transform) &&
        BuildableEligibility.IsTypeSelectable(Object.Class.Get(), UFGRecipe::GetDescriptorForRecipe(Data->BuiltWithRecipe));
}

void FMISModule::UnbindSelectionObserver()
{
    if (!SelectionObserver.IsValid()) return;
    if (auto* State = ActiveState.Get())
        State->OnPendingDismantleActorsChanged.RemoveDynamic(SelectionObserver.Get(), &UMISSelectionObserver::SelectionChanged);
    SelectionObserver->OnChanged = nullptr;
    SelectionObserver.Reset();
}

void FMISModule::ReleaseSelectionPanel()
{
    CloseSelectionInteraction();
    if (SelectionPanel.IsValid() && SelectionViewport.IsValid())
        SelectionViewport->RemoveViewportWidgetContent(SelectionPanel.ToSharedRef());
    SelectionPanel.Reset();
    SelectionViewport.Reset();
}

void FMISModule::ResetSelectionPanel()
{
    ReleaseSelectionPanel();
    SelectionTypes.Reset();
    UpdatedSelectionTypes.Reset();
    SelectionTypeLookup.Reset();
    NativeSelectionSnapshot.Reset();
    UpdatedSelectionSnapshot.Reset();
    ExcludedSelection.Reset();
    SelectionOperations.Reset();
    NextSelectionOperation = 0;
    SelectionMessage = FText::GetEmpty();
    bSelectionDirty = true;
    MarkSelectionChanged();
    bSelectionRequested = false;
    NextSelectionCheck = 0;
}

void FMISModule::MarkSelectionChanged()
{
    bSelectionSnapshotDirty = true;
    bSelectedActorLookupDirty = true;
    bTemporaryPinsDirty = true;
}

void FMISModule::CheckSelectionChanges()
{
    auto* State = ActiveState.Get();
    if (!State) return;
    const auto Pending = State->GetPendingDismantleActors();
    UpdatedSelectionSnapshot.Reset(Pending.Num());
    bool bChanged = false;
    for (AActor* Actor : Pending)
    {
        if (!IsValid(Actor)) continue;
        auto& Entry = UpdatedSelectionSnapshot.AddDefaulted_GetRef();
        Entry.Object = CaptureSelectionObject(Actor);
        Entry.bEligible = BuildableEligibility.IsActorSelectable(Actor);
        const int32 Index = UpdatedSelectionSnapshot.Num() - 1;
        if (!NativeSelectionSnapshot.IsValidIndex(Index))
        {
            bChanged = true;
            continue;
        }
        const auto& Previous = NativeSelectionSnapshot[Index];
        bChanged |= !Entry.Matches(Previous);
    }
    bChanged |= UpdatedSelectionSnapshot.Num() != NativeSelectionSnapshot.Num();
    Swap(NativeSelectionSnapshot, UpdatedSelectionSnapshot);
    if (bChanged)
    {
        bSelectionDirty = true;
        bSelectedActorLookupDirty = true;
        bTemporaryPinsDirty = true;
    }
    if (!bSelectionDirty)
    {
        for (const auto& Pair : ExcludedSelection)
        {
            if (!IsSelectionObjectValid(Pair.Value))
            {
                bSelectionDirty = true;
                break;
            }
        }
    }
}

void FMISModule::TickSelectionPanel()
{
    auto* PC = Player.Get();
    if (!PC || !FSlateApplication::IsInitialized()) return;
    if (!IsDefaultDismantleMode())
    {
        CloseSelectionInteraction();
        bSelectionRequested = false;
        if (SelectionPanel.IsValid()) SelectionPanel->SetVisibility(EVisibility::Collapsed);
        ReleaseTemporaryPins(false);
        return;
    }
    if (bSelectionInteractive && (!FSlateApplication::Get().IsActive() || PC->IsPaused())) CloseSelectionInteraction();
    if (!PC->IsPaused()) ProcessSelectionOperations();
    const double Now = FPlatformTime::Seconds();
    const bool bCheckDue = Now >= NextSelectionCheck;
    const bool bRefreshPanel = !IsSelectionBusy() || bCheckDue;
    if (bRefreshPanel && (bSelectionSnapshotDirty || bCheckDue))
    {
        bSelectionSnapshotDirty = false;
        CheckSelectionChanges();
        NextSelectionCheck = Now + 0.25;
        const uint16 TextRevision = FTextLocalizationManager::Get().GetTextRevision();
        bSelectionDirty |= SelectionTextRevision != TextRevision;
        SelectionTextRevision = TextRevision;
    }
    if (bRefreshPanel && bSelectionDirty)
    {
        bSelectionDirty = false;
        RefreshSelectionPanel();
    }
    else if (bCheckDue && SelectionPanel.IsValid())
    {
        SelectionPanel->SetSide(UMISConfigurationLibrary::GetSelectionPanelSide(PC) == EMISDistancePanelSide::Right);
        SelectionPanel->Refresh(false);
    }
    ReleaseTemporaryPins(false);
    if (bSelectionRequested)
    {
        bSelectionRequested = false;
        ToggleSelectionInteraction();
    }
    if (SelectionPanel.IsValid())
        SelectionPanel->SetVisibility(Overlay.IsValid() || PC->IsPaused() ||
            (!bSelectionInteractive && GetPanelSelectedCount() <= 1) ? EVisibility::Collapsed
            : bSelectionInteractive ? EVisibility::Visible : EVisibility::HitTestInvisible);
}

void FMISModule::RefreshSelectionPanel()
{
    auto* PC = Player.Get();
    if (!ActiveState.IsValid() || !PC) return;
    for (auto It = ExcludedSelection.CreateIterator(); It; ++It)
        if (!IsSelectionObjectValid(It.Value())) It.RemoveCurrent();
    for (auto& Pair : SelectionTypeLookup)
    {
        Pair.Value->Selected = 0;
        Pair.Value->Excluded = 0;
    }
    auto FindGroup = [this](UClass* Class) {
        auto& Row = SelectionTypeLookup.FindOrAdd(Class);
        if (!Row.IsValid())
        {
            Row = MakeShared<FMISSelectionType>();
            Row->Class = Class;
            Row->Name = FText::FromString(Class->GetName());
        }
        return Row;
    };
    for (const auto& Entry : NativeSelectionSnapshot)
    {
        AActor* Actor = Entry.Object.Actor.Get();
        if (!Entry.bEligible || !IsValid(Actor)) continue;
        const auto& Object = Entry.Object;
        const auto Key = Object.GetKey();
        if (const auto* Excluded = ExcludedSelection.Find(Key); Excluded && Excluded->SameObject(Object))
            ExcludedSelection.Remove(Key);
        auto Row = FindGroup(Actor->GetClass());
        ++Row->Selected;
        if (Row->Selected != 1) continue;
        const auto* Buildable = CastChecked<AFGBuildable>(Actor);
        const auto Recipe = Buildable->GetBuiltWithRecipe();
        const auto Descriptor = Buildable->GetBuiltWithDescriptor();
        if (Row->NameRecipe.Get() != Recipe.Get() || Row->NameDescriptor.Get() != Descriptor.Get() ||
            Row->NameTextRevision != SelectionTextRevision)
        {
            Row->Name = GetSelectionTypeName(Buildable);
            Row->NameRecipe = Recipe.Get();
            Row->NameDescriptor = Descriptor.Get();
            Row->NameTextRevision = SelectionTextRevision;
        }
    }
    for (const auto& Pair : ExcludedSelection) ++FindGroup(Pair.Value.Class.Get())->Excluded;
    UpdatedSelectionTypes.Reset(SelectionTypeLookup.Num());
    for (auto It = SelectionTypeLookup.CreateIterator(); It; ++It)
    {
        const auto& Row = It.Value();
        if (Row->Selected == 0 && Row->Excluded == 0)
        {
            It.RemoveCurrent();
            continue;
        }
        UpdatedSelectionTypes.Add(Row);
    }
    UpdatedSelectionTypes.Sort([](const auto& A, const auto& B) {
        const int32 Compared = A->Name.CompareTo(B->Name);
        return Compared != 0 ? Compared < 0 : A->Class->GetPathName() < B->Class->GetPathName();
    });
    const bool bRowsChanged = UpdatedSelectionTypes != SelectionTypes;
    Swap(SelectionTypes, UpdatedSelectionTypes);
    if (SelectionTypes.IsEmpty() && !IsSelectionBusy())
    {
        ReleaseSelectionPanel();
        SelectionMessage = FText::GetEmpty();
        return;
    }
    if (!SelectionPanel.IsValid())
    {
        if (GetPanelSelectedCount() <= 1 && ExcludedSelection.IsEmpty()) return;
        auto* Viewport = PC->GetWorld()->GetGameViewport();
        if (!Viewport) return;
        SelectionPanel = SNew(SMISSelectionPanel).Owner(this);
        SelectionViewport = Viewport;
        Viewport->AddViewportWidgetContent(SelectionPanel.ToSharedRef(), 102);
    }
    const bool bRight = UMISConfigurationLibrary::GetSelectionPanelSide(PC) == EMISDistancePanelSide::Right;
    SelectionPanel->SetSide(bRight);
    SelectionPanel->Refresh(bRowsChanged);
}

bool FMISModule::QueueSelectionInteraction(FKey Key, const FModifierKeysState& Modifiers, bool bRepeat)
{
    const auto* PC = Player.Get();
    if (!IsDefaultDismantleMode() || !InputBindings.SelectionFilter.Matches(Key, Modifiers) || !SelectionPanel.IsValid() ||
        Overlay.IsValid() || !PC || PC->IsPaused() || (!bSelectionInteractive && PC->bShowMouseCursor)) return false;
    if (!bRepeat) bSelectionRequested = true;
    return true;
}

void FMISModule::ToggleSelectionInteraction()
{
    if (bSelectionInteractive) { CloseSelectionInteraction(); return; }
    auto* PC = Player.Get();
    if (!IsDefaultDismantleMode() || !PC || PC->IsPaused() || Overlay.IsValid() || IsApplying() || !SelectionPanel.IsValid() ||
        (GetPanelSelectedCount() <= 1 && ExcludedSelection.IsEmpty()) ||
        PC->bShowMouseCursor || PC->IsInputKeyDown(InputBindings.Primary.Key)) return;
    bSelectionPreviousCursor = PC->bShowMouseCursor;
    bSelectionInteractive = true;
    NextHintUpdate = 0;
    ActiveState->ResetBuildGunDelay();
    ActiveState->SetAimedAtActor(nullptr);
    PC->SetIgnoreLookInput(true);
    PC->SetIgnoreMoveInput(true);
    PC->bShowMouseCursor = true;
    SelectionPanel->SetVisibility(EVisibility::Visible);
    FInputModeGameAndUI Mode;
    Mode.SetWidgetToFocus(SelectionPanel);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(Mode);
    FSlateApplication::Get().SetKeyboardFocus(SelectionPanel);
}

void FMISModule::CloseSelectionInteraction()
{
    if (!bSelectionInteractive) return;
    if (!SelectionOperations.IsEmpty())
    {
        SelectionOperations.Reset();
        SelectionMessage = LOCTEXT("SelectionStopped", "Update stopped. Completed changes remain; recheck a type to restore excluded buildings.");
        bSelectionDirty = true;
    }
    bSelectionInteractive = false;
    NextHintUpdate = 0;
    if (auto* PC = Player.Get())
    {
        PC->SetIgnoreLookInput(false);
        PC->SetIgnoreMoveInput(false);
        PC->bShowMouseCursor = bSelectionPreviousCursor;
        PC->SetInputMode(FInputModeGameOnly());
    }
    if (SelectionPanel.IsValid()) SelectionPanel->SetVisibility(GetPanelSelectedCount() > 1
        ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
}

void FMISModule::SetSelectionTypeEnabled(UClass* Class, bool bEnabled)
{
    if (!IsDefaultDismantleMode() || !Class || !bSelectionInteractive || IsSelectionBusy()) return;
    auto* State = ActiveState.Get();
    bRestoreSelection = bEnabled;
    NextSelectionOperation = SelectionOperationChanged = 0;
    if (bEnabled)
    {
        for (const auto& Pair : ExcludedSelection)
            if (Pair.Value.Class.Get() == Class) SelectionOperations.Add(Pair.Value);
    }
    else
    {
        for (AActor* Actor : State->GetPendingDismantleActors())
            if (BuildableEligibility.IsActorSelectable(Actor) && Actor->GetClass() == Class) SelectionOperations.Add(CaptureSelectionObject(Actor));
    }
    SelectionMessage = FText::GetEmpty();
}

void FMISModule::ProcessSelectionOperations()
{
    if (!IsDefaultDismantleMode() || SelectionOperations.IsEmpty()) return;
    auto* State = ActiveState.Get();
    const double Deadline = FPlatformTime::Seconds() + 0.002;
    const int32 Requested = SelectionOperations.Num();
    TGuardValue<bool> MutationGuard(bInsideSelectionMutation, true);
    do
    {
        const auto Object = SelectionOperations[NextSelectionOperation++];
        if (!IsSelectionObjectValid(Object)) continue;
        AActor* Actor = Object.Actor.Get();
        if (bRestoreSelection)
        {
            if (State->GetNumPendingDismantleActors(false) >= State->GetMaxNumPendingDismantleActors() || !HasObjectHeadroom()) break;
            const auto Filter = State->GetCurrentDismantleClassFilter();
            if (Filter && !Object.Class->IsChildOf(Filter)) continue;
            if (Object.LightweightIndex != INDEX_NONE)
            {
                auto* Subsystem = AFGLightweightBuildableSubsystem::Get(Player->GetWorld());
                auto* Data = Subsystem ? Subsystem->GetRuntimeDataForBuildableClassAndIndex(Object.Class.Get(), Object.LightweightIndex) : nullptr;
                if (!Data || !Data->IsValid()) continue;
                bool bSpawned = false;
                auto* Temporary = Subsystem->FindOrSpawnBuildableForRuntimeData(Object.Class.Get(), Data, Object.LightweightIndex, bSpawned);
                Actor = Temporary ? Temporary->Buildable.Get() : nullptr;
            }
            if (!BuildableEligibility.IsActorSelectable(Actor) || !State->CanDismantleActor(Actor)) continue;
            auto* Buildable = Cast<AFGBuildable>(Actor);
            const bool bPin = Buildable && Buildable->GetIsLightweightTemporary() && !Buildable->GetBlockCleanupOfTemporary();
            if (bPin) Buildable->SetBlockCleanupOfTemporary(true);
            {
                TGuardValue<bool> RemoveGuard(State->mShouldRemoveFromMultiSelect, false);
                State->AddPendingDismantleActor(Actor);
            }
            MarkSelectionChanged();
            if (ActiveState.Get() != State || SelectionOperations.IsEmpty())
            {
                if (bPin && IsValid(Buildable)) Buildable->SetBlockCleanupOfTemporary(false);
                return;
            }
            if (State->mPendingDismantleActors.Contains(Actor))
            {
                ++SelectionOperationChanged;
                if (bPin) PinnedTemporaries.AddUnique(Buildable);
                const auto Key = Object.GetKey();
                if (const auto* Excluded = ExcludedSelection.Find(Key); Excluded && Excluded->SameObject(Object))
                    ExcludedSelection.Remove(Key);
            }
            else if (bPin) Buildable->SetBlockCleanupOfTemporary(false);
        }
        else if (IsValid(Actor) && State->mPendingDismantleActors.Contains(Actor) &&
            CaptureSelectionObject(Actor).SameObject(Object))
        {
            TGuardValue<bool> RemoveGuard(State->mShouldRemoveFromMultiSelect, true);
            State->AddPendingDismantleActor(Actor);
            MarkSelectionChanged();
            if (ActiveState.Get() != State || SelectionOperations.IsEmpty()) return;
            if (!State->mPendingDismantleActors.Contains(Actor))
            {
                ++SelectionOperationChanged;
                ExcludedSelection.Add(Object.GetKey(), Object);
            }
        }
    } while (NextSelectionOperation < Requested && FPlatformTime::Seconds() < Deadline);
    bSelectionDirty = true;
    if (NextSelectionOperation >= Requested || (bRestoreSelection &&
        (State->GetNumPendingDismantleActors(false) >= State->GetMaxNumPendingDismantleActors() || !HasObjectHeadroom())))
    {
        SelectionMessage = SelectionOperationChanged == Requested ? FText::GetEmpty()
            : bRestoreSelection
                ? LOCTEXT("SelectionRestoreIncomplete", "Some buildings could not be restored. Check the dismantle limit and filter.")
                : LOCTEXT("SelectionExcludeIncomplete", "Some buildings could not be removed from the selection.");
        SelectionOperations.Reset();
    }
}

int32 FMISModule::GetPanelSelectedCount() const
{
    int32 Selected = 0;
    for (const auto& Row : SelectionTypes) Selected += Row->Selected;
    return Selected;
}

FText FMISModule::GetSelectionStatus() const
{
    if (IsSelectionBusy()) return LOCTEXT("SelectionUpdating", "Updating selection…");
    return SelectionMessage;
}

#undef LOCTEXT_NAMESPACE
