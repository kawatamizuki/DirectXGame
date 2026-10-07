#pragma once
#include <vector>
#include <DirectXMath.h>
#include "GridCoord.h"

// XZ平面上の向き付き矩形。originが最小角(ローカル(0,0)の角)で、ローカル+X方向に
// width、ローカル+Z方向にdepthだけ伸びる。yawはローカル+Zがワールドの
// (sin(yaw),0,cos(yaw))を向く規約(建物のrotation.y・道路のyawと共通)。
// 道路区間・建物のfootprint・青い隣接マスを全て同じ型で表し、重なり判定を1箇所に集約する。
struct OrientedRect
{
    DirectX::XMFLOAT3 origin{};
    float yaw = 0.0f;
    float width = 0.0f;
    float depth = 0.0f;
};

// 2つの向き付き矩形が(面積を持って)重なっているかを、分離軸定理(SAT)で厳密に判定する。
// 判定軸は両矩形の辺の向き(各2本=計4本)。epsilon以下の貫入(辺が接するだけ・ほぼ接するだけ)
// は「重なっていない」扱いにするため、ぴったり隣接する配置は許可される。
bool OrientedRectsOverlap(const OrientedRect& a, const OrientedRect& b, float epsilon);

// ワールド座標の点(x,z)が矩形の内側(境界を含む。toleranceぶんだけ外側まで許容)にあるか。
bool OrientedRectContainsPoint(const OrientedRect& rect, float x, float z, float tolerance);

// 矩形が触れているワールドセルを列挙する。点サンプリングのように境界ぎりぎりの位置で
// 結果がブレることがないよう、全セルを OrientedRectsOverlap で厳密に判定する。
// 道路の占有セル・建物のfootprintセル(境界チェック用)の両方がこの1つの関数を使う。
std::vector<GridCoord> SampleOrientedRectangleCells(const OrientedRect& rect, float cellSize);

// originを最小角とする矩形版(上の関数の薄いラッパー)。
std::vector<GridCoord> SampleOrientedRectangleCells(
    const DirectX::XMFLOAT3& origin, float yaw,
    float width, float depth, float cellSize);
