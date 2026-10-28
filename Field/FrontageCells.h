#pragma once
#include <vector>
#include <DirectXMath.h>

// ---- 曲がった道の両脇に並べる、建物用のマス(青マス)の位置を決める(純粋な計算。ゲームの状態は知らない) ----
// 道の中心線(折れ線)に沿って、道の縁に接する列のマスを、左右それぞれ「隙間なく・重ならずに」詰めて並べる。
// 1つ1つのマスが、その場所の道の向き(接線)に揃った向きを持つので、カーブでは、マスが扇のように少しずつ回る。
// (以前は、道の区間ごとに別々の格子を作っていたので、短い区間の連なりでは、向きの違うマスが重なったり抜けたりしていた)

// 並べるマス1つ分。
struct FrontageCell
{
    DirectX::XMFLOAT3 center{};     // マスの中心(ワールド)
    float yaw = 0.0f;               // マスの向き。ローカル+Zがワールドの(sin(yaw),0,cos(yaw))を向く(建物・道路のyaw規約と同じ)
};

// centerline: 道の中心線(XZ平面。始点→終点の順)。2点未満なら空を返す。
// roadWidth: 道幅(縁に接する列を決めるのに使う)。cellSize: マスの1辺。
// 戻り値: 左側の列→右側の列の順。同じ側のマスは、中心線に沿った順に並ぶ。
// 直線では、全てのマスが同じ向きで、ちょうどcellSizeおきに並ぶ(従来の1つの格子と同じ並び)。
// カーブでは、内側にあたる列は、道の縁から少し離して置く(曲がった縁がマスに食い込まないように)。
std::vector<FrontageCell> BuildFrontageCells(const std::vector<DirectX::XMFLOAT3>& centerline, float roadWidth, float cellSize);
