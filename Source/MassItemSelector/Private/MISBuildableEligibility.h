#pragma once

#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableSignSupport.h"
#include "FGBuildablePipelineFlowIndicator.h"
#include "FGDismantleInterface.h"
#include "Resources/FGBuildDescriptor.h"

namespace MIS
{
inline bool CheckBuildableType(UClass* Class, UClass* Descriptor)
{
    if (!Class || !Class->IsChildOf(AFGBuildable::StaticClass()) ||
        Class->IsChildOf(AFGBuildablePipelineFlowIndicator::StaticClass()) ||
        Class->IsChildOf(AFGBuildableSignSupport::StaticClass())) return false;
    // Some sign poles inherit from the generic pole class.
    for (const UClass* Parent = Class; Parent; Parent = Parent->GetSuperClass())
        if (Parent->GetName().StartsWith(TEXT("Build_SignPole_"))) return false;
    if (!Descriptor || !Descriptor->IsChildOf(UFGBuildDescriptor::StaticClass())) return false;
    const auto BuildClass = UFGBuildDescriptor::GetBuildClass(Descriptor);
    return BuildClass && Class->IsChildOf(BuildClass);
}

class FBuildableEligibilityCache
{
public:
    bool IsTypeSelectable(UClass* Class, UClass* Descriptor) const
    {
        if (!IsValid(Class) || !IsValid(Descriptor)) return false;
        auto& Descriptors = Results.FindOrAdd(Class);
        if (const bool* Cached = Descriptors.Find(Descriptor)) return *Cached;
        const bool bSelectable = CheckBuildableType(Class, Descriptor);
        Descriptors.Add(Descriptor, bSelectable);
        return bSelectable;
    }

    bool IsActorSelectable(AActor* Actor) const
    {
        const auto* Buildable = Cast<AFGBuildable>(Actor);
        return IsValid(Buildable) && !Buildable->GetParentBuildableActor() &&
            IsTypeSelectable(Buildable->GetClass(), Buildable->GetBuiltWithDescriptor()) &&
            IFGDismantleInterface::Execute_CanDismantle(Actor);
    }

    void Reset() { Results.Reset(); }

private:
    mutable TMap<TWeakObjectPtr<UClass>, TMap<TWeakObjectPtr<UClass>, bool>> Results;
};
}
