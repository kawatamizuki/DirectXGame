#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include"Material.h"
#include"Vertex.h"
#include <DirectXMath.h>



class Model
{
public:
    Model();
    ~Model();

    bool LoadFromObj(ID3D11Device* device, const std::string& filePath);

    // OBJファイルを介さず、生成済みの頂点データから直接モデルを作る
    // (プロシージャルメッシュ用。道路メッシュなど、実行中に内容が変わるジオメトリに使う)。
    // 呼ぶたびに頂点バッファを作り直す(LoadFromObjと同じ「作り直し」の考え方)。
    // マテリアル(テクスチャの有無)はこの関数では変更しない。必要なら呼び出し側が
    // GetMaterial()経由で別途設定する(未設定のままなら頂点カラーのみで描画される)。
    bool CreateFromVertices(ID3D11Device* device, const std::vector<Vertex>& vertices);

    ID3D11Buffer* GetVertexBuffer() const;
    UINT GetVertexCount() const;
    //const std::string& GetTexturePath() const;
    //ID3D11ShaderResourceView* GetTextureView() const;

    const DirectX::XMFLOAT3& GetBoundsMin() const { return m_boundsMin; }
    const DirectX::XMFLOAT3& GetBoundsMax() const { return m_boundsMax; }
    float GetBoundingRadius() const { return m_boundingRadius; }

    Material& GetMaterial();
    const Material& GetMaterial() const;

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    UINT m_vertexCount;

    DirectX::XMFLOAT3 m_boundsMin = { 0, 0, 0 };
    DirectX::XMFLOAT3 m_boundsMax = { 0, 0, 0 };
    float m_boundingRadius = 1.0f;
   /* std::string m_texturePath;
    ID3D11ShaderResourceView* m_textureView;*/

    Material m_material;
};
