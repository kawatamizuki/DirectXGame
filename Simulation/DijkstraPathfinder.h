#pragma once
#include "IPathfinder.h"

// IPathfinderのv1実装。
// ダイクストラ法(優先度キューを使った、2点間の最短距離を求める標準的なアルゴリズム)。
// 道路の交差点・端点を「駅」、道路区間を「駅と駅を結ぶ線路(重み=距離)」に見立てて、
// 始点から終点までの最も短い経路を求める。
class DijkstraPathfinder : public IPathfinder
{
public:
    std::optional<PathResult> FindPath(const RoadGraph& graph, int startNode, int endNode) const override;
};
