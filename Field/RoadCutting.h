#pragma once
#include <vector>
#include <DirectXMath.h>
#include "RoadSegment.h"
#include "RoadType.h"

// ---- 道路の部分解体の、計算(純粋な部分。画面・ゲームの状態を知らない。単体テストできる) ----
// 解体の単位は「道路に沿った、長さ(マス1つぶん)×道幅」のスライス(RoadCutWindow)。
// スライスの中にある、全ての道路の中心線を、切り取る(交差点では、選んだ範囲の中にある、各道路の該当部分だけが消える)。
// 切り取った後の、残りの区間と、取り除いた部分(費用の計算・住民の経路の確認に使う)を返す。

// 切り取る範囲(向き付きの四角)。道路の中心線を、この四角で切る。
struct RoadCutWindow
{
    DirectX::XMFLOAT3 center{};     // 四角の中心(XZ平面)
    float dirX = 0.0f;              // 道路に沿った向き(単位ベクトル。ワールドの(sin(yaw), cos(yaw))と同じ)
    float dirZ = 1.0f;
    float halfLength = 0.0f;        // 道路に沿った、半分の長さ
    float halfWidth = 0.0f;         // 道路に直角な、半分の幅(道幅の半分)
};

// 取り除いた、道路の中心線の1つの部分。
struct RemovedRoadSpan
{
    DirectX::XMFLOAT3 start{};
    DirectX::XMFLOAT3 end{};
    float width = 1.0f;
    RoadType type = RoadType::Normal;
};

struct RoadCutResult
{
    std::vector<RoadSegment> segments;      // 切った後の区間(切られなかった区間は、そのまま。並びも、できるだけ保つ)
    std::vector<RemovedRoadSpan> removed;   // 取り除いた部分(短すぎる端切れを、一緒に捨てた分も含む)
    float removedLength = 0.0f;             // 取り除いた中心線の長さの合計
    bool changed = false;                   // 1か所でも切ったか
};

// 道路を解体する時の、最小の費用(円)。何かを取り除いたなら、必ずこの額以上かかる(0円では消せない)。
constexpr int kMinimumRoadDemolitionCost = 1;

// 道路解体の費用の割合(建設費の、この割合を払う。返金ではない)。
constexpr float kRoadDemolitionCostRatio = 0.5f;

// segmentの上の、pointに一番近い位置(中心線の上)を含む、スライスを作る。
// スライスの境目は、区間の始点から、sliceLengthおき(始点 + m*sliceLength)。直線は、始点が格子の交点なので、境目が、画面のマス目の線と揃う。
// 行き止まりの端には、見た目だけの延長(startExtension / endExtension。無ければ0)があるので、その端のスライスは、延長の分も含む
// (延長だけを選んで、中心線が何も切れない、ということが無いように。延長の部分は、端のスライスと一緒に消える)。
// sliceIndexOutには、始点から数えた番号(同じスライスを重ねて選ばないための印)を返す。
RoadCutWindow MakeSliceWindow(const RoadSegment& segment, const DirectX::XMFLOAT3& point, float sliceLength,
                              float startExtension, float endExtension, int& sliceIndexOut);

// 中心線a→bを、windowの四角で切った時の、四角の中にある部分(aからの距離 distanceStart〜distanceEnd。長さの単位)。
// 重なる長さが、ほぼ0なら、falseを返す。
bool ClipLineToWindow(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, const RoadCutWindow& window, float& distanceStart, float& distanceEnd);

// windowsの中にある部分を、全ての区間から切り取る。
// minPieceLength: これより短い、残りの端切れは、捨てる(微小な区間が残ると、ノードが増えて、見た目も壊れるため)。
// isolatedMinPieceLength: 両端とも切り口(どこにもつながらない)になった、孤立した切れ端が、これより短ければ、捨てる(つながらない短い道は、ごみになるため。
//   自由描きの短い区間(約0.8)は、スライス(0.5)で割り切れず、選んだスライスの間に、短い切れ端が挟まることがある)。
//   片方の端が、元の端(つながっている)の切れ端は、対象外。0なら、この規則は使わない。
// 切った端は、flatStart/flatEndをtrueにする(平らに描き、延長しない)。切られなかった端の印は、そのまま。
RoadCutResult CutRoads(const std::vector<RoadSegment>& segments, const std::vector<RoadCutWindow>& windows, float minPieceLength,
                       float isolatedMinPieceLength = 0.0f);

// 取り除いた部分の、解体費(円)。道路の種類ごとの建設単価(costPerCell。最初のマス1つぶんの長さあたり)の、半額。
// 全ての部分を合計してから、切り上げる(操作ごとに1回)。何かを取り除いたなら、最低1円。何も無ければ0円。
int CalculateRoadDemolitionCost(const std::vector<RemovedRoadSpan>& removed);
