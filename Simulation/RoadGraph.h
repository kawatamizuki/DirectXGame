#pragma once
#include <vector>
#include <DirectXMath.h>
#include "RoadSegment.h"

// グラフの1本の辺(=1つの道路区間に対応)。
struct RoadGraphEdge
{
    int nodeA;             // 端点ノードA
    int nodeB;             // 端点ノードB
    float weight;          // コスト(v1は区間の長さ)
    size_t segmentIndex;   // 元になったRoadSegmentの添字(debugLoad加算・見た目用)
};

// 道路区間の集まりを、交差点/端点をノードとするグラフとして表現するクラス。
// 経路探索アルゴリズムそのものはIPathfinderに分離してあるので、
// このクラスはデータと基本的な問い合わせだけを持つ(将来アルゴリズムを差し替えられるように)。
class RoadGraph
{
public:
    // segmentsからグラフを組み立てる。
    // 各区間のstart/endを、既存ノードと誤差1mm以内なら再利用し、無ければ新規ノードを作る。
    static RoadGraph BuildFromSegments(const std::vector<RoadSegment>& segments);

    // worldPosに一番近いノードのidを返す(線形探索)。ノードが1つも無ければ-1。
    int FindNearestNode(const DirectX::XMFLOAT3& worldPos) const;

    const DirectX::XMFLOAT3& GetNodePosition(int nodeId) const { return m_nodePositions[nodeId]; }
    int GetNodeCount() const { return static_cast<int>(m_nodePositions.size()); }
    size_t GetEdgeCount() const { return m_edges.size(); }

    // nodeIdに繋がっている辺の添字一覧(m_edgesへの添字)を返す。
    const std::vector<int>& GetAdjacentEdgeIndices(int nodeId) const { return m_adjacencyEdgeIndices[nodeId]; }
    const RoadGraphEdge& GetEdge(int edgeIndex) const { return m_edges[edgeIndex]; }

private:
    std::vector<DirectX::XMFLOAT3> m_nodePositions;
    std::vector<RoadGraphEdge> m_edges;
    std::vector<std::vector<int>> m_adjacencyEdgeIndices; // ノードごとの隣接エッジ添字一覧
};
