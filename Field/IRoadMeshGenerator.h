#pragma once
#include <vector>
#include <d3d11.h>
#include "RoadSegment.h"
#include "RoadNode.h"

class Renderer;
class Camera;

// 道路データ(RoadSegment)から見た目を作る処理を差し替え可能にするインターフェース。
// v1(DebugLineRoadMeshGenerator)はDebugRendererのライン描画による簡易表現だったが、
// 実メッシュ(QuadRoadMeshGenerator)に置き換えたことで、
// 「区間が変わった時にメッシュを作り直す」+「毎フレーム描画する」という形になった。
// 将来、Kennyモデルベースの実装に差し替える時も、Game側の1メンバ型を差し替えるだけで見た目を刷新できる。
class IRoadMeshGenerator
{
public:
    virtual ~IRoadMeshGenerator() = default;

    // 区間一覧からメッシュを作り直す(区間が追加/分割された時だけ呼ばれる。毎フレームではない)。
    // nodesは交差点・端点の情報(RoadSystemが既に持っているもの)。継ぎ目をなめらかに
    // 繋ぐためのミター処理に使う(次数2の素直な連続点だけをミターする)。
    // deadEndExtension: 行き止まりの見た目をこの分だけ区間の方向に伸ばす(BuildRoadMeshVerticesの
    // 同名引数と同じ。データには触れず見た目だけの調整)。
    virtual void RebuildMesh(ID3D11Device* device, const std::vector<RoadSegment>& segments, const std::vector<RoadNode>& nodes, float deadEndExtension) = 0;

    // 生成済みのメッシュを捨てて、道路が1本も無い状態に戻す(セーブデータの読み込み前の全消去に使う)。
    virtual void Clear() = 0;

    // 毎フレーム呼ばれる描画処理。
    virtual void Draw(Renderer& renderer, const Camera& camera) = 0;

    // 現在生成済みの頂点数(Profilerのメモリ内訳表示に使う)。
    virtual size_t GetVertexCount() const = 0;
};
