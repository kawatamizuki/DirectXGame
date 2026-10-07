#include "QuadRoadMeshGenerator.h"
#include "RoadMeshBuilder.h"
#include "Renderer.h"
#include "Transform.h"

using namespace DirectX;

namespace
{
    // 道路面の色(アスファルト調のプレースホルダー、頂点カラー)。本物のテクスチャに差し替えるまでの仮の色。
    constexpr XMFLOAT4 kRoadColor(0.25f, 0.25f, 0.27f, 1.0f);
}

void QuadRoadMeshGenerator::RebuildMesh(ID3D11Device* device, const std::vector<RoadSegment>& segments, const std::vector<RoadNode>& nodes, float deadEndExtension)
{
    std::vector<Vertex> vertices = BuildRoadMeshVertices(segments, nodes, kRoadColor, 0, deadEndExtension);
    m_model.CreateFromVertices(device, vertices);
}

void QuadRoadMeshGenerator::Draw(Renderer& renderer, const Camera& camera)
{
    // 道路がまだ1本も無い場合、頂点数0のモデルをDrawModelに渡すとエラーログが出てしまうため
    // (Renderer::DrawModelはvertexCount==0を失敗として扱う)、ここでスキップする。
    if (m_model.GetVertexCount() == 0)
    {
        return;
    }

    Transform transform; // 単位Transform(頂点は既にワールド座標で生成済みのため、移動・回転・拡大は不要)
    renderer.DrawModel(m_model, transform, camera);
}
