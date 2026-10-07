#pragma once
#include <DirectXMath.h>
#include "OrientedRectangleOverlap.h"

// 建物が「どう置かれたか」の確定結果(配置時に決まった値)。建物自身(GameObject)が持つ。
// セーブ/ロードで、建物の占有・青マス(隣接の格子)を配置時と同じ形に戻すのに使う。
// 入力(カーソル位置など)ではなく結果を持つので、後から配置のルールが変わっても、
// 保存した街は保存した時の形のまま読み込める。
struct BuildingPlacement
{
    // footprint(建物が占める範囲)の実際の形(ワールド座標)。占有判定に使う。
    OrientedRect footprintRect;

    // 配置に使った基準座標系(道路に揃えた格子の原点と向き)。
    DirectX::XMFLOAT3 frameOrigin{};
    float frameYaw = 0.0f;

    // その基準座標系での、footprintの最小角のマス番号と、占めるマスの数(回転を反映済み)。
    int localCellX = 0;
    int localCellZ = 0;
    int localWidthCells = 1;
    int localDepthCells = 1;
};
