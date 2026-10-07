#pragma once
#include <vector>
#include <optional>
#include <DirectXMath.h>
#include "RoadGraph.h"

// RoadGraph上での経路探索の結果。
struct PathResult
{
    std::vector<DirectX::XMFLOAT3> waypoints; // start→endの順のノード座標列
    std::vector<size_t> segmentIndices;       // 通過した道路区間(debugLoad加算用)
    float totalCost = 0.0f;                   // 経路の総コスト(v1では総距離)
};

// RoadGraph上で2ノード間の経路を探すアルゴリズムのインターフェース。
// 「最短距離を求める」以外にも、将来「商店街を優先的に通る経路」のような
// 目的別のアルゴリズムに差し替えられるようにする。
class IPathfinder
{
public:
    virtual ~IPathfinder() = default;

    // startNodeからendNodeへの経路を探す。見つからなければstd::nullopt。
    virtual std::optional<PathResult> FindPath(const RoadGraph& graph, int startNode, int endNode) const = 0;
};
