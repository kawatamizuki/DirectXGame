#include <Windows.h>
#include <vector>

#include "Model.h"
#include"Vertex.h"
#include "ObjLoader.h"
#include "Debug.h"
#include "WICTextureLoader11.h"

//この中だけで使う。テクスチャパスの文字コードを正確に変換するための関数
namespace
{
    std::wstring ToWideString(const std::string& str)
    {
        if (str.empty())
        {
            return L"";
        }

        int sizeNeeded = MultiByteToWideChar(
            CP_ACP,
            0,
            str.c_str(),
            -1,
            nullptr,
            0
        );

        std::wstring result(sizeNeeded, 0);

        MultiByteToWideChar(
            CP_ACP,
            0,
            str.c_str(),
            -1,
            &result[0],
            sizeNeeded
        );

        if (!result.empty() && result.back() == L'\0')
        {
            result.pop_back();
        }

        return result;
    }
}

Model::Model()
    : m_vertexCount(0)
{
}

Model::~Model()
{
    //m_vertexBuffer.Reset();
    m_material.Clear();

    m_vertexCount = 0;
}

bool Model::LoadFromObj(ID3D11Device* device, const std::string& filePath)
{

    
   
    if (!device)
    {
        Debug::Error("Model::LoadFromObj failed : device is null");
        return false;
    }

    // すでに読み込み済みなら一旦解放
    m_vertexBuffer.Reset();

    m_vertexCount = 0;


    //==========================================================
    //  OBJモデル用の頂点バッファを作る
    //==========================================================
    std::vector<ObjVertex> objVertices;//頂点データ
    std::string texturePath;//テクスチャのパス

    if (!ObjLoader::Load(filePath, objVertices, texturePath))
    {
        Debug::Error("Model::LoadFromObj failed : ObjLoader::Load failed : " + filePath);
        return false;
    }

    m_material.Clear();
    m_material.SetTexturePath(texturePath);

    if (!texturePath.empty())
    {
        Debug::Info("Loading texture : " + texturePath);

        std::wstring wpath = ToWideString(texturePath);

        ID3D11ShaderResourceView* textureView = nullptr;

        HRESULT hr = DirectX::CreateWICTextureFromFile(
            device,
            wpath.c_str(),
            nullptr,
            &textureView
        );

        if (FAILED(hr))
        {
            Debug::Error("Texture load failed : " + texturePath);
        }
        else
        {
            Debug::Info("Texture load success : " + texturePath);

            m_material.SetTextureView(textureView);

            textureView->Release();
            textureView = nullptr;
        }
    }
    else
    {
        Debug::Warning("No texture path found");
    }

    // MTLからテクスチャパスを取得できたか確認
    if (!texturePath.empty())
    {
        Debug::Info("Model::LoadFromObj texturePath : " + texturePath);
    }
    else
    {
        Debug::Warning("Model::LoadFromObj texturePath is empty : " + filePath);
    }

    if (objVertices.empty())
    {
        Debug::Warning("Model::LoadFromObj failed : vertex data is empty : " + filePath);
        return false;
    }

  
    // ObjVertex → Rendererで使っているVertex形式へ変換
    std::vector<Vertex> convertedVertices;
    convertedVertices.reserve(objVertices.size());
    Debug::Info("OBJ loaded. vertexCount = " + std::to_string(objVertices.size()));

    for (const auto& v : objVertices)
    {
        convertedVertices.push_back({
            v.x,  v.y,  v.z,
            v.nx, v.ny, v.nz,
            v.u,  v.v,
            v.r,  v.g,  v.b,  v.a
            });
    }

    // OBJ用頂点バッファを作成
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * convertedVertices.size());
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.MiscFlags = 0;
    bufferDesc.StructureByteStride = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = convertedVertices.data();

    HRESULT hr = device->CreateBuffer(&bufferDesc, &initData, m_vertexBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        Debug::Error("Model::LoadFromObj failed : CreateBuffer failed");
        return false;
    }

    m_vertexCount = static_cast<UINT>(convertedVertices.size());

   

    return true;
}

ID3D11Buffer* Model::GetVertexBuffer() const
{
    return m_vertexBuffer.Get();
}

UINT Model::GetVertexCount() const
{
    return m_vertexCount;
}

//const std::string& Model::GetTexturePath() const
//{
//    return m_texturePath;
//}
//
//ID3D11ShaderResourceView* Model::GetTextureView() const
//{
//    return m_textureView;
//}

Material& Model::GetMaterial()
{
    return m_material;
}

const Material& Model::GetMaterial() const
{
    return m_material;
}