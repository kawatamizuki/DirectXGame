#include "RoadCutting.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include "WorldScale.h"

using namespace DirectX;

namespace
{
    constexpr float kEpsilon = 1e-4f;       // 長さの比較の許容(ワールド単位)

    struct Interval
    {
        float a = 0.0f;
        float b = 0.0f;
    };

    float Distance(const XMFLOAT3& p, const XMFLOAT3& q)
    {
        const float dx = p.x - q.x;
        const float dz = p.z - q.z;
        return std::sqrt(dx * dx + dz * dz);
    }
}

RoadCutWindow MakeSliceWindow(const RoadSegment& segment, const XMFLOAT3& point, float sliceLength,
                              float startExtension, float endExtension, int& sliceIndexOut)
{
    const float dx = segment.end.x - segment.start.x;
    const float dz = segment.end.z - segment.start.z;
    const float length = std::sqrt(dx * dx + dz * dz);

    RoadCutWindow window;
    window.halfLength = sliceLength * 0.5f;
    window.halfWidth = segment.width * 0.5f;
    sliceIndexOut = 0;
    if (length < kEpsilon || !(sliceLength > 0.0f))
    {
        // 長さのない区間は、始点を中心にした四角にする(向きは、適当に+Z)。
        window.center = segment.start;
        window.dirX = 0.0f;
        window.dirZ = 1.0f;
        return window;
    }

    window.dirX = dx / length;
    window.dirZ = dz / length;
    float along = (point.x - segment.start.x) * window.dirX + (point.z - segment.start.z) * window.dirZ;

    // 境目は 始点 + m*sliceLength。pointが入るスライスの番号m(延長の部分は、端のスライスに入れる)。
    // 最後のスライスは、長さが半端でも、1つ数える。
    const int lastIndex = (std::max)(0, static_cast<int>(std::ceil(length / sliceLength - 1e-4f)) - 1);
    int index = static_cast<int>(std::floor(along / sliceLength + 1e-4f));
    index = (std::max)(0, (std::min)(index, lastIndex));
    sliceIndexOut = index;

    float low = static_cast<float>(index) * sliceLength;
    float high = static_cast<float>(index + 1) * sliceLength;
    if (index == 0) { low -= (std::max)(0.0f, startExtension); }
    if (index == lastIndex) { high = (std::max)(high, length + (std::max)(0.0f, endExtension)); }

    const float centerAlong = (low + high) * 0.5f;
    window.halfLength = (high - low) * 0.5f;
    window.center = XMFLOAT3(segment.start.x + window.dirX * centerAlong, segment.start.y, segment.start.z + window.dirZ * centerAlong);
    return window;
}

bool ClipLineToWindow(const XMFLOAT3& a, const XMFLOAT3& b, const RoadCutWindow& window, float& distanceStart, float& distanceEnd)
{
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    const float length = std::sqrt(dx * dx + dz * dz);
    if (length < kEpsilon)
    {
        return false;
    }

    // 四角の座標系: u=道路に沿った向き、v=それに直角(dirZ, -dirX)。
    const float perpX = window.dirZ;
    const float perpZ = -window.dirX;
    const float ax = a.x - window.center.x;
    const float az = a.z - window.center.z;
    const float au = ax * window.dirX + az * window.dirZ;
    const float av = ax * perpX + az * perpZ;
    const float du = (dx * window.dirX + dz * window.dirZ);
    const float dv = (dx * perpX + dz * perpZ);

    // Liang-Barsky: 線 p(t) = a + (b-a) t (t=0〜1) を、|u|<=halfLength, |v|<=halfWidth で切る。
    float t0 = 0.0f;
    float t1 = 1.0f;
    auto clip = [&](float p, float q, bool touchingCounts) -> bool
    {
        // 条件: p*t <= q
        if (std::fabs(p) < 1e-9f)
        {
            // 平行。内側なら、そのまま。外側なら、交わらない。辺の上(触れるだけ)は、touchingCountsで決める:
            //  ・道路に沿った辺(幅の端)の上を通る線(隣の道路の中心線など)は、交わらない。
            //  ・道路に直角な辺(スライスの境目)の上を通る線(交わる道路の中心線)は、交わる(隣り合う2つのスライスの、どちらを選んでも、その道路の該当部分が切れる)。
            return touchingCounts ? (q >= -1e-6f) : (q > 1e-6f);
        }
        const float r = q / p;
        if (p < 0.0f)
        {
            if (r > t1) { return false; }
            if (r > t0) { t0 = r; }
        }
        else
        {
            if (r < t0) { return false; }
            if (r < t1) { t1 = r; }
        }
        return true;
    };

    if (!clip(-du, au + window.halfLength, true) || !clip(du, window.halfLength - au, true) ||
        !clip(-dv, av + window.halfWidth, false) || !clip(dv, window.halfWidth - av, false))
    {
        return false;
    }
    if ((t1 - t0) * length < kEpsilon)
    {
        return false; // 辺に触れるだけ・点だけ
    }
    distanceStart = t0 * length;
    distanceEnd = t1 * length;
    return true;
}

RoadCutResult CutRoads(const std::vector<RoadSegment>& segments, const std::vector<RoadCutWindow>& windows, float minPieceLength,
                       float isolatedMinPieceLength)
{
    RoadCutResult result;
    result.segments.reserve(segments.size());
    std::vector<char> fromCut;      // result.segmentsと同じ並び。切られた区間から出てきた切れ端か(切られなかった区間は、false)

    for (const RoadSegment& segment : segments)
    {
        const float length = Distance(segment.start, segment.end);

        // この区間に当たる範囲を、全てのwindowから集める。
        std::vector<Interval> hits;
        if (length >= kEpsilon)
        {
            const float segMinX = (std::min)(segment.start.x, segment.end.x);
            const float segMaxX = (std::max)(segment.start.x, segment.end.x);
            const float segMinZ = (std::min)(segment.start.z, segment.end.z);
            const float segMaxZ = (std::max)(segment.start.z, segment.end.z);
            for (const RoadCutWindow& window : windows)
            {
                // 窓を囲む円と、区間を囲む四角が、離れていれば、当たらない(ClipLineToWindowの前に、安く除く)。
                const float radius = std::sqrt(window.halfLength * window.halfLength + window.halfWidth * window.halfWidth) + kEpsilon;
                if (window.center.x + radius < segMinX || window.center.x - radius > segMaxX ||
                    window.center.z + radius < segMinZ || window.center.z - radius > segMaxZ)
                {
                    continue;
                }
                Interval hit;
                if (ClipLineToWindow(segment.start, segment.end, window, hit.a, hit.b))
                {
                    hits.push_back(hit);
                }
            }
        }
        if (hits.empty())
        {
            result.segments.push_back(segment); // 切られない区間は、そのまま
            fromCut.push_back(0);
            continue;
        }

        // 重なる・つながる範囲を1つにまとめる。
        std::sort(hits.begin(), hits.end(), [](const Interval& l, const Interval& r) { return l.a < r.a; });
        std::vector<Interval> merged;
        for (const Interval& hit : hits)
        {
            if (!merged.empty() && hit.a <= merged.back().b + kEpsilon)
            {
                merged.back().b = (std::max)(merged.back().b, hit.b);
            }
            else
            {
                merged.push_back(hit);
            }
        }

        // 残る部分(範囲と範囲の間)。短すぎる端切れは、捨てる。
        std::vector<Interval> kept;
        float cursor = 0.0f;
        for (const Interval& cut : merged)
        {
            if (cut.a - cursor > kEpsilon) { kept.push_back({ cursor, cut.a }); }
            cursor = (std::max)(cursor, cut.b);
        }
        if (length - cursor > kEpsilon) { kept.push_back({ cursor, length }); }
        kept.erase(std::remove_if(kept.begin(), kept.end(), [&](const Interval& piece)
        {
            const float pieceLength = piece.b - piece.a;
            if (pieceLength < minPieceLength) { return true; }
            // 両端とも切り口(元の端を含まない)の、孤立した短い切れ端は、捨てる。
            const bool isolated = (piece.a > kEpsilon) && (piece.b < length - kEpsilon);
            return isolated && pieceLength < isolatedMinPieceLength;
        }), kept.end());

        // 残った部分を、新しい区間にする。切った側の端は、平ら。切っていない側の端は、元の点・元の印のまま。
        const float inverse = 1.0f / length;
        const float dirX = (segment.end.x - segment.start.x) * inverse;
        const float dirY = (segment.end.y - segment.start.y) * inverse;
        const float dirZ = (segment.end.z - segment.start.z) * inverse;
        for (const Interval& piece : kept)
        {
            RoadSegment out = segment;
            out.debugLoad = 0.0f;
            const bool startsAtOriginal = piece.a <= kEpsilon;
            const bool endsAtOriginal = piece.b >= length - kEpsilon;
            if (!startsAtOriginal)
            {
                out.start = XMFLOAT3(segment.start.x + dirX * piece.a, segment.start.y + dirY * piece.a, segment.start.z + dirZ * piece.a);
                out.flatStart = true;
            }
            if (!endsAtOriginal)
            {
                out.end = XMFLOAT3(segment.start.x + dirX * piece.b, segment.start.y + dirY * piece.b, segment.start.z + dirZ * piece.b);
                out.flatEnd = true;
            }
            result.segments.push_back(out);
            fromCut.push_back(1);
        }

        // 取り除いた部分(残る部分の、間と外側)。
        float removedCursor = 0.0f;
        auto addRemoved = [&](float from, float to)
        {
            if (to - from <= kEpsilon) { return; }
            RemovedRoadSpan span;
            span.start = XMFLOAT3(segment.start.x + dirX * from, segment.start.y + dirY * from, segment.start.z + dirZ * from);
            span.end = XMFLOAT3(segment.start.x + dirX * to, segment.start.y + dirY * to, segment.start.z + dirZ * to);
            span.width = segment.width;
            span.type = segment.type;
            result.removed.push_back(span);
            result.removedLength += (to - from);
        };
        for (const Interval& piece : kept)
        {
            addRemoved(removedCursor, piece.a);
            removedCursor = piece.b;
        }
        addRemoved(removedCursor, length);
        result.changed = true;
    }

    // 切った跡で、両端とも、どこにもつながらない(行き止まり)になった、短い切れ端は、捨てる。
    // (隣の区間の一部を切ったために、つなぎ目が、行き止まりになり、間に、短い切れ端が、孤立して残ることがある。自由描きの区間の連なりで、起きる)
    // 切られた区間から出てきた切れ端だけが対象(最初から、孤立していた、短い道は、触らない)。
    if (result.changed && isolatedMinPieceLength > 0.0f)
    {
        using Key = std::pair<long long, long long>;
        auto keyOf = [](const XMFLOAT3& p) { return Key(std::llround(static_cast<double>(p.x) * 10000.0), std::llround(static_cast<double>(p.z) * 10000.0)); };
        std::map<Key, int> degree;
        for (const RoadSegment& s : result.segments) { ++degree[keyOf(s.start)]; ++degree[keyOf(s.end)]; }
        std::vector<RoadSegment> kept;
        kept.reserve(result.segments.size());
        for (size_t i = 0; i < result.segments.size(); ++i)
        {
            const RoadSegment& s = result.segments[i];
            const float pieceLength = Distance(s.start, s.end);
            if (fromCut[i] && pieceLength < isolatedMinPieceLength && degree[keyOf(s.start)] == 1 && degree[keyOf(s.end)] == 1)
            {
                RemovedRoadSpan span;
                span.start = s.start;
                span.end = s.end;
                span.width = s.width;
                span.type = s.type;
                result.removed.push_back(span);
                result.removedLength += pieceLength;
                continue;
            }
            kept.push_back(s);
        }
        result.segments = std::move(kept);
    }

    // 切った跡で、「交差点・区間のつなぎ目だった点」が、行き止まり(1本だけ)になった端は、平らにする。
    // そのままだと、行き止まりの見た目の延長(0.5)が、隣の、切り取った隙間を、見た目だけ、塞いでしまう
    // (例: 区間Aの終わりの1スライスを切ると、続きの区間Bの始点が、行き止まりになり、延長で、隙間が消える)。
    if (result.changed)
    {
        using Key = std::pair<long long, long long>;
        auto keyOf = [](const XMFLOAT3& p) { return Key(std::llround(static_cast<double>(p.x) * 10000.0), std::llround(static_cast<double>(p.z) * 10000.0)); };
        std::map<Key, int> before;
        std::map<Key, int> after;
        for (const RoadSegment& s : segments) { ++before[keyOf(s.start)]; ++before[keyOf(s.end)]; }
        for (const RoadSegment& s : result.segments) { ++after[keyOf(s.start)]; ++after[keyOf(s.end)]; }
        for (RoadSegment& s : result.segments)
        {
            if (!s.flatStart && before[keyOf(s.start)] >= 2 && after[keyOf(s.start)] == 1) { s.flatStart = true; }
            if (!s.flatEnd && before[keyOf(s.end)] >= 2 && after[keyOf(s.end)] == 1) { s.flatEnd = true; }
        }
    }
    return result;
}


int CalculateRoadDemolitionCost(const std::vector<RemovedRoadSpan>& removed)
{
    double yen = 0.0;
    double totalLength = 0.0;
    for (const RemovedRoadSpan& span : removed)
    {
        const double length = Distance(span.start, span.end);
        totalLength += length;
        yen += length * static_cast<double>(GetRoadTypeDefinition(span.type).costPerCell) / static_cast<double>(WorldScale::kRoadPriceCellLength)
             * static_cast<double>(kRoadDemolitionCostRatio);
    }
    if (totalLength <= static_cast<double>(kEpsilon))
    {
        return 0;
    }
    // 浮動小数の誤差で、ちょうどの金額が1円増えないよう、少しだけ引いてから切り上げる。
    const int cost = static_cast<int>(std::ceil(yen - 1e-6));
    return (std::max)(kMinimumRoadDemolitionCost, cost);
}
