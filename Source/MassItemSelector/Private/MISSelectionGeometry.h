#pragma once

#include <cmath>
#include <span>

namespace MIS
{
struct FPoint { double X; double Y; };

inline double DistanceSquared(FPoint A, FPoint B)
{
    const double X = A.X - B.X, Y = A.Y - B.Y;
    return X * X + Y * Y;
}

inline double SegmentDistanceSquared(FPoint P, FPoint A, FPoint B)
{
    const double LengthSquared = DistanceSquared(A, B);
    if (LengthSquared == 0.0) return DistanceSquared(P, A);
    double T = ((P.X - A.X) * (B.X - A.X) + (P.Y - A.Y) * (B.Y - A.Y)) / LengthSquared;
    T = T < 0 ? 0 : (T > 1 ? 1 : T);
    return DistanceSquared(P, { A.X + T * (B.X - A.X), A.Y + T * (B.Y - A.Y) });
}

// Reject point/line strokes. Unlike signed area this accepts figure-eight lassos.
inline bool IsUsable(std::span<const FPoint> Polygon)
{
    if (Polygon.size() < 3) return false;
    for (const FPoint P : Polygon)
        if (!std::isfinite(P.X) || !std::isfinite(P.Y)) return false;
    const FPoint A = Polygon.front();
    FPoint B = A;
    for (const FPoint P : Polygon)
        if (DistanceSquared(A, P) > DistanceSquared(A, B)) B = P;
    if (DistanceSquared(A, B) < 16.0) return false;
    for (const FPoint P : Polygon)
        if (SegmentDistanceSquared(P, A, B) >= 4.0) return true;
    return false;
}

// Requires a polygon accepted by IsUsable.
// Even-odd fill, implicit closing edge, boundary included (half a UI unit).
inline bool Contains(std::span<const FPoint> Polygon, FPoint P)
{
    if (!std::isfinite(P.X) || !std::isfinite(P.Y)) return false;
    bool Inside = false;
    for (std::size_t I = 0, J = Polygon.size() - 1; I < Polygon.size(); J = I++)
    {
        const FPoint A = Polygon[J], B = Polygon[I];
        if (SegmentDistanceSquared(P, A, B) <= 0.25) return true;
        if ((A.Y > P.Y) != (B.Y > P.Y) &&
            P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X)
            Inside = !Inside;
    }
    return Inside;
}
}
