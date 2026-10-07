#include "IOccupancyGrid.h"

bool IOccupancyGrid::IsFreeRange(const std::vector<GridCoord>& coords) const
{
    for (const GridCoord& coord : coords)
    {
        if (!IsFree(coord))
        {
            return false;
        }
    }
    return true;
}

void IOccupancyGrid::SetOccupiedRange(const std::vector<GridCoord>& coords, bool occupied)
{
    for (const GridCoord& coord : coords)
    {
        SetOccupied(coord, occupied);
    }
}
