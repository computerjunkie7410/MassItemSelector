#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FMISModule;

struct FMISSelectionKey
{
    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<UClass> Class;
    int32 LightweightIndex = INDEX_NONE;

    bool operator==(const FMISSelectionKey& Other) const
    {
        return LightweightIndex == Other.LightweightIndex &&
            (LightweightIndex == INDEX_NONE ? Actor == Other.Actor : Class == Other.Class);
    }

    friend uint32 GetTypeHash(const FMISSelectionKey& Key)
    {
        return Key.LightweightIndex == INDEX_NONE ? GetTypeHash(Key.Actor)
            : HashCombine(GetTypeHash(Key.Class), GetTypeHash(Key.LightweightIndex));
    }
};

struct FMISSelectionObject
{
    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<UClass> Class;
    int32 LightweightIndex = INDEX_NONE;
    FTransform Transform;

    FMISSelectionKey GetKey() const { return { Actor, Class, LightweightIndex }; }

    bool SameObject(const FMISSelectionObject& Other) const
    {
        return GetKey() == Other.GetKey() &&
            (LightweightIndex == INDEX_NONE || Transform.Equals(Other.Transform));
    }
};

struct FMISSelectionSnapshotEntry
{
    FMISSelectionObject Object;
    bool bEligible = false;

    bool Matches(const FMISSelectionSnapshotEntry& Other) const
    {
        return bEligible == Other.bEligible && Object.Actor == Other.Object.Actor && Object.SameObject(Other.Object);
    }
};

struct FMISSelectionType
{
    TWeakObjectPtr<UClass> Class;
    FText Name;
    TWeakObjectPtr<UClass> NameRecipe;
    TWeakObjectPtr<UClass> NameDescriptor;
    uint32 NameTextRevision = MAX_uint32;
    int32 Selected = 0;
    int32 Excluded = 0;
    FText Label;
    FText CachedName;
    int32 CachedCount = INDEX_NONE;
    uint16 TextRevision = 0;
    float LabelWidth = 0.f;
};

class SMISSelectionPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMISSelectionPanel) {}
        SLATE_ARGUMENT(FMISModule*, Owner)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    void Refresh(bool bRowsChanged);
    void SetSide(bool bRight);
    bool IsUnderPointer(const FPointerEvent& Event) const;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnPreviewKeyDown(const FGeometry&, const FKeyEvent&) override;
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent&) override;
    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;
private:
    TSharedRef<ITableRow> MakeRow(TSharedPtr<FMISSelectionType> Item, const TSharedRef<STableViewBase>& Table);
    FMISModule* Owner = nullptr;
    TSharedPtr<SListView<TSharedPtr<FMISSelectionType>>> List;
    TSharedPtr<SWidget> Body;
    float PanelWidth = 320.f;
    FSlateFontInfo RowFont;
};
