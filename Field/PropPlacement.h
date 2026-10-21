#pragma once
#include <algorithm>
#include <DirectXMath.h>

// 飾り(Prop)を地面に置く時の、大きさと位置の計算。画面・GPU・ゲームの状態に依存しない純粋な計算なので、単体テストできる。
struct PropPlacementResult
{
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };  // Transform::position
    float scale = 1.0f;                                 // 3軸共通の倍率
};

// モデルの大きさ(boundsMin〜boundsMax)から、置き方を求める。
//  ・倍率: 最大の辺が1マス(cellSize)に収まる値。細長い物が巨大にならないよう、幅・高さ・奥行きのうち最大の辺で決める
//    (BuildControllerの自動フィット「1x1マスに収める」と同じ考え方)。
//  ・位置: XZはモデルの中心を地面の点(groundPoint)に合わせ、Yは足元(boundsMin.y)を地面の高さ(groundHeight)に合わせる。
//  ・大きさが0のモデルでも、無限大やNaNにならない(最大の辺に下限を設ける)。
inline PropPlacementResult ComputePropPlacement(
    const DirectX::XMFLOAT3& boundsMin, const DirectX::XMFLOAT3& boundsMax,
    const DirectX::XMFLOAT3& groundPoint, float groundHeight, float cellSize)
{
    const float width = boundsMax.x - boundsMin.x;
    const float height = boundsMax.y - boundsMin.y;
    const float depth = boundsMax.z - boundsMin.z;
    const float longest = (std::max)({ width, height, depth, 1e-4f });

    PropPlacementResult result;
    result.scale = cellSize / longest;
    result.position =
    {
        groundPoint.x - (boundsMin.x + boundsMax.x) * 0.5f * result.scale,
        groundHeight - boundsMin.y * result.scale,
        groundPoint.z - (boundsMin.z + boundsMax.z) * 0.5f * result.scale
    };
    return result;
}
