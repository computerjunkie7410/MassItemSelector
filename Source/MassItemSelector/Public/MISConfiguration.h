#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "Module/GameInstanceModule.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MISConfiguration.generated.h"

UENUM(BlueprintType)
enum class EMISDistancePanelSide : uint8
{
    Left,
    Right
};

UCLASS()
class MASSITEMSELECTOR_API UMISModConfiguration : public UModConfiguration
{
    GENERATED_BODY()
public:
    UMISModConfiguration();
    virtual void PostInitProperties() override;
};

UCLASS()
class MASSITEMSELECTOR_API UMISGameInstanceModule : public UGameInstanceModule
{
    GENERATED_BODY()
public:
    UMISGameInstanceModule();
};

UCLASS()
class MASSITEMSELECTOR_API UMISConfigurationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    static float GetWheelStep(UObject* WorldContext);
    static EMISDistancePanelSide GetPanelSide(UObject* WorldContext);
    static EMISDistancePanelSide GetSelectionPanelSide(UObject* WorldContext);
};
