#include "MISConfiguration.h"
#include "MISSelectionRange.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Configuration/Properties/WidgetExtension/CP_Float.h"
#include "Configuration/Properties/WidgetExtension/CP_Integer.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "ItemSelector"

namespace
{
constexpr float DefaultWheelStep = 5.f;
constexpr float MinWheelStep = 0.1f;
constexpr float MaxWheelStep = MIS::MaxSelectionDistanceMeters;

UConfigPropertySection* GetPreferences(UObject* WorldContext)
{
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
    auto* Manager = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UConfigManager>() : nullptr;
    return Manager ? Manager->GetConfigurationRootSection(GetDefault<UMISModConfiguration>()->ConfigId) : nullptr;
}
}

UMISModConfiguration::UMISModConfiguration()
{
    ConfigId.ModReference = TEXT("MassItemSelector");
    DisplayName = LOCTEXT("ConfigName", "Mass Item Selector");
    Description = LOCTEXT("ConfigDescription", "Customize Mass Selection controls and distance panel placement.");
}

void UMISModConfiguration::PostInitProperties()
{
    Super::PostInitProperties();
    if (!HasAnyFlags(RF_ClassDefaultObject) || RootSection) return;
    auto* SectionClass = LoadClass<UCP_Section>(nullptr, TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertySection.BP_ConfigPropertySection_C"));
    auto* FloatClass = LoadClass<UCP_Float>(nullptr, TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyFloat.BP_ConfigPropertyFloat_C"));
    auto* IntegerClass = LoadClass<UCP_Integer>(nullptr, TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyInteger.BP_ConfigPropertyInteger_C"));
    checkf(SectionClass && FloatClass && IntegerClass, TEXT("Item Selector requires SML's configuration property editors."));
    auto* Section = NewObject<UCP_Section>(this, SectionClass, TEXT("Preferences"));
    Section->bRequiresWorldReload = false;
    Section->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
    Section->HasHeader = false;
    RootSection = Section;

    auto* Wheel = NewObject<UCP_Float>(Section, FloatClass, TEXT("WheelStepMeters"));
    Wheel->bRequiresWorldReload = false;
    Wheel->DisplayName = LOCTEXT("WheelStepLabel", "Wheel step (m)");
    Wheel->Tooltip = LOCTEXT("WheelStepTooltip", "Meters per wheel step when adjusting selection distance. Accepts 0.1 to 500 meters; selection distance is capped at 500 meters.");
    Wheel->WidgetType = ECP_FloatWidgetType::CPF_Spinbox;
    Wheel->MinValue = MinWheelStep;
    Wheel->MaxValue = MaxWheelStep;
    Wheel->DefaultValue = Wheel->Value = DefaultWheelStep;
    Section->SectionProperties.Add(TEXT("WheelStepMeters"), Wheel);

    auto* Side = NewObject<UCP_Integer>(Section, IntegerClass, TEXT("DistancePanelSide"));
    Side->bRequiresWorldReload = false;
    Side->DisplayName = LOCTEXT("PanelSideLabel", "Distance panel side");
    Side->Tooltip = LOCTEXT("PanelSideTooltip", "Place the distance controls at the top left or top right. Takes effect when Mass Selection is opened.");
    Side->WidgetType = ECP_IntegerWidgetType::CPI_Enum;
    Side->EnumClass = StaticEnum<EMISDistancePanelSide>();
    Side->MinValue = 0;
    Side->MaxValue = 1;
    Side->DefaultValue = Side->Value = static_cast<int32>(EMISDistancePanelSide::Left);
    Section->SectionProperties.Add(TEXT("DistancePanelSide"), Side);
}

UMISGameInstanceModule::UMISGameInstanceModule()
{
    bRootModule = true;
    ModConfigurations.Add(UMISModConfiguration::StaticClass());
}

float UMISConfigurationLibrary::GetWheelStep(UObject* WorldContext)
{
    auto* Root = GetPreferences(WorldContext);
    auto* Property = Root ? Cast<UConfigPropertyFloat>(Root->SectionProperties.FindRef(TEXT("WheelStepMeters"))) : nullptr;
    return Property && FMath::IsFinite(Property->Value) ? FMath::Clamp(Property->Value, MinWheelStep, MaxWheelStep) : DefaultWheelStep;
}

EMISDistancePanelSide UMISConfigurationLibrary::GetPanelSide(UObject* WorldContext)
{
    auto* Root = GetPreferences(WorldContext);
    auto* Property = Root ? Cast<UConfigPropertyInteger>(Root->SectionProperties.FindRef(TEXT("DistancePanelSide"))) : nullptr;
    return Property && Property->Value == static_cast<int32>(EMISDistancePanelSide::Right)
        ? EMISDistancePanelSide::Right : EMISDistancePanelSide::Left;
}

#undef LOCTEXT_NAMESPACE
