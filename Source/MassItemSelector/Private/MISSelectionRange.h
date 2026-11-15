#pragma once
#include <cmath>
#include <algorithm>

namespace MIS
{
inline constexpr float MinSelectionDistanceMeters = 5.f;
inline constexpr float MaxSelectionDistanceMeters = 500.f;

inline float ClampSelectionDistanceMeters(float Requested)
{
    return std::isfinite(Requested)
        ? std::clamp(Requested, MinSelectionDistanceMeters, MaxSelectionDistanceMeters) : 100.f;
}

inline bool WithinMaxDistance(double DistanceSquared, double Maximum)
{
    return std::isfinite(DistanceSquared) && DistanceSquared >= 0 &&
        std::isfinite(Maximum) && Maximum > 0 &&
        DistanceSquared <= Maximum * Maximum;
}
}
