#include "CurveSmoothing.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace
{
    constexpr float kSamePointEpsilon = 1e-4f;

    float Distance(const XMFLOAT3& a, const XMFLOAT3& b)
    {
        const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // 折れ線(点列)の、累積の長さ。cumulative[i] = 先頭からi番目の点までの長さ。
    std::vector<float> CumulativeLength(const std::vector<XMFLOAT3>& points)
    {
        std::vector<float> cumulative(points.size(), 0.0f);
        for (size_t i = 1; i < points.size(); ++i)
        {
            cumulative[i] = cumulative[i - 1] + Distance(points[i - 1], points[i]);
        }
        return cumulative;
    }

    // 折れ線の、先頭から長さdistanceの位置の点(範囲外は、端に丸める)。cursorは、探す位置の目印(増える方向に使い回す)。
    XMFLOAT3 PointAtLength(const std::vector<XMFLOAT3>& points, const std::vector<float>& cumulative, float distance, size_t& cursor)
    {
        if (distance <= 0.0f) { return points.front(); }
        if (distance >= cumulative.back()) { return points.back(); }
        while (cursor + 1 < cumulative.size() && cumulative[cursor + 1] < distance) { ++cursor; }
        const float segmentLength = cumulative[cursor + 1] - cumulative[cursor];
        const float t = segmentLength > 1e-9f ? (distance - cumulative[cursor]) / segmentLength : 0.0f;
        const XMFLOAT3& a = points[cursor];
        const XMFLOAT3& b = points[cursor + 1];
        return XMFLOAT3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
    }

    // 点列を、一定の間隔stepで取り直す(最後の点は、必ず、元の最後の点)。
    std::vector<XMFLOAT3> ResampleByStep(const std::vector<XMFLOAT3>& points, float step)
    {
        const std::vector<float> cumulative = CumulativeLength(points);
        const float total = cumulative.back();
        const int intervals = (std::max)(1, static_cast<int>(std::lround(total / step)));
        std::vector<XMFLOAT3> result;
        result.reserve(static_cast<size_t>(intervals) + 1);
        size_t cursor = 0;
        for (int i = 0; i <= intervals; ++i)
        {
            const float d = total * static_cast<float>(i) / static_cast<float>(intervals);
            result.push_back(PointAtLength(points, cumulative, d, cursor));
        }
        result.front() = points.front();
        result.back() = points.back();
        return result;
    }
}

std::vector<XMFLOAT3> SmoothAndResample(const std::vector<XMFLOAT3>& input, const CurveSmoothingOptions& options)
{
    // 同じ位置が続く点を、1つにする。
    std::vector<XMFLOAT3> points;
    points.reserve(input.size());
    for (const XMFLOAT3& p : input)
    {
        if (points.empty() || Distance(points.back(), p) > kSamePointEpsilon)
        {
            points.push_back(p);
        }
    }
    if (points.size() < 2)
    {
        return input;
    }
    const float total = CumulativeLength(points).back();
    const float spacing = (std::max)(0.05f, options.spacing);
    if (total < spacing * 0.25f)
    {
        return points; // ほとんど動いていない(短すぎて、均す意味が無い)
    }

    // 1. 細かい等間隔の点に取り直す(均しの計算を、間隔の揺れに左右されないようにする)。
    const float fineStep = (std::min)(spacing * 0.25f, (std::max)(0.02f, options.sigma * 0.5f > 0.0f ? options.sigma * 0.5f : spacing * 0.25f));
    std::vector<XMFLOAT3> fine = ResampleByStep(points, fineStep);
    const int n = static_cast<int>(fine.size());
    const float actualFineStep = total / static_cast<float>((std::max)(1, n - 1));

    // 2. ガウスの重みの移動平均。端の外側は、端を中心とした点対称(2*端 - 内側の点)に延ばす(端の位置と向きが、保たれる)。
    std::vector<XMFLOAT3> smoothed = fine;
    if (options.sigma > 1e-4f && n >= 3)
    {
        const int radius = (std::max)(1, static_cast<int>(std::ceil(3.0f * options.sigma / actualFineStep)));
        std::vector<float> weights(static_cast<size_t>(radius) + 1);
        float weightSum = 0.0f;
        for (int k = 0; k <= radius; ++k)
        {
            const float d = static_cast<float>(k) * actualFineStep;
            weights[static_cast<size_t>(k)] = std::exp(-0.5f * d * d / (options.sigma * options.sigma));
            weightSum += (k == 0 ? 1.0f : 2.0f) * weights[static_cast<size_t>(k)];
        }
        auto sample = [&](int index) -> XMFLOAT3
        {
            if (index < 0)
            {
                const int m = (std::min)(-index, n - 1);
                const XMFLOAT3& e = fine.front(); const XMFLOAT3& q = fine[static_cast<size_t>(m)];
                return XMFLOAT3(2.0f * e.x - q.x, 2.0f * e.y - q.y, 2.0f * e.z - q.z);
            }
            if (index >= n)
            {
                const int m = (std::min)(index - (n - 1), n - 1);
                const XMFLOAT3& e = fine.back(); const XMFLOAT3& q = fine[static_cast<size_t>(n - 1 - m)];
                return XMFLOAT3(2.0f * e.x - q.x, 2.0f * e.y - q.y, 2.0f * e.z - q.z);
            }
            return fine[static_cast<size_t>(index)];
        };
        for (int i = 1; i + 1 < n; ++i)
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            for (int k = -radius; k <= radius; ++k)
            {
                const float w = weights[static_cast<size_t>(k < 0 ? -k : k)];
                const XMFLOAT3 p = sample(i + k);
                x += p.x * w; y += p.y * w; z += p.z * w;
            }
            smoothed[static_cast<size_t>(i)] = XMFLOAT3(x / weightSum, y / weightSum, z / weightSum);
        }
    }

    // 3. 出力の間隔で、取り直す。始点・終点は、入力と同じ。
    std::vector<XMFLOAT3> result = ResampleByStep(smoothed, spacing);
    result.front() = input.front();
    result.back() = input.back();
    return result;
}
