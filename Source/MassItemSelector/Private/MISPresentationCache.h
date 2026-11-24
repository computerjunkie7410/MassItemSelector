#pragma once

#include "CoreMinimal.h"

class FMISDrawingCache
{
public:
    struct FCursorGeometry
    {
        TArray<FVector2D> Ring;
        TArray<FVector2D> Crosshairs[4];
    };

    FMISDrawingCache()
    {
        CursorGeometry.Ring.SetNumUninitialized(33);
        for (int32 I = 0; I <= 32; ++I)
        {
            const double Angle = I * 2.0 * PI / 32;
            RingOffsets[I] = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 10;
        }
        for (auto& Line : CursorGeometry.Crosshairs) Line.SetNumUninitialized(2);
    }

    void InvalidateOutline() { bOutlineDirty = true; }

    const TArray<FVector2D>& GetClosedOutline(const TArray<FVector2D>& Points)
    {
        if (bOutlineDirty)
        {
            ClosedOutline.Reset(Points.Num() + 1);
            ClosedOutline.Append(Points);
            if (!Points.IsEmpty()) ClosedOutline.Add(Points[0]);
            bOutlineDirty = false;
        }
        return ClosedOutline;
    }

    const FCursorGeometry& GetCursorGeometry(const FVector2D& Cursor)
    {
        if (!bCursorReady || Cursor != LastCursor)
        {
            for (int32 I = 0; I <= 32; ++I) CursorGeometry.Ring[I] = Cursor + RingOffsets[I];
            CursorGeometry.Crosshairs[0][0] = Cursor + FVector2D(-20, 0);
            CursorGeometry.Crosshairs[0][1] = Cursor + FVector2D(-5, 0);
            CursorGeometry.Crosshairs[1][0] = Cursor + FVector2D(5, 0);
            CursorGeometry.Crosshairs[1][1] = Cursor + FVector2D(20, 0);
            CursorGeometry.Crosshairs[2][0] = Cursor + FVector2D(0, -20);
            CursorGeometry.Crosshairs[2][1] = Cursor + FVector2D(0, -5);
            CursorGeometry.Crosshairs[3][0] = Cursor + FVector2D(0, 5);
            CursorGeometry.Crosshairs[3][1] = Cursor + FVector2D(0, 20);
            LastCursor = Cursor;
            bCursorReady = true;
        }
        return CursorGeometry;
    }

private:
    TArray<FVector2D> ClosedOutline;
    FVector2D RingOffsets[33];
    FCursorGeometry CursorGeometry;
    FVector2D LastCursor = FVector2D::ZeroVector;
    bool bOutlineDirty = true;
    bool bCursorReady = false;
};
