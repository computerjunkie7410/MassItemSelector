#pragma once

#include "CoreMinimal.h"
#include "MISSelectionObserver.generated.h"

UCLASS()
class UMISSelectionObserver : public UObject
{
    GENERATED_BODY()
public:
    TFunction<void()> OnChanged;
    UFUNCTION()
    void SelectionChanged();
};
