#include "RoutePlanner.h"
#include <cfloat>
#include <cmath>

using namespace DirectX;

namespace
{
    float DistanceBetween(const XMFLOAT3& a, const XMFLOAT3& b)
    {
        const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // 直前の点と同じ位置(ほぼ)なら足さない(長さ0の区間で、歩きが止まらないように)。
    void AppendPoint(std::vector<XMFLOAT3>& points, const XMFLOAT3& point)
    {
        if (!points.empty() && DistanceBetween(points.back(), point) < 1e-4f)
        {
            return;
        }
        points.push_back(point);
    }
}

RoutePlan PlanRoute(const RouteEndpoint& from, const RouteEndpoint& to, const std::vector<RoadSegment>& segments,
                    const RoadGraph& graph, const IPathfinder& pathfinder, PathCache& cache)
{
    RoutePlan plan;
    if (!from.hasRoadConnection || !to.hasRoadConnection || from.segmentIndex >= segments.size() || to.segmentIndex >= segments.size())
    {
        return plan;
    }

    std::vector<XMFLOAT3> middle;           // 2つの接続点の間(道路の上)の点
    std::vector<size_t> segmentIndices{ from.segmentIndex };

    if (from.segmentIndex != to.segmentIndex)
    {
        const RoadSegment& fromSegment = segments[from.segmentIndex];
        const RoadSegment& toSegment = segments[to.segmentIndex];
        const int fromNodes[2] = { graph.FindNearestNode(fromSegment.start), graph.FindNearestNode(fromSegment.end) };
        const int toNodes[2] = { graph.FindNearestNode(toSegment.start), graph.FindNearestNode(toSegment.end) };

        float bestCost = FLT_MAX;
        const PathResult* bestPath = nullptr;
        for (int a = 0; a < 2; ++a)
        {
            for (int b = 0; b < 2; ++b)
            {
                if (fromNodes[a] < 0 || toNodes[b] < 0)
                {
                    continue;
                }
                const std::pair<int, int> key{ fromNodes[a], toNodes[b] };
                auto it = cache.find(key);
                if (it == cache.end())
                {
                    it = cache.emplace(key, pathfinder.FindPath(graph, key.first, key.second)).first;
                }
                if (!it->second.has_value())
                {
                    continue;
                }
                const float cost = DistanceBetween(from.connectionPoint, graph.GetNodePosition(fromNodes[a]))
                    + it->second->totalCost
                    + DistanceBetween(graph.GetNodePosition(toNodes[b]), to.connectionPoint);
                if (cost < bestCost)
                {
                    bestCost = cost;
                    bestPath = &it->second.value();
                }
            }
        }
        if (bestPath == nullptr)
        {
            return plan; // 道路でつながっていない
        }
        middle = bestPath->waypoints;
        segmentIndices.insert(segmentIndices.end(), bestPath->segmentIndices.begin(), bestPath->segmentIndices.end());
        segmentIndices.push_back(to.segmentIndex);
    }
    // 同じ区間の時は、区間の上を直接歩く(グラフを通らない)。

    AppendPoint(plan.waypoints, from.position);
    AppendPoint(plan.waypoints, from.connectionPoint);
    for (const XMFLOAT3& point : middle)
    {
        AppendPoint(plan.waypoints, point);
    }
    AppendPoint(plan.waypoints, to.connectionPoint);
    AppendPoint(plan.waypoints, to.position);
    if (plan.waypoints.size() < 2)
    {
        plan.waypoints.push_back(to.position);
    }

    for (size_t i = 0; i + 1 < plan.waypoints.size(); ++i)
    {
        plan.length += DistanceBetween(plan.waypoints[i], plan.waypoints[i + 1]);
    }
    plan.segmentIndices = std::move(segmentIndices);
    plan.valid = true;
    return plan;
}
