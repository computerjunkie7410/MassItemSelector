#pragma once

#include "FGGameFeatureData.h"
#include "MISGameFeatureData.generated.h"

UCLASS()
class MASSITEMSELECTOR_API UMISGameFeatureData : public UFGGameFeatureData
{
    GENERATED_BODY()

public:
    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return HasAnyFlags(RF_ClassDefaultObject) ? FPrimaryAssetId()
            : FPrimaryAssetId(UGameFeatureData::StaticClass()->GetFName(), GetFName());
    }
};
