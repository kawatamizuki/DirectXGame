#include "CellOccupancyGrid.h"

bool CellOccupancyGrid::IsFree(const GridCoord& coord) const
{
    return m_occupiedCells.find(coord) == m_occupiedCells.end();
}

void CellOccupancyGrid::SetOccupied(const GridCoord& coord, bool occupied)
{
    if (occupied)
    {
        m_occupiedCells.insert(coord);
    }
    else
    {
        m_occupiedCells.erase(coord);
    }
}

bool CellOccupancyGrid::IsRectFree(const OrientedRect& rect, unsigned kindMask) const
{
    // 辺が接するだけ(貫入量が許容値以下)は重なりとみなさず、ぴったり隣接できるようにする。
    // 浮動小数の誤差(1e-6程度)より十分大きく、見た目に分かる隙間よりは十分小さい値。
    constexpr float kEpsilon = 0.0001f;

    for (const GridCoord& cell : SampleOrientedRectangleCells(rect, kIndexCellSize))
    {
        auto it = m_rectIndex.find(cell);
        if (it == m_rectIndex.end())
        {
            continue;
        }

        for (int rectIndex : it->second)
        {
            if ((static_cast<unsigned>(m_rectKinds[rectIndex]) & kindMask) == 0)
            {
                continue;
            }

            if (OrientedRectsOverlap(rect, m_rects[rectIndex], kEpsilon))
            {
                return false;
            }
        }
    }

    return true;
}

void CellOccupancyGrid::OccupyRect(const OrientedRect& rect, OccupantKind kind)
{
    int rectIndex = static_cast<int>(m_rects.size());
    m_rects.push_back(rect);
    m_rectKinds.push_back(kind);

    for (const GridCoord& cell : SampleOrientedRectangleCells(rect, kIndexCellSize))
    {
        m_rectIndex[cell].push_back(rectIndex);
    }
}
