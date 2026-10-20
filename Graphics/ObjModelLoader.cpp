#include "ObjModelLoader.h"
#include <vector>
#include "ObjLoader.h"

bool ObjModelLoader::LoadFromFile(const std::string& filePath, ModelData& out)
{
    out.Clear();

    std::vector<ObjVertex> objVertices;
    if (!ObjLoader::Load(filePath, objVertices, out.texturePath, out.boundsMin, out.boundsMax))
    {
        return false;
    }

    // ObjVertexとVertexは同じ並びの項目。描画側の形(Vertex)に詰め替える。
    out.vertices.reserve(objVertices.size());
    for (const ObjVertex& v : objVertices)
    {
        out.vertices.push_back({ v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v, v.r, v.g, v.b, v.a });
    }
    if (out.vertices.empty())
    {
        return false;
    }

    // OBJは右手系で作られているので、エンジンの左手系に変換して返す(鏡像・文字の鏡文字を防ぐ)。
    out.ConvertFromRightHanded();
    return true;
}
