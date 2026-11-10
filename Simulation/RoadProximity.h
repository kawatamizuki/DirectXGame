#pragma once
#include <vector>
#include <optional>
#include <DirectXMath.h>
#include "RoadSegment.h"
#include "OrientedRectangleOverlap.h"

// 道路区間上の最近接点の情報。
struct NearestRoadPoint
{
    DirectX::XMFLOAT3 point; // 区間上の最近接点(ワールド座標)
    size_t segmentIndex;     // その区間のsegments内での添字
    float distance;          // queryPointからの距離
};

// queryPointから最も近い、道路区間上の点を探す(区間の内部・端点いずれも対象)。
// maxDistance(+区間ごとの道幅×widthFactor)以内に見つからなければstd::nullopt。
// widthFactorは既定0(道幅を考慮しない、従来通り)。DemandSystemの建物-道路接続判定は
// 挙動を変えないため常に既定値のまま使う。RoadSystemの道路配置(カーブモードのみ)は
// 0.5を渡し、太い道路の面の上に居ても吸着できるようにする。
// 建物-道路の接続判定(DemandSystem)と、道路の途中への分岐(RoadSystem)の両方で使う共通ロジック。
std::optional<NearestRoadPoint> FindNearestPointOnRoad(
    const std::vector<RoadSegment>& segments,
    const DirectX::XMFLOAT3& queryPoint,
    float maxDistance,
    float widthFactor = 0.0f);

// 建物の敷地(向き付き矩形)から、最も近い道路区間上の点を探す。矩形の縁(または内側)から区間までの最短距離がmaxDistance以内なら見つかる。
// 建物の中心ではなく敷地で測るので、大きな建物(映画館など)も、道路に隣接していれば接続できる。
// distance: 敷地から区間までの最短距離(道路を貫いていれば0)。point: その時の、区間上の最近接点(入口/私道の端)。
std::optional<NearestRoadPoint> FindNearestRoadToRect(
    const std::vector<RoadSegment>& segments,
    const OrientedRect& rect,
    float maxDistance);
