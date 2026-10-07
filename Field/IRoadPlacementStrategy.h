#pragma once
#include <DirectXMath.h>
#include <optional>
#include "RoadSegment.h"

// 道路の「引き方」を差し替え可能にするインターフェース。
// v1は直線(2クリック)だが、将来曲線ストラテジーに差し替える際も
// RoadSystem側の呼び出し(OnPlacementPoint/Reset/HasPendingStart)は変更不要。
class IRoadPlacementStrategy
{
public:
    virtual ~IRoadPlacementStrategy() = default;

    // プレイヤーがワールド上の1点をクリックするたびに呼ばれる。
    // セグメントが確定した時だけ値を返す(直線の場合は2クリック目)。
    virtual std::optional<RoadSegment> OnPlacementPoint(const DirectX::XMFLOAT3& worldPoint) = 0;

    // 配置途中の状態を取り消す(Cancel入力用)。
    virtual void Reset() = 0;

    // 配置中(始点だけ確定済み)ならtrueを返し、始点をoutStartに書き込む。
    // プレビュー線の描画に使う。
    virtual bool HasPendingStart(DirectX::XMFLOAT3& outStart) const = 0;
};
