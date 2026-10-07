#include <cfloat>
#include <cmath>
#include "RoadProximity.h"

using namespace DirectX;

std::optional<NearestRoadPoint> FindNearestPointOnRoad(
    const std::vector<RoadSegment>& segments,
    const XMFLOAT3& queryPoint,
    float maxDistance,
    float widthFactor)
{
    XMVECTOR queryVec = XMLoadFloat3(&queryPoint);

    bool found = false;
    NearestRoadPoint best{};
    float bestDist = FLT_MAX;

    for (size_t i = 0; i < segments.size(); ++i)
    {
        const RoadSegment& segment = segments[i];

        XMVECTOR startVec = XMLoadFloat3(&segment.start);
        XMVECTOR endVec = XMLoadFloat3(&segment.end);
        XMVECTOR segmentDir = endVec - startVec;

        float segmentLengthSq = XMVectorGetX(XMVector3LengthSq(segmentDir));

        // start==endの退化した区間は始点との距離だけで判定する
        float t = 0.0f;
        if (segmentLengthSq > 0.0001f)
        {
            t = XMVectorGetX(XMVector3Dot(queryVec - startVec, segmentDir)) / segmentLengthSq;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
        }

        XMVECTOR closest = startVec + segmentDir * t;
        float dist = XMVectorGetX(XMVector3Length(queryVec - closest));

        // widthFactor>0の時だけ、この区間自身の道幅ぶん判定範囲を広げる
        // (道路面の上にカーソルがあるのに吸着しない問題への対策。既定0では従来通り)。
        float acceptRadius = maxDistance + segment.width * widthFactor;

        if (dist <= acceptRadius && dist < bestDist)
        {
            bestDist = dist;
            XMStoreFloat3(&best.point, closest);
            best.segmentIndex = i;
            best.distance = dist;
            found = true;
        }
    }

    if (!found)
    {
        return std::nullopt;
    }

    return best;
}
