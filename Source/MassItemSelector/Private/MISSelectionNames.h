#pragma once

#include "CoreMinimal.h"

inline FText MISFormatSelectionTypeName(const FText& Name, const FText& Material)
{
    if (Material.IsEmpty()) return Name;

    return FText::Format(NSLOCTEXT("ItemSelector", "SelectionTypeMaterial", "{0} ({1})"),
        Name, Material);
}
