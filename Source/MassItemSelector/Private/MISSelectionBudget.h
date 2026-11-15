#pragma once

namespace MIS
{
inline int RemainingCapacity(int Limit, int Selected)
{
    if (Limit <= 0) return 0;
    if (Selected <= 0) return Limit;
    return Selected >= Limit ? 0 : Limit - Selected;
}
}
