#pragma once
#include <map>
#include <optional>
#include <utility>
#include <vector>
#include <DirectXMath.h>
#include "IPathfinder.h"
#include "RoadGraph.h"
#include "RoadSegment.h"

// 建物から建物への、歩く経路を作る。
// 道路のグラフのノードは、道路区間の端点だけ。建物が区間の途中に面している時、端点へ直接歩くと、道路を斜めに横切ってしまう。
// そこで、経路は「建物 → 道路上の接続点(道路に垂直) → 道路に沿って → 道路上の接続点 → 建物」にする。
// 同じ区間に面している2つの建物は、区間の上を直接歩く。違う区間なら、両方の区間の、どちらの端点から入るか(2x2通り)のうち、
// 道のりが一番短い組み合わせを使う。画面・ゲームを知らない(数値と道路だけで決まる)ので、単体テストできる。

// 経路の片方の端(建物)の情報。
struct RouteEndpoint
{
    DirectX::XMFLOAT3 position{};           // 建物の位置(歩き始め・終わり)
    DirectX::XMFLOAT3 connectionPoint{};    // 建物から一番近い、道路上の点
    size_t segmentIndex = 0;                // その点がある道路区間の添字
    bool hasRoadConnection = false;         // 道路に接続しているか(falseなら、経路は作れない)
};

struct RoutePlan
{
    bool valid = false;
    float length = 0.0f;                    // 歩く道のり(建物の位置から建物の位置まで)
    std::vector<DirectX::XMFLOAT3> waypoints;   // from→toの順(最初=fromの位置、最後=toの位置)
    std::vector<size_t> segmentIndices;     // 通る道路区間(混雑表示用)
};

// (始点ノード, 終点ノード) → 経路 の使い回し(1回の判定の中だけ。道路が変わったら捨てる)。
using PathCache = std::map<std::pair<int, int>, std::optional<PathResult>>;

RoutePlan PlanRoute(const RouteEndpoint& from, const RouteEndpoint& to, const std::vector<RoadSegment>& segments,
                    const RoadGraph& graph, const IPathfinder& pathfinder, PathCache& cache);
