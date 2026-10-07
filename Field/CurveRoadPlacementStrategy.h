#pragma once
#include "IRoadPlacementStrategy.h"

// IRoadPlacementStrategyの曲線版。
// ドラッグ中に渡され続けるマウスのワールド座標(生の入力、震えを含む)を
// 指数移動平均で平滑化しながら、一定距離進むたびに短い直線(RoadSegment)を
// 1本ずつ確定させていく。これを繰り返すことで、見た目には滑らかな曲線になる。
// 極力まっすぐドラッグした場合は、平滑化後の点もほぼ一直線に並ぶため、
// 結果として直線に近い道路になる(曲線しか引けないわけではない)。
class CurveRoadPlacementStrategy : public IRoadPlacementStrategy
{
public:
    std::optional<RoadSegment> OnPlacementPoint(const DirectX::XMFLOAT3& worldPoint) override;
    void Reset() override;
    bool HasPendingStart(DirectX::XMFLOAT3& outStart) const override;

private:
    bool m_hasStart = false;
    DirectX::XMFLOAT3 m_lastConfirmedPoint{}; // 直前に確定した点(次の小区間の始点になる)
    DirectX::XMFLOAT3 m_smoothedPoint{};      // マウスのブレを平滑化した現在位置

    // 平滑化の追従度合い(0〜1)。小さいほど反応が鈍くなる代わりに滑らかになる。
    // 以前は0.5だったが、手ブレの細かい揺れまで拾ってしまい道路がガタガタになる問題が
    // あったため下げた。RoadSystem側がドラッグ中は逐次確定せずバッファに貯めてから
    // 離した時にまとめて検証・確定する方式になったため、「反応が鈍すぎて区間が1つも
    // 確定しないまま離してしまう」場合でも、離した時の残り距離ブリッジ処理が
    // 最後にまとめて繋いでくれる(以前のような「直線1本が即置かれる」ことにはならない)。
    static constexpr float kSmoothingFactor = 0.25f;
    // 平滑化後の位置が直前の確定点からこれだけ離れたら、新しい小区間として確定する。
    // 値を上げるほど1区間が長くなり、細かい手ブレの往復が均されてガタつきにくくなる。
    static constexpr float kMinSegmentLength = 0.3f;
};
