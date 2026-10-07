#pragma once
#include "IRoadMeshGenerator.h"
#include "Model.h"

// IRoadMeshGeneratorの実メッシュ版。
// 区間(RoadSegment)ごとに帯状のクアッド(2枚の三角形)を生成し、
// 1つのModelにまとめて描画する(区間の数だけドローコールを分けるのではなく、
// 全区間ぶんの頂点をまとめて1回のDrawModelで描く)。
//
// 継ぎ目の処理: 「次数2の素直な連続点(行き止まりでもT字路/十字路でもない、
// 単純に2本の区間が繋がっているだけの点)」では、繋がる2区間の縁の向きを
// 平均化してミター(継ぎ目の角度に応じた縁の調整)する。これにより、きつい曲線でも
// 継ぎ目に隙間や重なりが目立たなくなる。行き止まり・T字路・十字路ではミターせず、
// 区間ごとの独立した縁のまま(交差点の見た目作り込みは次の一歩)。
class QuadRoadMeshGenerator : public IRoadMeshGenerator
{
public:
    void RebuildMesh(ID3D11Device* device, const std::vector<RoadSegment>& segments, const std::vector<RoadNode>& nodes, float deadEndExtension) override;
    void Draw(Renderer& renderer, const Camera& camera) override;
    size_t GetVertexCount() const override { return m_model.GetVertexCount(); }

private:
    Model m_model;
};
