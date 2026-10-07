#pragma once
#include <DirectXMath.h>
#include "RoadType.h"
#include "RoadDrawMode.h"

// 道路の1区間を表す純粋なデータ。メッシュの頂点や色そのものは一切持たない
// (実際の見た目の生成はIRoadMeshGenerator/RoadMeshBuilder側に分離する)。
struct RoadSegment
{
    DirectX::XMFLOAT3 start;
    DirectX::XMFLOAT3 end;
    float width = 1.0f;      // GetRoadTypeDefinition(type).widthから設定される(見た目・占有判定に使用)

    // 今この区間を何人の代表住民が使っているか(表示専用の数値)。
    // DemandSystemが毎ティック書き込み、道路の混雑色分けの表示に使う。
    // 経路探索の計算そのものには使わない。
    float debugLoad = 0.0f;

    // この区間の道路種別(Narrow/Normal/Large)。
    RoadType type = RoadType::Normal;

    // この区間がどちらの配置モードで作られたか。RoadMeshBuilderが、継ぎ目を
    // ミター(平滑化)してよいかどうかの判断に使う(Curve同士の継ぎ目は元々
    // 小さい角度差しかない滑らかな曲線なのでミターが有効だが、Straightは
    // グリッド角度スナップにより鋭い角度で折れることがあり、ミターすると
    // 逆に隙間が目立つため)。
    RoadDrawMode drawMode = RoadDrawMode::Straight;
};
