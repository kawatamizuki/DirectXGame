#include "FrontageCells.h"
#include <algorithm>
#include <cmath>
#include "OrientedRectangleOverlap.h"

using namespace DirectX;

namespace
{
    constexpr float kMinPointDistance = 1e-4f;      // これより近い連続した点は、1つにまとめる
    constexpr float kMaxMiterScale = 1.5f;          // 縁を平行移動する時の、角での伸びの上限
    constexpr float kSagittaRatio = 0.125f;         // 内側の列を、縁から離す量(弦の高さ = 長さ^2 / 8 / 半径)
    constexpr float kOverlapEpsilonRatio = 0.01f;   // マス同士が「重なっている」とみなす貫入量(cellSizeに対する割合)
    constexpr float kSearchStepRatio = 0.02f;       // 重ならなくなるまで、先へずらす刻み(cellSizeに対する割合)
    constexpr float kMaxAdvanceRatio = 2.5f;        // 1つ前のマスから離す距離の上限(cellSizeに対する割合)

    // 中心から見た、yaw向きのマスの形(GridOrientationRegistry::MakeCellRectと同じ回転規約)。
    OrientedRect MakeRectAt(const XMFLOAT3& center, float yaw, float cellSize)
    {
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        const float h = cellSize * 0.5f;
        OrientedRect rect;
        rect.origin = XMFLOAT3(center.x - h * (c + s), center.y, center.z - h * (c - s));
        rect.yaw = yaw;
        rect.width = cellSize;
        rect.depth = cellSize;
        return rect;
    }

    struct Vec2 { float x = 0.0f; float z = 0.0f; };
    Vec2 Normalized(Vec2 v)
    {
        const float len = std::sqrt(v.x * v.x + v.z * v.z);
        if (len < 1e-6f) { return Vec2{}; }
        return Vec2{ v.x / len, v.z / len };
    }

    // 折れ線(縁を平行移動したもの)と、その上の弧の長さ。
    struct Polyline
    {
        std::vector<XMFLOAT3> points;
        std::vector<float> arc;     // pointsと同じ数。始点からの弧の長さ
        float total = 0.0f;
    };

    // 弧の長さaでの位置。
    XMFLOAT3 PositionAt(const Polyline& line, float a)
    {
        a = (std::max)(0.0f, (std::min)(a, line.total));
        size_t i = 0;
        while (i + 2 < line.points.size() && line.arc[i + 1] < a) { ++i; }
        const float span = line.arc[i + 1] - line.arc[i];
        const float t = span > 1e-9f ? (a - line.arc[i]) / span : 0.0f;
        const XMFLOAT3& p = line.points[i];
        const XMFLOAT3& q = line.points[i + 1];
        return XMFLOAT3(p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t);
    }

    // 弧の長さaでの向き(前後に少し離した2点を結ぶ。折れ線の角で、向きが跳ばないように)。
    float YawAt(const Polyline& line, float a, float halfWindow)
    {
        const XMFLOAT3 p = PositionAt(line, a - halfWindow);
        const XMFLOAT3 q = PositionAt(line, a + halfWindow);
        return std::atan2(q.x - p.x, q.z - p.z);
    }

    // 片側(side=+1が、ローカル+X側 = (cos,-sin)の側 / -1が反対側)の、縁に接する列の中心を通る折れ線を作る。
    Polyline BuildOffsetLine(const std::vector<XMFLOAT3>& pts, float baseOffset, float cellSize, float side)
    {
        const size_t n = pts.size();
        std::vector<Vec2> dir(n - 1);
        for (size_t i = 0; i + 1 < n; ++i) { dir[i] = Normalized(Vec2{ pts[i + 1].x - pts[i].x, pts[i + 1].z - pts[i].z }); }

        Polyline line;
        line.points.reserve(n);
        for (size_t i = 0; i < n; ++i)
        {
            const Vec2 a = dir[i == 0 ? 0 : i - 1];
            const Vec2 b = dir[i == n - 1 ? n - 2 : i];
            Vec2 tangent = Normalized(Vec2{ a.x + b.x, a.z + b.z });
            if (tangent.x == 0.0f && tangent.z == 0.0f) { tangent = b; }
            // ローカル+Xは(cos,-sin)。向き(sin,cos)から見ると(z,-x)。
            const Vec2 normal{ tangent.z * side, -tangent.x * side };

            // 曲がりが、この側へ向かっているか(=この側が内側か)。内側なら、弦の分だけ縁から離す。
            float extra = 0.0f;
            if (i != 0 && i != n - 1)
            {
                const float toward = (b.x - a.x) * normal.x + (b.z - a.z) * normal.z;
                if (toward > 0.0f)
                {
                    const float turn = std::acos((std::max)(-1.0f, (std::min)(1.0f, a.x * b.x + a.z * b.z)));
                    const float len = 0.5f * (std::sqrt((pts[i].x - pts[i - 1].x) * (pts[i].x - pts[i - 1].x) + (pts[i].z - pts[i - 1].z) * (pts[i].z - pts[i - 1].z))
                                            + std::sqrt((pts[i + 1].x - pts[i].x) * (pts[i + 1].x - pts[i].x) + (pts[i + 1].z - pts[i].z) * (pts[i + 1].z - pts[i].z)));
                    const float curvature = len > 1e-6f ? turn / len : 0.0f;
                    extra = kSagittaRatio * cellSize * cellSize * curvature;
                }
            }

            // 角では、平行移動した2本の縁の交点(ミター)へ。伸びすぎないよう上限を付ける。
            const float cosHalf = (std::max)(0.01f, tangent.x * b.x + tangent.z * b.z);
            const float scale = (std::min)(kMaxMiterScale, 1.0f / cosHalf);
            const float dist = (baseOffset + extra) * scale;
            line.points.push_back(XMFLOAT3(pts[i].x + normal.x * dist, pts[i].y, pts[i].z + normal.z * dist));
        }

        line.arc.assign(n, 0.0f);
        for (size_t i = 1; i < n; ++i)
        {
            const float dx = line.points[i].x - line.points[i - 1].x;
            const float dz = line.points[i].z - line.points[i - 1].z;
            line.arc[i] = line.arc[i - 1] + std::sqrt(dx * dx + dz * dz);
        }
        line.total = line.arc[n - 1];
        return line;
    }

    // 1つの列を、隙間なく・重ならずに、順に並べる。
    void PlaceAlong(const Polyline& line, float cellSize, std::vector<FrontageCell>& out)
    {
        if (line.total < 1e-6f) { return; }
        const float halfWindow = cellSize * 0.5f;
        const float epsilon = cellSize * kOverlapEpsilonRatio;

        float a = 0.0f;
        FrontageCell prev;
        prev.center = PositionAt(line, a);
        prev.yaw = YawAt(line, a, halfWindow);
        out.push_back(prev);

        // 1つ前のマスから、cellSizeだけ先に置くのが基本。曲がっていて重なる時だけ、重ならなくなるまで先へずらす。
        // (直線では、ずらさないので、ちょうどcellSizeおきになる)
        while (true)
        {
            float next = a + cellSize;
            FrontageCell cell;
            const OrientedRect prevRect = MakeRectAt(prev.center, prev.yaw, cellSize);
            const float limit = a + cellSize * kMaxAdvanceRatio;
            while (true)
            {
                cell.center = PositionAt(line, next);
                cell.yaw = YawAt(line, next, halfWindow);
                if (!OrientedRectsOverlap(prevRect, MakeRectAt(cell.center, cell.yaw, cellSize), epsilon) || next >= limit) { break; }
                next += cellSize * kSearchStepRatio;
            }
            if (next > line.total + 1e-4f) { break; }
            a = next;
            prev = cell;
            out.push_back(cell);
        }
    }
}

std::vector<FrontageCell> BuildFrontageCells(const std::vector<XMFLOAT3>& centerline, float roadWidth, float cellSize)
{
    std::vector<FrontageCell> result;
    if (cellSize <= 0.0f) { return result; }

    std::vector<XMFLOAT3> pts;
    pts.reserve(centerline.size());
    for (const XMFLOAT3& p : centerline)
    {
        if (pts.empty() || std::abs(p.x - pts.back().x) + std::abs(p.z - pts.back().z) > kMinPointDistance) { pts.push_back(p); }
    }
    if (pts.size() < 2) { return result; }

    // 道の縁に接する最初の列の番号(幅1.0以下なら1、Large(1.6)なら2)。列0は道そのもの。
    int sideColumn = static_cast<int>(std::ceil((roadWidth * 0.5f + cellSize * 0.5f) / cellSize - 0.0001f));
    if (sideColumn < 1) { sideColumn = 1; }
    const float baseOffset = static_cast<float>(sideColumn) * cellSize;

    for (float side : { 1.0f, -1.0f })
    {
        PlaceAlong(BuildOffsetLine(pts, baseOffset, cellSize, side), cellSize, result);
    }
    return result;
}
