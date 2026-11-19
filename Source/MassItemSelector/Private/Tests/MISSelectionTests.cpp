#if WITH_DEV_AUTOMATION_TESTS

#include "MISSelectionPanel.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableWire.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISSelectionIdentityTest, "MassItemSelector.Selection.Identity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISSelectionIdentityTest::RunTest(const FString&)
{
    FMISSelectionObject Original;
    Original.Actor = GetMutableDefault<AFGBuildable>();
    Original.Class = AFGBuildable::StaticClass();
    Original.LightweightIndex = 7;
    Original.Transform = FTransform::Identity;

    TMap<FMISSelectionKey, FMISSelectionObject> Excluded;
    Excluded.Add(Original.GetKey(), Original);
    auto Recreated = Original;
    Recreated.Actor = GetMutableDefault<AFGBuildableWire>();
    TestTrue(TEXT("A new temporary actor still identifies the original instance"), Original.SameObject(Recreated));
    TestNotNull(TEXT("The original exclusion is found through its instance key"), Excluded.Find(Recreated.GetKey()));

    auto Recycled = Recreated;
    Recycled.Transform.SetTranslation(FVector(1000, 0, 0));
    const auto* Previous = Excluded.Find(Recycled.GetKey());
    TestNotNull(TEXT("A reused index finds the old record for validation"), Previous);
    TestFalse(TEXT("A different transform cannot reuse that exclusion"), Previous && Previous->SameObject(Recycled));

    auto OtherClass = Original;
    OtherClass.Class = AFGBuildableWire::StaticClass();
    TestNull(TEXT("The same instance index on another class is distinct"), Excluded.Find(OtherClass.GetKey()));
    auto OtherIndex = Original;
    ++OtherIndex.LightweightIndex;
    TestNull(TEXT("Another index on the same class is distinct"), Excluded.Find(OtherIndex.GetKey()));

    auto Actor = Original;
    Actor.LightweightIndex = INDEX_NONE;
    auto MovedActor = Actor;
    MovedActor.Transform.SetTranslation(FVector(1000, 0, 0));
    TestTrue(TEXT("Moving a regular actor preserves its identity"), Actor.SameObject(MovedActor));
    auto OtherActor = Actor;
    OtherActor.Actor = GetMutableDefault<AFGBuildableWire>();
    TestFalse(TEXT("Different regular actors have distinct keys"), Actor.GetKey() == OtherActor.GetKey());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMISSelectionSnapshotTest, "MassItemSelector.Selection.Snapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMISSelectionSnapshotTest::RunTest(const FString&)
{
    FMISSelectionSnapshotEntry Original;
    Original.Object.Actor = GetMutableDefault<AFGBuildable>();
    Original.Object.Class = AFGBuildable::StaticClass();
    Original.Object.LightweightIndex = 7;
    Original.Object.Transform = FTransform::Identity;
    Original.bEligible = true;
    TestTrue(TEXT("An unchanged selection needs no regrouping"), Original.Matches(Original));

    auto Changed = Original;
    Changed.bEligible = false;
    TestFalse(TEXT("Dismantle eligibility changes trigger regrouping"), Original.Matches(Changed));
    Changed = Original;
    Changed.Object.Transform.SetTranslation(FVector(1000, 0, 0));
    TestFalse(TEXT("A recycled lightweight instance changes the snapshot"), Original.Matches(Changed));
    Changed = Original;
    Changed.Object.Actor = GetMutableDefault<AFGBuildableWire>();
    TestFalse(TEXT("A new temporary actor invalidates the actor lookup"), Original.Matches(Changed));
    return true;
}

#endif
