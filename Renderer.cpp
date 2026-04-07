//#include "Renderer.h"
//
//#pragma comment(lib, "d3d11.lib")
//
//Renderer::Renderer()
//    : m_device(nullptr)
//    , m_context(nullptr)
//    , m_swapChain(nullptr)
//    , m_renderTargetView(nullptr)
//{
//}
//
//Renderer::~Renderer()
//{
//    Finalize();
//}
//
//bool Renderer::Initialize(HWND hwnd)
//{
//    DXGI_SWAP_CHAIN_DESC scDesc = {};
//    scDesc.BufferCount = 1;
//    scDesc.BufferDesc.Width = 800;
//    scDesc.BufferDesc.Height = 600;
//    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
//    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
//    scDesc.OutputWindow = hwnd;
//    scDesc.SampleDesc.Count = 1;
//    scDesc.Windowed = TRUE;
//    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
//
//    HRESULT hr = D3D11CreateDeviceAndSwapChain(
//        nullptr,
//        D3D_DRIVER_TYPE_HARDWARE,
//        nullptr,
//        0,
//        nullptr,
//        0,
//        D3D11_SDK_VERSION,
//        &scDesc,
//        &m_swapChain,
//        &m_device,
//        nullptr,
//        &m_context
//    );
//
//    if (FAILED(hr))
//    {
//        return false;
//    }
//
//    ID3D11Texture2D* backBuffer = nullptr;
//    hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
//    if (FAILED(hr))
//    {
//        return false;
//    }
//
//    hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTargetView);
//    backBuffer->Release();
//
//    if (FAILED(hr))
//    {
//        return false;
//    }
//
//    D3D11_VIEWPORT viewport = {};
//    viewport.Width = 800.0f;
//    viewport.Height = 600.0f;
//    viewport.MinDepth = 0.0f;
//    viewport.MaxDepth = 1.0f;
//    viewport.TopLeftX = 0.0f;
//    viewport.TopLeftY = 0.0f;
//
//    m_context->RSSetViewports(1, &viewport);
//
//    return true;
//}
//
//void Renderer::BeginFrame()
//{
//    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
//
//    m_context->OMSetRenderTargets(1, &m_renderTargetView, nullptr);
//    m_context->ClearRenderTargetView(m_renderTargetView, clearColor);
//}
//
//void Renderer::EndFrame()
//{
//    m_swapChain->Present(1, 0);
//}
//
//void Renderer::Finalize()
//{
//    if (m_renderTargetView)
//    {
//        m_renderTargetView->Release();
//        m_renderTargetView = nullptr;
//    }
//
//    if (m_swapChain)
//    {
//        m_swapChain->Release();
//        m_swapChain = nullptr;
//    }
//
//    if (m_context)
//    {
//        m_context->Release();
//        m_context = nullptr;
//    }
//
//    if (m_device)
//    {
//        m_device->Release();
//        m_device = nullptr;
//    }
//}

#include "Renderer.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

/*頂点情報*/
struct Vertex
{
    float x, y, z;
    float r, g, b, a;
};

Renderer::Renderer()
    : m_device(nullptr)
    , m_context(nullptr)
    , m_swapChain(nullptr)
    , m_renderTargetView(nullptr)
    , m_vertexShader(nullptr)
    , m_pixelShader(nullptr)
    , m_inputLayout(nullptr)
    , m_vertexBuffer(nullptr)
{
}

Renderer::~Renderer()
{
    Finalize();
}

bool Renderer::Initialize(HWND hwnd)
{

    //画面の設定
    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferCount = 1;
    scDesc.BufferDesc.Width = 800;
    scDesc.BufferDesc.Height = 600;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow = hwnd;
    scDesc.SampleDesc.Count = 1;
    scDesc.Windowed = TRUE;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &scDesc,
        &m_swapChain,
        &m_device,
        nullptr,
        &m_context
    );

    if (FAILED(hr))
    {
        return false;
    }

    //バックバッファを取り出す
    ID3D11Texture2D* backBuffer = nullptr;
    hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (FAILED(hr))
    {
        return false;
    }
    //描画先として使える形にする
    hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTargetView);
    backBuffer->Release();

    if (FAILED(hr))
    {
        return false;
    }
    //画面のどこにどう描くか設定
    D3D11_VIEWPORT viewport = {};
    viewport.Width = 800.0f;
    viewport.Height = 600.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    m_context->RSSetViewports(1, &viewport);

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    //シェーダー読み込み
    hr = D3DCompileFromFile(
        L"SimpleShader.hlsl",
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VSMain",
        "vs_5_0",
        0,
        0,
        &vsBlob,
        &errorBlob
    );

    if (FAILED(hr))
    {
        if (errorBlob) errorBlob->Release();
        return false;
    }

    hr = D3DCompileFromFile(
        L"SimpleShader.hlsl",
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PSMain",
        "ps_5_0",
        0,
        0,
        &psBlob,
        &errorBlob
    );

    if (FAILED(hr))
    {
        if (vsBlob) vsBlob->Release();
        if (errorBlob) errorBlob->Release();
        return false;
    }

    hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        &m_vertexShader
    );
    if (FAILED(hr))
    {
        vsBlob->Release();
        psBlob->Release();
        return false;
    }

    hr = m_device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        &m_pixelShader
    );
    if (FAILED(hr))
    {
        vsBlob->Release();
        psBlob->Release();
        return false;
    }

    //入力レイアウト作成
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,                          D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, sizeof(float) * 3,          D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = m_device->CreateInputLayout(
        layout,
        2,
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        &m_inputLayout
    );

    vsBlob->Release();
    psBlob->Release();

    if (FAILED(hr))
    {
        return false;
    }

    Vertex vertices[] =
    {
        {  0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f },
        {  0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f },
        { -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f }
    };

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(vertices);
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;

    hr = m_device->CreateBuffer(&bufferDesc, &initData, &m_vertexBuffer);
    if (FAILED(hr))
    {
        return false;
    }

    return true;
}

void Renderer::BeginFrame()
{
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    m_context->OMSetRenderTargets(1, &m_renderTargetView, nullptr);
    m_context->ClearRenderTargetView(m_renderTargetView, clearColor);
}

void Renderer::DrawTriangle()
{
    UINT stride = sizeof(Vertex);
    UINT offset = 0;

    m_context->IASetInputLayout(m_inputLayout);
    m_context->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_context->VSSetShader(m_vertexShader, nullptr, 0);
    m_context->PSSetShader(m_pixelShader, nullptr, 0);

    m_context->Draw(3, 0);
}

void Renderer::EndFrame()
{
    m_swapChain->Present(1, 0);
}

void Renderer::Finalize()
{
    if (m_vertexBuffer)
    {
        m_vertexBuffer->Release();
        m_vertexBuffer = nullptr;
    }

    if (m_inputLayout)
    {
        m_inputLayout->Release();
        m_inputLayout = nullptr;
    }

    if (m_pixelShader)
    {
        m_pixelShader->Release();
        m_pixelShader = nullptr;
    }

    if (m_vertexShader)
    {
        m_vertexShader->Release();
        m_vertexShader = nullptr;
    }

    if (m_renderTargetView)
    {
        m_renderTargetView->Release();
        m_renderTargetView = nullptr;
    }

    if (m_swapChain)
    {
        m_swapChain->Release();
        m_swapChain = nullptr;
    }

    if (m_context)
    {
        m_context->Release();
        m_context = nullptr;
    }

    if (m_device)
    {
        m_device->Release();
        m_device = nullptr;
    }
}