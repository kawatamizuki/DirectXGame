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
    , m_depthStencilBuffer(nullptr)
    , m_depthStencilView(nullptr)
    , m_depthStencilState(nullptr)
    , m_vertexShader(nullptr)
    , m_pixelShader(nullptr)
    , m_inputLayout(nullptr)
    , m_vertexBuffer(nullptr)
    , m_constantBuffer(nullptr)
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

    // 深度バッファ作成
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = 800;
    depthDesc.Height = 600;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = m_device->CreateTexture2D(&depthDesc, nullptr, &m_depthStencilBuffer);
    if (FAILED(hr))
    {
        return false;
    }

    hr = m_device->CreateDepthStencilView(m_depthStencilBuffer, nullptr, &m_depthStencilView);
    if (FAILED(hr))
    {
        return false;
    }
    //深度比較ルール
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
    dsDesc.StencilEnable = FALSE;

    hr = m_device->CreateDepthStencilState(&dsDesc, &m_depthStencilState);
    if (FAILED(hr))
    {
        return false;
    }

    //画面のどこにどう描くか設定
    D3D11_VIEWPORT viewport = {};
    RECT rect;
    GetClientRect(hwnd, &rect);

    float width = (float)(rect.right - rect.left);
    float height = (float)(rect.bottom - rect.top);

    viewport.Width = width;
    viewport.Height = height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    m_context->RSSetViewports(1, &viewport);

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    //
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.ByteWidth = sizeof(ConstantBuffer);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

     hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);
    if (FAILED(hr))
    {
        return false;
    }

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

    //深度チェックのため2つの三角形としてデータを用意
    Vertex vertices[] =
    {
        // 奥の三角形（赤） 
        {  0.0f,  0.6f, 4.0f, 1.0f, 0.0f, 0.0f, 1.0f },
        {  0.6f, -0.6f, 4.0f, 1.0f, 0.0f, 0.0f, 1.0f },
        { -0.6f, -0.6f, 4.0f, 1.0f, 0.0f, 0.0f, 1.0f },

        // 手前の三角形（青） 
        {  0.0f,  0.3f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        {  0.3f, -0.3f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { -0.3f, -0.3f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f }
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
    // 画面を消すときの色を指定する
   // { 赤, 緑, 青, アルファ } の順
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    D3D11_VIEWPORT vp = {};
    vp.Width = 800;
    vp.Height = 600;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    m_context->RSSetViewports(1, &vp);


    // このフレームで使う描画先を設定する
    // m_renderTargetView  : 色を書き込む先
    // m_depthStencilView  : 深度(Z値)を書き込む先
    m_context->OMSetRenderTargets(1, &m_renderTargetView, m_depthStencilView);

    // 深度テストのルールをGPUに設定する
    // 例: 手前のピクセルだけ描画する
    m_context->OMSetDepthStencilState(m_depthStencilState, 0);

    // 画面全体を clearColor で塗りつぶして初期化する
    // 前のフレームの絵が残らないようにする
    m_context->ClearRenderTargetView(m_renderTargetView, clearColor);

    // 深度バッファを 1.0f (一番奥) で初期化する
    // 前のフレームのZ情報が残らないようにする
    m_context->ClearDepthStencilView(m_depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
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

      XMMATRIX world = XMMatrixIdentity();
   // //ワールド行列がきいているかどうかのチェック
   // XMMATRIX world = XMMatrixRotationX(XMConvertToRadians(45.0f));//x
   // XMMATRIX world = XMMatrixRotationY(XMConvertToRadians(45.0f));//y
   // XMMATRIX world = XMMatrixRotationZ(XMConvertToRadians(45.0f));//z
    XMVECTOR eye = XMVectorSet(0.0f, 0.0f, -10.0f, 0.0f); // カメラ位置
    XMVECTOR target = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f); // 見る方向
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(eye, target, up);

    //透視投影
    XMMATRIX projection = XMMatrixPerspectiveFovLH(
        XMConvertToRadians(45.0),
        800.0f / 600.0f,
        0.1f,
        100.0f
    );
    ////平行投影（プロジェクション行列がきいているかどうかのチェック）
    //XMMATRIX projection = XMMatrixOrthographicLH(
    //    4.0f,
    //    3.0f,
    //    0.1f,
    //    100.0f
    //);

    XMMATRIX wvp = world * view * projection;

    ConstantBuffer cb;
    cb.WVP = XMMatrixTranspose(wvp);

    m_context->UpdateSubresource(m_constantBuffer, 0, nullptr, &cb, 0, 0);

    m_context->VSSetConstantBuffers(0, 1, &m_constantBuffer);
    //深度チェック中
    m_context->Draw(6, 0);
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

    if (m_depthStencilView)
    {
        m_depthStencilView->Release();
        m_depthStencilView = nullptr;
    }

    if (m_depthStencilBuffer)
    {
        m_depthStencilBuffer->Release();
        m_depthStencilBuffer = nullptr;
    }

    if (m_depthStencilState)
    {
        m_depthStencilState->Release();
        m_depthStencilState = nullptr;
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

    if (m_constantBuffer)
    {
        m_constantBuffer->Release();
        m_constantBuffer = nullptr;
    }
}