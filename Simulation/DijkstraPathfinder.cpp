#include <cfloat>
#include <queue>
#include <algorithm>
#include "DijkstraPathfinder.h"

using namespace DirectX;

std::optional<PathResult> DijkstraPathfinder::FindPath(const RoadGraph& graph, int startNode, int endNode) const
{
    int nodeCount = graph.GetNodeCount();
    if (startNode < 0 || endNode < 0 || startNode >= nodeCount || endNode >= nodeCount)
    {
        return std::nullopt;
    }

    if (startNode == endNode)
    {
        PathResult result;
        result.waypoints.push_back(graph.GetNodePosition(startNode));
        result.totalCost = 0.0f;
        return result;
    }

    // 各ノードへの最短距離、経路復元用に「どのノードからどの辺で来たか」を記録する。
    std::vector<float> dist(nodeCount, FLT_MAX);
    std::vector<int> prevNode(nodeCount, -1);
    std::vector<int> prevEdge(nodeCount, -1);
    std::vector<bool> visited(nodeCount, false);

    using QueueItem = std::pair<float, int>; // (現在地までの距離, ノードid)
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;

    dist[startNode] = 0.0f;
    queue.push({ 0.0f, startNode });

    while (!queue.empty())
    {
        float d = queue.top().first;
        int node = queue.top().second;
        queue.pop();

        if (visited[node])
        {
            continue;
        }
        visited[node] = true;

        if (node == endNode)
        {
            break;
        }

        for (int edgeIndex : graph.GetAdjacentEdgeIndices(node))
        {
            const RoadGraphEdge& edge = graph.GetEdge(edgeIndex);
            int other = (edge.nodeA == node) ? edge.nodeB : edge.nodeA;

            float newDist = d + edge.weight;
            if (newDist < dist[other])
            {
                dist[other] = newDist;
                prevNode[other] = node;
                prevEdge[other] = edgeIndex;
                queue.push({ newDist, other });
            }
        }
    }

    if (dist[endNode] >= FLT_MAX)
    {
        // 経路が存在しない(道路網が繋がっていない)
        return std::nullopt;
    }

    // endNodeからstartNodeへ逆順にたどって経路を復元する
    std::vector<int> nodePath;
    std::vector<size_t> segmentIndices;

    int current = endNode;
    while (current != startNode)
    {
        nodePath.push_back(current);
        segmentIndices.push_back(graph.GetEdge(prevEdge[current]).segmentIndex);
        current = prevNode[current];
    }
    nodePath.push_back(startNode);

    std::reverse(nodePath.begin(), nodePath.end());
    std::reverse(segmentIndices.begin(), segmentIndices.end());

    PathResult result;
    result.totalCost = dist[endNode];
    result.segmentIndices = std::move(segmentIndices);
    result.waypoints.reserve(nodePath.size());
    for (int nodeId : nodePath)
    {
        result.waypoints.push_back(graph.GetNodePosition(nodeId));
    }

    return result;
}
