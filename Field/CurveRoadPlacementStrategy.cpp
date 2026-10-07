#include "CurveRoadPlacementStrategy.h"

using namespace DirectX;

std::optional<RoadSegment> CurveRoadPlacementStrategy::OnPlacementPoint(const XMFLOAT3& worldPoint)
{
    if (!m_hasStart)
    {
        // ドラッグ開始点。ここではまだ区間を確定しない。
        m_hasStart = true;
        m_lastConfirmedPoint = worldPoint;
        m_smoothedPoint = worldPoint;
        return std::nullopt;
    }

    // 生の入力点(マウスのブレを含む)に向けて、平滑化した位置を少しずつ近づける
    // (指数移動平均)。これによりマウスの微細な震えが吸収され、軌跡が滑らかになる。
    XMVECTOR smoothedVec = XMLoadFloat3(&m_smoothedPoint);
    XMVECTOR rawVec = XMLoadFloat3(&worldPoint);
    smoothedVec = XMVectorLerp(smoothedVec, rawVec, kSmoothingFactor);
    XMStoreFloat3(&m_smoothedPoint, smoothedVec);

    XMVECTOR lastConfirmedVec = XMLoadFloat3(&m_lastConfirmedPoint);
    float distance = XMVectorGetX(XMVector3Length(smoothedVec - lastConfirmedVec));

    if (distance < kMinSegmentLength)
    {
        // まだ次の小区間を確定するほど離れていない
        return std::nullopt;
    }

    RoadSegment segment;
    segment.start = m_lastConfirmedPoint;
    segment.end = m_smoothedPoint;

    m_lastConfirmedPoint = m_smoothedPoint;

    return segment;
}

void CurveRoadPlacementStrategy::Reset()
{
    m_hasStart = false;
}

bool CurveRoadPlacementStrategy::HasPendingStart(XMFLOAT3& outStart) const
{
    if (!m_hasStart)
    {
        return false;
    }

    outStart = m_lastConfirmedPoint;
    return true;
}
