#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MISHUDHooks.generated.h"

UCLASS()
class MASSITEMSELECTOR_API UMISHUDHooks : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Update only our entry in the native Widget_DismantleMode hint cache.
    static bool UpdateMassSelectionHint(UObject* Target, bool bRemove);
};
