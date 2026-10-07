#include <cmath>
#include "GridCoord.h"

using namespace DirectX;

GridCoord WorldToGrid(const XMFLOAT3& worldPos, float cellSize)
{
    GridCoord coord;
    coord.x = static_cast<int>(std::floor(worldPos.x / cellSize));
    coord.z = static_cast<int>(std::floor(worldPos.z / cellSize));
    return coord;
}

XMFLOAT3 GridToWorld(const GridCoord& coord, float cellSize)
{
    return XMFLOAT3(
        (static_cast<float>(coord.x) + 0.5f) * cellSize,
        0.0f,
        (static_cast<float>(coord.z) + 0.5f) * cellSize
    );
}

XMFLOAT3 GridToWorldCorner(const GridCoord& coord, float cellSize)
{
    return XMFLOAT3(
        static_cast<float>(coord.x) * cellSize,
        0.0f,
        static_cast<float>(coord.z) * cellSize
    );
}

