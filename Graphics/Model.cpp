#include <Windows.h>
#include <vector>
#include <cmath>
#include "Model.h"
#include"Vertex.h"
#include "ModelLoaderRegistry.h"
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

bool Model::LoadFromFile(ID3D11Device* device, const std::string& filePath)
{
    if (!device)
    {
        Debug::Error("Model::LoadFromFile failed : device is null");
        return false;
    }

    // 拡張子に合うローダー(OBJ/FBX)でCPU側のデータに読み、そこから頂点バッファ・テクスチャを作る。
    ModelData data;
    if (!ModelLoaderRegistry::Default().LoadFromFile(filePath, data))
    {
        Debug::Error("Model::LoadFromFile failed : " + filePath);
        return false;
    }

    return CreateFromModelData(device, data, filePath);
}

bool Model::CreateFromModelData(ID3D11Device* device, const ModelData& data, const std::string& debugName)
{
    if (!device)
    {
        Debug::Error("Model::CreateFromModelData failed : device is null");
        return false;
    }

    // すでに読み込み済みなら一旦解放
    m_vertexBuffer.Reset();

    m_vertexCount = 0;

    if (data.vertices.empty())
    {
        Debug::Warning("Model::CreateFromModelData failed : vertex data is empty : " + debugName);
        return false;
    }

    const std::string& texturePath = data.texturePath;

    // 頂点から計算したローカルBounds
    m_boundsMin = data.boundsMin;
    m_boundsMax = data.boundsMax;

    // AABBサイズからBoundingSphere半径を計算
    DirectX::XMFLOAT3 half =
    {
        (m_boundsMax.x - m_boundsMin.x) * 0.5f,
        (m_boundsMax.y - m_boundsMin.y) * 0.5f,
        (m_boundsMax.z - m_boundsMin.z) * 0.5f
    };

    m_boundingRadius =
        sqrtf(
            half.x * half.x +
            half.y * half.y +
            half.z * half.z
        );

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

    // テクスチャパスを取得できたか確認
    if (!texturePath.empty())
    {
        Debug::Info("Model::CreateFromModelData texturePath : " + texturePath);
    }
    else
    {
        Debug::Warning("Model::CreateFromModelData texturePath is empty : " + debugName);
    }

    Debug::Info("Model loaded. vertexCount = " + std::to_string(data.vertices.size()));

    // 頂点バッファを作成
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * data.vertices.size());
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.MiscFlags = 0;
    bufferDesc.StructureByteStride = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data.vertices.data();

    HRESULT hr = device->CreateBuffer(&bufferDesc, &initData, m_vertexBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        Debug::Error("Model::CreateFromModelData failed : CreateBuffer failed");
        return false;
    }

    m_vertexCount = static_cast<UINT>(data.vertices.size());

    return true;
}

bool Model::CreateFromVertices(ID3D11Device* device, const std::vector<Vertex>& vertices)
{
    if (!device)
    {
        Debug::Error("Model::CreateFromVertices failed : device is null");
        return false;
    }

    // すでに作成済みなら一旦解放(区間が変わるたびに作り直される想定)
    m_vertexBuffer.Reset();
    m_vertexCount = 0;

    if (vertices.empty())
    {
        Debug::Warning("Model::CreateFromVertices : vertices is empty");
        return true;
    }

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * vertices.size());
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.MiscFlags = 0;
    bufferDesc.StructureByteStride = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices.data();

    HRESULT hr = device->CreateBuffer(&bufferDesc, &initData, m_vertexBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        Debug::Error("Model::CreateFromVertices failed : CreateBuffer failed");
        return false;
    }

    m_vertexCount = static_cast<UINT>(vertices.size());

    return true;
}

void Model::ClearVertices()
{
    m_vertexBuffer.Reset();
    m_vertexCount = 0;
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