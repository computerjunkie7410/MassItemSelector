#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "EnhancedActionKeyMapping.h"
#include "UObject/StrongObjectPtr.h"

class APlayerController;
class FModifierKeysState;
class UInputAction;

namespace MIS
{
struct FInputBinding
{
    FKey Key;
    TArray<FKey> Modifiers;
    FName MappingName;
    TStrongObjectPtr<UInputAction> Action;

    void Update(APlayerController* Player, const TCHAR* ActionPath);
    void ResolveMappings(TConstArrayView<FEnhancedActionKeyMapping> Mappings);
    bool Matches(FKey InKey, const FModifierKeysState& State) const;
    FText GetText(APlayerController* Player) const;
};

struct FDismantleBindings
{
    FInputBinding Dismantle;
    FInputBinding Close;
    FInputBinding CycleMode;
    FInputBinding HideHUD;
    FInputBinding Primary;
    FInputBinding Secondary;
    FInputBinding SelectionFilter;
    void Update(APlayerController* Player);
};

extern const FName LassoMappingName;
UInputAction* GetLassoAction();
FText GetLassoKeyText(APlayerController* Player);
bool AreModifiersDown(const FModifierKeysState& State, const TArray<FKey>& Modifiers);
}
