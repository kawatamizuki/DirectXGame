#include <cfloat>
#include "RoadGraph.h"
#include "Profiler.h"

using namespace DirectX;

namespace
{
    // 同一ノードとみなす距離のしきい値(道路の端点吸着はほぼ厳密一致するはずだが、念のため誤差を許容する)。
    constexpr float kSameNodeEpsilon = 0.001f;

    // pointと一致する既存ノードを探す。無ければ新規追加してそのidを返す。
    int FindOrAddNode(std::vector<XMFLOAT3>& nodePositions, const XMFLOAT3& point)
    {
        XMVECTOR pointVec = XMLoadFloat3(&point);

        for (size_t i = 0; i < nodePositions.size(); ++i)
        {
            XMVECTOR existing = XMLoadFloat3(&nodePositions[i]);
            float distSq = XMVectorGetX(XMVector3LengthSq(existing - pointVec));
            if (distSq <= kSameNodeEpsilon * kSameNodeEpsilon)
            {
                return static_cast<int>(i);
            }
        }

        nodePositions.push_back(point);
        return static_cast<int>(nodePositions.size() - 1);
    }
}

RoadGraph RoadGraph::BuildFromSegments(const std::vector<RoadSegment>& segments)
{
    PROFILE_SCOPE("RoadGraph::BuildFromSegments");

    RoadGraph graph;

    for (size_t i = 0; i < segments.size(); ++i)
    {
        const RoadSegment& segment = segments[i];

        int nodeA = FindOrAddNode(graph.m_nodePositions, segment.start);
        int nodeB = FindOrAddNode(graph.m_nodePositions, segment.end);

        XMVECTOR startVec = XMLoadFloat3(&segment.start);
        XMVECTOR endVec = XMLoadFloat3(&segment.end);
        float length = XMVectorGetX(XMVector3Length(endVec - startVec));

        RoadGraphEdge edge{ nodeA, nodeB, length, i };
        graph.m_edges.push_back(edge);
    }

    // ノード数が確定してから隣接リストを組み立てる
    graph.m_adjacencyEdgeIndices.resize(graph.m_nodePositions.size());
    for (size_t edgeIndex = 0; edgeIndex < graph.m_edges.size(); ++edgeIndex)
    {
        const RoadGraphEdge& edge = graph.m_edges[edgeIndex];
        graph.m_adjacencyEdgeIndices[edge.nodeA].push_back(static_cast<int>(edgeIndex));
        graph.m_adjacencyEdgeIndices[edge.nodeB].push_back(static_cast<int>(edgeIndex));
    }

    return graph;
}

int RoadGraph::FindNearestNode(const XMFLOAT3& worldPos) const
{
    if (m_nodePositions.empty())
    {
        return -1;
    }

    XMVECTOR posVec = XMLoadFloat3(&worldPos);

    int bestNode = 0;
    float bestDistSq = FLT_MAX;

    for (size_t i = 0; i < m_nodePositions.size(); ++i)
    {
        XMVECTOR nodeVec = XMLoadFloat3(&m_nodePositions[i]);
        float distSq = XMVectorGetX(XMVector3LengthSq(nodeVec - posVec));
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestNode = static_cast<int>(i);
        }
    }

    return bestNode;
}
