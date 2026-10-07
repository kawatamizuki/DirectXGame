#pragma once
#include <vector>
#include <DirectXMath.h>
#include "RoadSegment.h"
#include "RoadNode.h"
#include "Vertex.h"

// segments/nodesから帯状クアッドの頂点列を生成する(継ぎ目のミター処理込み)。
// 確定済み道路(QuadRoadMeshGenerator)と、カーブドラッグ中のゴーストプレビュー(RoadSystem)の
// 両方から使う共通ロジック。
//
// emitStartIndexを指定すると、ミター計算(継ぎ目の向き調整)はsegments全体を対象にしつつ、
// 実際に頂点を出力するのはemitStartIndex以降の区間だけに絞れる。ゴーストプレビューが
// 「既存の確定済み道路(再描画したくない) + まだ未確定の区間(これだけ描きたい)」を
// 1つのsegments配列にまとめて渡し、境界の継ぎ目もミターさせつつ確定済み分の頂点は
// 二重に生成しない、という用途に使う。
//
// deadEndExtension: 行き止まり(次数1のノード)の見た目を、区間自身の方向にこの分だけ
// 伸ばす。データ(segments/nodes/占有判定)には一切触れず、見た目の頂点位置だけを
// 伸ばす。直線モードの道路は端点がセルの中心に来るため、伸ばさないと行き止まりが
// マス目の半分で途切れて見えてしまう(隣接セルの端まで届かない)問題への対策。
std::vector<Vertex> BuildRoadMeshVertices(
    const std::vector<RoadSegment>& segments,
    const std::vector<RoadNode>& nodes,
    const DirectX::XMFLOAT4& color,
    size_t emitStartIndex = 0,
    float deadEndExtension = 0.0f);
