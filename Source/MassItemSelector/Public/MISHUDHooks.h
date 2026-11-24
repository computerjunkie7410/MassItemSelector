#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MISHUDHooks.generated.h"

class FStructOnScope;
class FArrayProperty;
class APlayerController;
namespace MIS { struct FDismantleBindings; }

enum class EMISDismantleHints { Native, Default, Selection, Draw, Preview };

UCLASS()
class MASSITEMSELECTOR_API UMISHUDHooks : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, CustomThunk, Category = "Mass Item Selector|HUD", meta = (ArrayParm = "Hints", BlueprintInternalUseOnly = "true"))
    static bool ShouldRebuildNativeHints(UObject* Target, const TArray<int32>& Hints);
    DECLARE_FUNCTION(execShouldRebuildNativeHints);
    static void ClearNativeHintState();
    static void UpdateDismantleCounter(UObject* Target, int32 Count, int32 Limit, bool bNativeMultiSelect);
    static bool HaveSameHints(FArrayProperty* Array, const void* A, const void* B);
    static void UpdateDismantleHints(UObject* Target, EMISDismantleHints Mode, TSharedPtr<FStructOnScope>& SavedHints, APlayerController* Player, const MIS::FDismantleBindings& Bindings);
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FMISNativeHintGuardTest;
#endif
    static bool bUpdatingHints;
    static void TrackNativeHints(UObject* Target, FArrayProperty* Array, const void* InitialHints);
    static bool CheckNativeHints(UObject* Target, FArrayProperty* Array, const void* Hints);
    static bool CopyNativeHints(UObject* Target, FArrayProperty* Array, void* Hints);
};
