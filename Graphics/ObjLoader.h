#pragma once
#include <vector>
#include <string>
#include <DirectXMath.h>

struct ObjVertex
{
    float x, y, z;        // 位置
    float nx, ny, nz;     // 法線
    float u, v;           // UV
    float r, g, b, a;     // 色（確認用に残してもよい）
};

struct ObjIndex
{
    int positionIndex;
    int texcoordIndex;
    int normalIndex;
};


class ObjLoader
{
public:
 static bool Load(
    const std::string& filePath,
    std::vector<ObjVertex>& outVertices,
    std::string& outTexturePath,
    DirectX::XMFLOAT3& outBoundsMin,
    DirectX::XMFLOAT3& outBoundsMax
);
private:
    //static int ParseVertexIndex(const std::string& token);//最初座標だけを読み込むために作った仮関数（あとで削除）
    static ObjIndex ParseFaceToken(const std::string& token);
    //mtlを読み込む
    static bool LoadMtl(
        const std::string& mtlPath,
        const std::string& directory,
        std::string& outTexturePath
    );
};
