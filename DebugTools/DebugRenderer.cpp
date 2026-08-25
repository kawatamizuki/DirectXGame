#include"DebugRenderer.h"
#include "Camera.h"
#include "Debug.h"

#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

namespace
{
    struct DebugConstantBuffer
    {
        XMMATRIX viewProjection;
    };
}



DebugRenderer::DebugRenderer()
{
}

DebugRenderer::~DebugRenderer()
{
}

bool DebugRenderer::Initialize(
    ID3D11Device* device,
    ID3D11DeviceContext* context)
{
    if (!device || !context)
    {
        return false;
    }

    m_device = device;
    m_context = context;


    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    // =========================
    // VertexShader コンパイル
    // =========================
    // DebugLineShader.hlsl の
    // VSMain 関数を VertexShader としてコンパイル
    HRESULT hr = D3DCompileFromFile(
        L"DebugLineShader.hlsl",          // HLSLファイル
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VSMain",                         // エントリーポイント
        "vs_5_0",                         // ShaderModel
        0,
        0,
        vsBlob.GetAddressOf(),
        errorBlob.GetAddressOf()
    );

    // コンパイル失敗
    if (FAILED(hr))
    {
        // HLSLエラー内容出力
        if (errorBlob)
        {
            Debug::Error(
                (char*)errorBlob->GetBufferPointer()
            );
        }

        return false;
    }

    // =========================
    // PixelShader コンパイル
    // =========================
    // DebugLineShader.hlsl の
    // PSMain 関数を PixelShader としてコンパイル
    hr = D3DCompileFromFile(
        L"DebugLineShader.hlsl",
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PSMain",
        "ps_5_0",
        0,
        0,
        psBlob.GetAddressOf(),
        errorBlob.GetAddressOf()
    );

    // コンパイル失敗
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            Debug::Error(
                (char*)errorBlob->GetBufferPointer()
            );
        }

        return false;
    }

    // =========================
    // VertexShader 作成
    // =========================
    // コンパイル済みバイナリから
    // GPU用 VertexShader を生成
    hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        m_vertexShader.GetAddressOf()
    );

    if (FAILED(hr))
    {
        return false;
    }

    // =========================
    // PixelShader 作成
    // =========================
    // コンパイル済みバイナリから
    // GPU用 PixelShader を生成
    hr = m_device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        m_pixelShader.GetAddressOf()
    );

    if (FAILED(hr))
    {
        return false;
    }

    // =========================
    // InputLayout 作成
    // =========================
    // DebugLineVertex の構造と
    // DebugLineShader.hlsl の VSInput を対応させる
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        {
            "POSITION",                         // HLSL側のセマンティクス
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,        // float3
            0,
            0,                                  // DebugLineVertex::position の位置
            D3D11_INPUT_PER_VERTEX_DATA,
            0
        },
        {
            "COLOR",
            0,
            DXGI_FORMAT_R32G32B32A32_FLOAT,     // float4
            0,
            sizeof(DirectX::XMFLOAT3),          // position の直後
            D3D11_INPUT_PER_VERTEX_DATA,
            0
        }
    };

    hr = m_device->CreateInputLayout(
        layout,
        _countof(layout),
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        m_inputLayout.GetAddressOf()
    );

    if (FAILED(hr))
    {
        Debug::Error("DebugRenderer::Initialize failed : CreateInputLayout failed");
        return false;
    }

    // =========================
    // ConstantBuffer 作成
    // =========================
    // ViewProjection 行列を VertexShader に送るためのバッファ
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.ByteWidth = sizeof(DebugConstantBuffer);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = 0;
    cbDesc.MiscFlags = 0;
    cbDesc.StructureByteStride = 0;

    hr = m_device->CreateBuffer(
        &cbDesc,
        nullptr,
        m_constantBuffer.GetAddressOf()
    );

    if (FAILED(hr))
    {
        Debug::Error("DebugRenderer::Initialize failed : CreateBuffer failed");
        return false;
    }

    return true;
}

void DebugRenderer::AddLine(
    const XMFLOAT3& start,
    const XMFLOAT3& end,
    const XMFLOAT4& color)
{
    m_vertices.push_back({ start, color });
    m_vertices.push_back({ end, color });
}

void DebugRenderer::AddOBB(
    const DirectX::XMFLOAT3& localMin,
    const DirectX::XMFLOAT3& localMax,
    const DirectX::XMMATRIX& world,
    const DirectX::XMFLOAT4& color)
{
    using namespace DirectX;

    // ローカル空間の8頂点
    XMFLOAT3 localCorners[8] =
    {
        { localMin.x, localMin.y, localMin.z },
        { localMax.x, localMin.y, localMin.z },
        { localMax.x, localMax.y, localMin.z },
        { localMin.x, localMax.y, localMin.z },

        { localMin.x, localMin.y, localMax.z },
        { localMax.x, localMin.y, localMax.z },
        { localMax.x, localMax.y, localMax.z },
        { localMin.x, localMax.y, localMax.z },
    };

    XMFLOAT3 worldCorners[8];

    // ローカル → ワールド変換
    for (int i = 0; i < 8; ++i)
    {
        XMVECTOR p = XMLoadFloat3(&localCorners[i]);
        p = XMVector3TransformCoord(p, world);
        XMStoreFloat3(&worldCorners[i], p);
    }

    // 箱の12辺
    int edges[12][2] =
    {
        {0,1}, {1,2}, {2,3}, {3,0},
        {4,5}, {5,6}, {6,7}, {7,4},
        {0,4}, {1,5}, {2,6}, {3,7}
    };

    // 12本の線を追加
    for (int i = 0; i < 12; ++i)
    {
        AddLine(
            worldCorners[edges[i][0]],
            worldCorners[edges[i][1]],
            color
        );
    }
}

void DebugRenderer::Flush(const Camera& camera)
{
    if (m_vertices.empty())
    {
        return;
    }

    // =========================
    // 頂点バッファ作成
    // =========================
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.ByteWidth =
        static_cast<UINT>(sizeof(DebugLineVertex) * m_vertices.size());
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = m_vertices.data();

    m_vertexBuffer.Reset();

    HRESULT hr = m_device->CreateBuffer(
        &vbDesc,
        &initData,
        m_vertexBuffer.GetAddressOf()
    );

    if (FAILED(hr))
    {
        Debug::Error("DebugRenderer::Flush failed : CreateBuffer failed");
        Clear();
        return;
    }

    // =========================
    // ViewProjection更新
    // =========================
    DebugConstantBuffer cb{};
    cb.viewProjection =
        XMMatrixTranspose(
            camera.GetViewMatrix() *
            camera.GetProjectionMatrix()
        );

    m_context->UpdateSubresource(
        m_constantBuffer.Get(),
        0,
        nullptr,
        &cb,
        0,
        0
    );

    // =========================
    // 描画設定
    // =========================
    UINT stride = sizeof(DebugLineVertex);
    UINT offset = 0;

    ID3D11Buffer* vertexBuffer = m_vertexBuffer.Get();

    m_context->IASetInputLayout(m_inputLayout.Get());

    m_context->IASetVertexBuffers(
        0,
        1,
        &vertexBuffer,
        &stride,
        &offset
    );

    m_context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_LINELIST
    );

    m_context->VSSetShader(
        m_vertexShader.Get(),
        nullptr,
        0
    );

    m_context->PSSetShader(
        m_pixelShader.Get(),
        nullptr,
        0
    );

    ID3D11Buffer* constantBuffer = m_constantBuffer.Get();

    m_context->VSSetConstantBuffers(
        0,
        1,
        &constantBuffer
    );

    // =========================
    // 線描画
    // =========================
    m_context->Draw(
        static_cast<UINT>(m_vertices.size()),
        0
    );

    Clear();
}
void DebugRenderer::Clear()
{
    m_vertices.clear();
}

void DebugRenderer::Finalize()
{
    m_vertexBuffer.Reset();
    m_constantBuffer.Reset();
    m_vertexShader.Reset();
    m_pixelShader.Reset();
    m_inputLayout.Reset();

    m_device = nullptr;
    m_context = nullptr;
}