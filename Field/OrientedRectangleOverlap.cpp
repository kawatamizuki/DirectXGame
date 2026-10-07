#include "OrientedRectangleOverlap.h"
#include <cmath>
#include <cfloat>

using namespace DirectX;

namespace
{
    struct Vec2
    {
        float x;
        float z;
    };

    // 4隅と2本の辺方向(ワールドXZ)を事前計算したもの。
    // ローカル+X→(cos yaw, -sin yaw)、ローカル+Z→(sin yaw, cos yaw)は、
    // XMQuaternionRotationRollPitchYaw(0,yaw,0)で回転した結果と一致する
    // (BuildControllerのLocalToWorldと同じ回転規約)。
    struct RectGeometry
    {
        Vec2 corners[4];
        Vec2 axisX;
        Vec2 axisZ;
    };

    RectGeometry MakeGeometry(const OrientedRect& rect)
    {
        float c = cosf(rect.yaw);
        float s = sinf(rect.yaw);

        RectGeometry g;
        g.axisX = { c, -s };
        g.axisZ = { s, c };

        Vec2 p0 = { rect.origin.x, rect.origin.z };
        Vec2 p1 = { p0.x + g.axisX.x * rect.width, p0.z + g.axisX.z * rect.width };
        Vec2 p2 = { p1.x + g.axisZ.x * rect.depth, p1.z + g.axisZ.z * rect.depth };
        Vec2 p3 = { p0.x + g.axisZ.x * rect.depth, p0.z + g.axisZ.z * rect.depth };
        g.corners[0] = p0;
        g.corners[1] = p1;
        g.corners[2] = p2;
        g.corners[3] = p3;
        return g;
    }

    // 4隅を、ある軸(ax,az)に投影した時の[min,max]区間を求める。
    void ProjectOntoAxis(const Vec2 corners[4], float ax, float az, float& outMin, float& outMax)
    {
        float d0 = corners[0].x * ax + corners[0].z * az;
        outMin = d0;
        outMax = d0;
        for (int i = 1; i < 4; ++i)
        {
            float d = corners[i].x * ax + corners[i].z * az;
            if (d < outMin) { outMin = d; }
            if (d > outMax) { outMax = d; }
        }
    }

    bool GeometriesOverlap(const RectGeometry& a, const RectGeometry& b, float epsilon)
    {
        const Vec2 axes[4] = { a.axisX, a.axisZ, b.axisX, b.axisZ };

        for (int i = 0; i < 4; ++i)
        {
            float minA, maxA, minB, maxB;
            ProjectOntoAxis(a.corners, axes[i].x, axes[i].z, minA, maxA);
            ProjectOntoAxis(b.corners, axes[i].x, axes[i].z, minB, maxB);

            bool overlapOnAxis = (minA < maxB - epsilon) && (minB < maxA - epsilon);
            if (!overlapOnAxis)
            {
                return false; // この軸で分離できている→重なっていない
            }
        }

        return true;
    }
}

bool OrientedRectsOverlap(const OrientedRect& a, const OrientedRect& b, float epsilon)
{
    return GeometriesOverlap(MakeGeometry(a), MakeGeometry(b), epsilon);
}

bool OrientedRectContainsPoint(const OrientedRect& rect, float x, float z, float tolerance)
{
    // ワールド→ローカルは回転の逆。ローカル+Xが(cos,-sin)、ローカル+Zが(sin,cos)なので、
    // 点をそれぞれの軸に投影すればそのままローカル座標になる。
    float c = cosf(rect.yaw);
    float s = sinf(rect.yaw);
    float dx = x - rect.origin.x;
    float dz = z - rect.origin.z;
    float localX = dx * c - dz * s;
    float localZ = dx * s + dz * c;

    return localX >= -tolerance && localX <= rect.width + tolerance &&
           localZ >= -tolerance && localZ <= rect.depth + tolerance;
}

std::vector<GridCoord> SampleOrientedRectangleCells(const OrientedRect& rect, float cellSize)
{
    std::vector<GridCoord> cells;

    RectGeometry rectGeometry = MakeGeometry(rect);

    float minX = FLT_MAX, maxX = -FLT_MAX, minZ = FLT_MAX, maxZ = -FLT_MAX;
    for (int i = 0; i < 4; ++i)
    {
        const Vec2& p = rectGeometry.corners[i];
        if (p.x < minX) { minX = p.x; }
        if (p.x > maxX) { maxX = p.x; }
        if (p.z < minZ) { minZ = p.z; }
        if (p.z > maxZ) { maxZ = p.z; }
    }

    // 矩形のバウンディングボックスから、判定すべきセルの候補範囲を求める
    // (1マス分余裕を持たせて、floor丸めの都合で候補から漏れないようにする。
    // 実際に重なっているかどうかはこの後のSAT判定で厳密に絞り込まれるため、
    // 候補範囲はやや広めでも問題ない)。
    int cellMinX = static_cast<int>(std::floor(minX / cellSize)) - 1;
    int cellMaxX = static_cast<int>(std::floor(maxX / cellSize)) + 1;
    int cellMinZ = static_cast<int>(std::floor(minZ / cellSize)) - 1;
    int cellMaxZ = static_cast<int>(std::floor(maxZ / cellSize)) + 1;

    constexpr float kEpsilon = 0.0001f;

    for (int cz = cellMinZ; cz <= cellMaxZ; ++cz)
    {
        for (int cx = cellMinX; cx <= cellMaxX; ++cx)
        {
            OrientedRect cellRect;
            cellRect.origin = XMFLOAT3(static_cast<float>(cx) * cellSize, 0.0f, static_cast<float>(cz) * cellSize);
            cellRect.yaw = 0.0f;
            cellRect.width = cellSize;
            cellRect.depth = cellSize;

            if (GeometriesOverlap(MakeGeometry(cellRect), rectGeometry, kEpsilon))
            {
                cells.push_back(GridCoord{ cx, cz });
            }
        }
    }

    return cells;
}

std::vector<GridCoord> SampleOrientedRectangleCells(
    const XMFLOAT3& origin, float yaw,
    float width, float depth, float cellSize)
{
    OrientedRect rect;
    rect.origin = origin;
    rect.yaw = yaw;
    rect.width = width;
    rect.depth = depth;
    return SampleOrientedRectangleCells(rect, cellSize);
}
