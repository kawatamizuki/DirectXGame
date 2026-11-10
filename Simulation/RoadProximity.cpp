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

std::optional<NearestRoadPoint> FindNearestRoadToRect(
    const std::vector<RoadSegment>& segments,
    const OrientedRect& rect,
    float maxDistance)
{
    // 矩形のローカル座標(u: ローカル+X、v: ローカル+Z)で考える。矩形は[0,width]×[0,depth]の軸平行な箱になる。
    // 規約はOrientedRectangleOverlap.cppと同じ: ローカル+X→(cos yaw, -sin yaw)、ローカル+Z→(sin yaw, cos yaw)。
    const float c = cosf(rect.yaw);
    const float s = sinf(rect.yaw);
    const float w = rect.width;
    const float d = rect.depth;
    auto toLocal = [&](const XMFLOAT3& p, float& u, float& v)
    {
        const float dx = p.x - rect.origin.x;
        const float dz = p.z - rect.origin.z;
        u = dx * c - dz * s;
        v = dx * s + dz * c;
    };
    // 点から箱までの距離(箱の中なら0)。
    auto distanceToBox = [&](float u, float v)
    {
        const float cu = (u < 0.0f) ? 0.0f : ((u > w) ? w : u);
        const float cv = (v < 0.0f) ? 0.0f : ((v > d) ? d : v);
        return sqrtf((u - cu) * (u - cu) + (v - cv) * (v - cv));
    };

    bool found = false;
    NearestRoadPoint best{};
    float bestDist = FLT_MAX;

    for (size_t i = 0; i < segments.size(); ++i)
    {
        const RoadSegment& segment = segments[i];
        float au, av, bu, bv;
        toLocal(segment.start, au, av);
        toLocal(segment.end, bu, bv);
        const float su = bu - au;
        const float sv = bv - av;

        // 区間(の線分)上の、箱に一番近い点のパラメータtと、その距離を求める。
        float bestT = 0.0f;
        float dist = FLT_MAX;
        auto consider = [&](float t, float distance)
        {
            if (distance < dist) { dist = distance; bestT = t; }
        };

        // (1) 線分が箱を貫いているか(Liang-Barsky)。貫いていれば、距離0。
        {
            float t0 = 0.0f, t1 = 1.0f;
            bool inside = true;
            const float p[4] = { -su, su, -sv, sv };
            const float q[4] = { au, w - au, av, d - av };
            for (int k = 0; k < 4 && inside; ++k)
            {
                if (fabsf(p[k]) < 1e-8f)
                {
                    if (q[k] < 0.0f) { inside = false; }
                }
                else
                {
                    const float r = q[k] / p[k];
                    if (p[k] < 0.0f) { if (r > t1) { inside = false; } else if (r > t0) { t0 = r; } }
                    else { if (r < t0) { inside = false; } else if (r < t1) { t1 = r; } }
                }
            }
            if (inside) { consider(0.5f * (t0 + t1), 0.0f); }
        }
        // (2) 線分の両端から箱まで。
        consider(0.0f, distanceToBox(au, av));
        consider(1.0f, distanceToBox(bu, bv));
        // (3) 箱の4隅から線分まで。
        const float cornerU[4] = { 0.0f, w, w, 0.0f };
        const float cornerV[4] = { 0.0f, 0.0f, d, d };
        const float lenSq = su * su + sv * sv;
        for (int k = 0; k < 4; ++k)
        {
            float t = 0.0f;
            if (lenSq > 0.0001f)
            {
                t = ((cornerU[k] - au) * su + (cornerV[k] - av) * sv) / lenSq;
                if (t < 0.0f) { t = 0.0f; }
                if (t > 1.0f) { t = 1.0f; }
            }
            const float pu = au + su * t;
            const float pv = av + sv * t;
            consider(t, sqrtf((cornerU[k] - pu) * (cornerU[k] - pu) + (cornerV[k] - pv) * (cornerV[k] - pv)));
        }

        if (dist <= maxDistance && dist < bestDist)
        {
            bestDist = dist;
            XMVECTOR startVec = XMLoadFloat3(&segment.start);
            XMVECTOR endVec = XMLoadFloat3(&segment.end);
            XMStoreFloat3(&best.point, startVec + (endVec - startVec) * bestT);
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
