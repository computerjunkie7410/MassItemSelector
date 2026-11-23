#include "MISInputBindings.h"
#include "FGInputLibrary.h"
#include "InputAction.h"
#include "FGPlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "InputTriggers.h"
#include "GenericPlatform/GenericApplication.h"

namespace MIS
{
const FName LassoMappingName(TEXT("MassItemSelector.Lasso"));

void FInputBinding::Update(APlayerController* Player, const TCHAR* ActionPath)
{
    if (!Action.IsValid()) Action.Reset(LoadObject<UInputAction>(nullptr, ActionPath));
    const auto* Input = Player ? Cast<UFGPlayerInput>(Player->PlayerInput) : nullptr;
    ResolveMappings(Input ? MakeArrayView(Input->GetEnhancedActionMappings()) : TConstArrayView<FEnhancedActionKeyMapping>());
    if (Input && Action.IsValid())
    {
        FKey ConfiguredKey;
        TArray<FKey> ConfiguredModifiers;
        if (Input->GetCurrentMappingForInputAction(Action.Get(), ConfiguredKey, &ConfiguredModifiers, MappingName, nullptr))
        {
            Key = ConfiguredKey;
            Modifiers = MoveTemp(ConfiguredModifiers);
        }
    }
}

void FInputBinding::ResolveMappings(TConstArrayView<FEnhancedActionKeyMapping> Mappings)
{
    Key = EKeys::Invalid;
    Modifiers.Reset();
    MappingName = NAME_None;
    if (!Action.IsValid()) return;
    for (const auto& Mapping : Mappings)
    {
        if (Mapping.Action != Action.Get() || !Mapping.Key.IsValid() || Mapping.Key.IsGamepadKey()) continue;
        Key = Mapping.Key;
        MappingName = Mapping.GetMappingName();
        auto AddModifiers = [&](const auto& Triggers) {
            for (const auto& Trigger : Triggers)
            {
                auto* Chord = Cast<UInputTriggerChordAction>(Trigger);
                if (!Chord || Chord->GetTriggerClassType() != ETriggerClass::ChordAction) continue;
                for (const auto& ModifierMapping : Mappings)
                    if (ModifierMapping.Action == Chord->ChordAction && ModifierMapping.Key.IsModifierKey())
                        Modifiers.AddUnique(ModifierMapping.Key);
            }
        };
        AddModifiers(Mapping.Triggers);
        AddModifiers(Action->Triggers);
        return;
    }
}

bool FInputBinding::Matches(FKey InKey, const FModifierKeysState& State) const
{
    return Key.IsValid() && InKey == Key && AreModifiersDown(State, Modifiers);
}

FText FInputBinding::GetText(APlayerController* Player) const
{
    if (!Key.IsValid()) return FText::GetEmpty();
    return MappingName.IsNone() ? UFGInputLibrary::GetAbbreviatedKeyName(Key)
        : UFGInputLibrary::GetInputActionNameAsText_Keyboard(Player, MappingName);
}

void FDismantleBindings::Update(APlayerController* Player)
{
    Dismantle.Update(Player, TEXT("/Game/FactoryGame/Inputs/Player/Actions/IA_ToggleBuildGunDismantle.IA_ToggleBuildGunDismantle"));
    Close.Update(Player, TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/IA_UnequipBuildGun.IA_UnequipBuildGun"));
    CycleMode.Update(Player, TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/IA_ModeSelect.IA_ModeSelect"));
    HideHUD.Update(Player, TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/IA_HideHUD.IA_HideHUD"));
    Primary.Update(Player, TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/IA_BuildGun_PrimaryFire.IA_BuildGun_PrimaryFire"));
    Secondary.Update(Player, TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/IA_BuildGun_SecondaryFire.IA_BuildGun_SecondaryFire"));
    SelectionFilter.Update(Player, TEXT("/MassItemSelector/Inputs/IA_SelectionFilter.IA_SelectionFilter"));
}

UInputAction* GetLassoAction()
{
    return LoadObject<UInputAction>(nullptr, TEXT("/MassItemSelector/Inputs/IA_Lasso.IA_Lasso"));
}

FText GetLassoKeyText(APlayerController* Player)
{
    return Player ? UFGInputLibrary::GetInputActionNameAsText_Keyboard(Player, LassoMappingName) : FText::GetEmpty();
}

bool AreModifiersDown(const FModifierKeysState& State, const TArray<FKey>& Modifiers)
{
    for (const FKey& Key : Modifiers)
    {
        if ((Key == EKeys::LeftControl || Key == EKeys::RightControl) && !State.IsControlDown()) return false;
        if ((Key == EKeys::LeftShift || Key == EKeys::RightShift) && !State.IsShiftDown()) return false;
        if ((Key == EKeys::LeftAlt || Key == EKeys::RightAlt) && !State.IsAltDown()) return false;
        if ((Key == EKeys::LeftCommand || Key == EKeys::RightCommand) && !State.IsCommandDown()) return false;
    }
    return true;
}
}
