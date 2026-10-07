#include "StraightRoadPlacementStrategy.h"

using namespace DirectX;

std::optional<RoadSegment> StraightRoadPlacementStrategy::OnPlacementPoint(const XMFLOAT3& worldPoint)
{
    if (!m_hasStart)
    {
        m_start = worldPoint;
        m_hasStart = true;
        return std::nullopt;
    }

    RoadSegment segment;
    segment.start = m_start;
    segment.end = worldPoint;
    segment.width = m_defaultWidth;

    m_hasStart = false;

    return segment;
}

void StraightRoadPlacementStrategy::Reset()
{
    m_hasStart = false;
}

bool StraightRoadPlacementStrategy::HasPendingStart(XMFLOAT3& outStart) const
{
    if (!m_hasStart)
    {
        return false;
    }

    outStart = m_start;
    return true;
}
