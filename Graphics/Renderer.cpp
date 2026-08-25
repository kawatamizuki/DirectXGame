#include <d3dcompiler.h>
//#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include "Renderer.h"
#include"Vertex.h"
#include "ObjLoader.h"
#include"Debug.h"



using namespace DirectX;


struct ConstantBuffer
{
    XMMATRIX WVP;
};

struct MaterialBuffer
{
    int hasTexture;
    float padding[3];
    XMFLOAT4 tint;
};




Renderer::Renderer()
    :
     m_windowWidth(0)
    ,m_windowHeight(0)
    , m_triangleVertexCount(0)
    , m_vsyncEnabled(true)
   

{
}

Renderer::~Renderer()
{
    //Finalize();
}



bool Renderer::Initialize(HWND hwnd)
{
    Debug::Log("Renderer::Initialize start");

    //========================================
    // 1. DirectX本体と画面表示用の仕組みを作る
    //========================================

    // 初期ウィンドウサイズとバックバッファ設定

    D3D11_VIEWPORT viewport = {};
    RECT rect;
    GetClientRect(hwnd, &rect);

     m_windowWidth = (float)(rect.right - rect.left);
     m_windowHeight = (float)(rect.bottom - rect.top);

  

    constexpr UINT kBackBufferCount = 1;
    constexpr UINT kSampleCount = 1;

    // スワップチェイン設定
    // 画面に表示するバックバッファの枚数やサイズ、形式などを決める
    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferCount = kBackBufferCount;
    scDesc.BufferDesc.Width = m_windowWidth;
    scDesc.BufferDesc.Height = m_windowHeight;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow = hwnd;
    scDesc.SampleDesc.Count = kSampleCount;
    scDesc.Windowed = TRUE;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

   // Device  : GPUリソースを作成するための本体
   // Context : 描画命令を出すための本体
   // SwapChain : 描画結果を画面に表示するための仕組み

    UINT createDeviceFlags = 0;

#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    //========================================
    // DirectX Debug Layer
    //========================================

    // Debugビルド時のみDirectXのデバッグ機能を有効化する。
    //
    // これを有効にすると:
    //
    // - Release忘れ
    // - 不正なGPUリソース使用
    // - 無効な描画設定
    // - シェーダー関連エラー
    //
    // などをVisual Studioの出力ウィンドウへ表示してくれる。
    
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &scDesc,
        m_swapChain.GetAddressOf(),
        m_device.GetAddressOf(),
        nullptr,
        m_context.GetAddressOf()
    );

    if (FAILED(hr))
    {
        Debug::Error("D3D11CreateDeviceAndSwapChain failed");
        return false;
    }

    //========================================
    // 2. 色の描画先(RenderTargetView)を作る
    //========================================

    // スワップチェインからバックバッファを取り出す
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;

    hr = m_swapChain->GetBuffer(
        0,
        __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(backBuffer.GetAddressOf())
    );

    if (FAILED(hr))
    {
        Debug::Error("SwapChain::GetBuffer failed");
        return false;
    }
    //描画先として使える形にする
    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
  

    if (FAILED(hr))
    {
        Debug::Error("CreateRenderTargetView failed");
        return false;
    }

    //========================================
    // 3. 深度バッファ関連を作る
    //========================================

    // 深度バッファ本体を作成する
    // これに各ピクセルのZ値を保存して、前後関係を判定する
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = m_windowWidth;
    depthDesc.Height = m_windowHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = m_device->CreateTexture2D(&depthDesc, nullptr, m_depthStencilBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        Debug::Error("CreateTexture2D for depth buffer failed");
        return false;
    }
    // 深度バッファを描画時に使うためのビューを作成
    hr = m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), nullptr, m_depthStencilView.GetAddressOf());
    if (FAILED(hr))
    {
        Debug::Error("CreateDepthStencilView failed");
        return false;
    }
    // 深度テストのルールを作る
    // 今回は「より手前にあるものを描画する」設定
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
    dsDesc.StencilEnable = FALSE;

    hr = m_device->CreateDepthStencilState(&dsDesc, m_depthStencilState.GetAddressOf());
    if (FAILED(hr))
    {
        Debug::Error("CreateDepthStencilState failed");
        return false;
    }
    //========================================
    // 4. ビューポートを設定する(画面のどこにどう描くか設定)
    //========================================

    // ウィンドウのクライアント領域を取得し、
    // その範囲全体に描画するよう設定する
    
   

    viewport.Width = m_windowWidth;
    viewport.Height = m_windowHeight;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    m_context->RSSetViewports(1, &viewport);

   //========================================
   // 5. シェーダーをコンパイルして作成する
   //========================================

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    const wchar_t* kShaderFile = L"SimpleShader.hlsl";
    const char* kVSMain = "VSMain";
    const char* kPSMain = "PSMain";

   //========================================
   // 6. サンプラーステートを作成する
   //========================================

   // テクスチャをピクセルシェーダーで読むときの設定。
   // Filter:
   //   拡大・縮小時に線形補間して滑らかに表示する。
   // AddressU/V/W:
   //   UVが0〜1の範囲外になったとき、テクスチャを繰り返す。
   //   例: UVが1.2なら0.2として扱う。
   // このサンプラーはHLSL側の SamplerState register(s0) に渡す。

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = m_device->CreateSamplerState(&samplerDesc, m_samplerState.GetAddressOf());
    if (FAILED(hr))
    {
        Debug::Error("CreateSamplerState failed");
        return false;
    }

    //========================================
    // 7. 定数バッファを作成する
    //========================================

    // 頂点シェーダーにWVP行列を送るためのバッファ
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.ByteWidth = sizeof(ConstantBuffer);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

     hr = m_device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
     if (FAILED(hr))
     {
         Debug::Error("CreateBuffer for ConstantBuffer failed");
         return false;
     }

    // 頂点シェーダーをコンパイル
    hr = D3DCompileFromFile(
        kShaderFile,
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        kVSMain,
        "vs_5_0",
        0,
        0,
        vsBlob.GetAddressOf(),
        errorBlob.GetAddressOf()
    );

    if (FAILED(hr))
    {
        Debug::Error("Vertex shader compile failed");

        if (errorBlob)
        {
            OutputDebugStringA(
                static_cast<char*>(errorBlob->GetBufferPointer())
            );
        }

        return false;
    }

  

    // ピクセルシェーダーをコンパイル
    hr = D3DCompileFromFile(
        kShaderFile,
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        kPSMain,
        "ps_5_0",
        0,
        0,
        psBlob.GetAddressOf(),
        errorBlob.GetAddressOf()
    );


    if (FAILED(hr))
    {
        Debug::Error("Pixel shader compile failed");

        if (errorBlob)
        {
            OutputDebugStringA(
                static_cast<const char*>(errorBlob->GetBufferPointer())
            );
        }

        return false;
    }
    // コンパイルした頂点シェーダーをGPUで使える形にする
    hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        m_vertexShader.GetAddressOf()
    );

    if (FAILED(hr))
    {
        Debug::Error("CreateVertexShader failed");
        return false;
    }

   
    // コンパイルしたピクセルシェーダーをGPUで使える形にする
    hr = m_device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        m_pixelShader.GetAddressOf()
    );

    if (FAILED(hr))
    {
        Debug::Error("CreatePixelShader failed");
        return false;
    }

    //========================================
   // 8. マテリアルバッファを作成する
   //========================================
    D3D11_BUFFER_DESC materialCbDesc = {};
    materialCbDesc.Usage = D3D11_USAGE_DEFAULT;
    materialCbDesc.ByteWidth = sizeof(MaterialBuffer);
    materialCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_device->CreateBuffer(&materialCbDesc, nullptr, m_materialBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        Debug::Error("CreateBuffer for MaterialBuffer failed");
        return false;
    }

    //========================================
    // 8. 入力レイアウトを作る
    //========================================

    // Vertex構造体のメモリ配置をシェーダー入力と対応づける
    // 位置(float3)+法線（float3）+UV(float2) + 色(float4)

    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,                          D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, sizeof(float) * 3,          D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, sizeof(float) * 6,          D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, sizeof(float) * 8,          D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = m_device->CreateInputLayout(
        layout,
        _countof(layout),
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        m_inputLayout.GetAddressOf()
    );

    // シェーダー作成後はコンパイル結果のBlobは不要
 
    if (FAILED(hr))
    {
        Debug::Error("CreateInputLayout failed");
        return false;
    }

    //========================================
    // 9. 三角形描画用の頂点バッファを作る
    //========================================

    // 深度チェック確認用の頂点データ
   Vertex triangleVertices[] =
{
    // 奥の三角形（赤）
    {  0.0f,  0.6f, 4.0f,   0.0f, 0.0f, -1.0f,   0.5f, 0.0f,   1.0f, 0.0f, 0.0f, 1.0f },
    {  0.6f, -0.6f, 4.0f,   0.0f, 0.0f, -1.0f,   1.0f, 1.0f,   1.0f, 0.0f, 0.0f, 1.0f },
    { -0.6f, -0.6f, 4.0f,   0.0f, 0.0f, -1.0f,   0.0f, 1.0f,   1.0f, 0.0f, 0.0f, 1.0f },

    // 手前の三角形（青）
    {  0.0f,  0.3f, 1.0f,   0.0f, 0.0f, -1.0f,   0.5f, 0.0f,   0.0f, 0.0f, 1.0f, 1.0f },
    {  0.3f, -0.3f, 1.0f,   0.0f, 0.0f, -1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 1.0f, 1.0f },
    { -0.3f, -0.3f, 1.0f,   0.0f, 0.0f, -1.0f,   0.0f, 1.0f,   0.0f, 0.0f, 1.0f, 1.0f }
};

    // 三角形データをGPUに送るための頂点バッファを作成
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(triangleVertices);
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA triangleInitData = {};
    triangleInitData.pSysMem = triangleVertices;

    hr = m_device->CreateBuffer(&bufferDesc, &triangleInitData, m_triangleVertexBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        Debug::Error("CreateBuffer for triangle vertex buffer failed");
        return false;
    }

    m_triangleVertexCount = _countof(triangleVertices);
    Debug::Info("Triangle vertex buffer created. vertexCount = " + std::to_string(m_triangleVertexCount));


    //========================================
    // 10. OBJモデル用の頂点バッファを作る（移行済み）
    //========================================
    
   
    Debug::Info("Renderer initialized successfully");

    return true;

   
}

void Renderer::BeginFrame()
{
    // 画面を消すときの色を指定する
   // { 赤, 緑, 青, アルファ } の順
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(m_windowWidth);
    vp.Height = static_cast<float>(m_windowHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    m_context->RSSetViewports(1, &vp);


    // このフレームで使う描画先を設定する
    // m_renderTargetView  : 色を書き込む先
    // m_depthStencilView  : 深度(Z値)を書き込む先
    ID3D11RenderTargetView* renderTargetView = m_renderTargetView.Get();

    m_context->OMSetRenderTargets(
        1,
        &renderTargetView,
        m_depthStencilView.Get()
    );

    // 深度テストのルールをGPUに設定する
    // 例: 手前のピクセルだけ描画する
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);

    // 画面全体を clearColor で塗りつぶして初期化する
    // 前のフレームの絵が残らないようにする
    m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);

    // 深度バッファを 1.0f (一番奥) で初期化する
    // 前のフレームのZ情報が残らないようにする
    m_context->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}


void Renderer::Resize(UINT width, UINT height)
{

    //========================================
    // 無効サイズ防止
    //========================================

    if (width == 0 || height == 0)
    {
        return;
    }

    //========================================
    // 新しいウィンドウサイズ保存
    //========================================
    m_windowWidth = width;
    m_windowHeight = height;

    //========================================
    // 現在の描画先を解除
    //========================================
    //
    // RenderTargetViewを破棄する前に、
    // GPUへセットされた状態を解除する必要がある。
    if (m_context)
    {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
    }

    //========================================
    // 古い描画リソース破棄
    //========================================
    //
    // サイズ変更前の:
    //
    // - RenderTargetView
    // - DepthStencilView
    // - DepthBuffer
    //
    // を解放する。
    //
    m_renderTargetView.Reset();
    m_depthStencilView.Reset();
    m_depthStencilBuffer.Reset();


    //========================================
    // SwapChainバッファサイズ変更
    //========================================
    //
    // バックバッファを
    // 新しいウィンドウサイズへ変更する。
    //
    HRESULT hr = m_swapChain->ResizeBuffers(
        0,
        m_windowWidth,
        m_windowHeight,
        DXGI_FORMAT_UNKNOWN,
        0
    );

    
    if (FAILED(hr))
    {
        Debug::Error("SwapChain ResizeBuffers failed");
        return;
    }

    //========================================
    // 新しいバックバッファ取得
    //========================================
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;

    hr = m_swapChain->GetBuffer(
        0,
        __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(backBuffer.GetAddressOf())
    );

    if (FAILED(hr))
    {
        Debug::Error("SwapChain GetBuffer failed");
        return;
    }

    //========================================
    // 新しいRenderTargetView作成
    //========================================
    hr = m_device->CreateRenderTargetView(
        backBuffer.Get(),
        nullptr,
        m_renderTargetView.GetAddressOf()
    );

    if (FAILED(hr))
    {
        Debug::Error("CreateRenderTargetView failed");
        return;
    }

    //========================================
    // 新しいDepthBuffer作成
    //========================================
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = m_windowWidth;
    depthDesc.Height = m_windowHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = m_device->CreateTexture2D(
        &depthDesc,
        nullptr,
        m_depthStencilBuffer.GetAddressOf()
    );

    if (FAILED(hr))
    {
        Debug::Error("Create depth buffer failed");
        return;
    }

    //========================================
    // 新しいDepthStencilView作成
    //========================================
    hr = m_device->CreateDepthStencilView(
        m_depthStencilBuffer.Get(),
        nullptr,
        m_depthStencilView.GetAddressOf()
    );

    if (FAILED(hr))
    {
        Debug::Error("CreateDepthStencilView failed");
        return;
    }

    //========================================
    // Viewport更新
    //========================================
    //
    // 描画範囲を新しいサイズへ更新する。
    //
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_windowWidth);
    viewport.Height = static_cast<float>(m_windowHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    m_context->RSSetViewports(1, &viewport);
}

void Renderer::SetVSyncEnabled(bool enabled)
{
    m_vsyncEnabled = enabled;
}

bool Renderer::IsVSyncEnabled() const
{
    return m_vsyncEnabled;
}

void Renderer::Update()
{
   // m_objAngle += 0.01f;
  /*  m_objTransform.rotation.y += 0.01f;*/
}

void Renderer::DrawTriangle()
{

    //========================================
    // 1. 頂点データの設定
    //========================================

    UINT stride = sizeof(Vertex);// 1頂点のサイズ
    UINT offset = 0;             // 読み込み開始位置

    // 深度テストを有効化（手前のものを優先表示）
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);

    // 頂点の構造（POSITION, COLOR）をGPUに伝える
    m_context->IASetInputLayout(m_inputLayout.Get());

    // 使用する頂点バッファをセット（今回は三角形）
    ID3D11Buffer* triangleVertexBuffer = m_triangleVertexBuffer.Get();

    m_context->IASetVertexBuffers(
        0,
        1,
        &triangleVertexBuffer,
        &stride,
        &offset
    );

    // 三角形リストとして描画する設定
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    //========================================
    // 2. シェーダーの設定
    //========================================

    // 頂点変換を行うシェーダー
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);

    // 色を決めるシェーダー
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    //========================================
    // 3. 行列（WVP）の作成
    //========================================

    // ワールド行列（今回は移動・回転なし）
    XMMATRIX world = XMMatrixIdentity();

    // カメラの位置・向き
    XMVECTOR eye = XMVectorSet(0.0f, 0.0f, -5.0f, 0.0f);
    XMVECTOR target = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    // ビュー行列（カメラ視点の変換）
    XMMATRIX view = XMMatrixLookAtLH(eye, target, up);

    // 投影行列（遠近感をつける）
    XMMATRIX projection = XMMatrixPerspectiveFovLH(
        XMConvertToRadians(45.0f),
        static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight),
        0.1f,
        100.0f
    );

    // ワールド→ビュー→投影の順に変換
    XMMATRIX wvp = world * view * projection;

    //========================================
    // 4. シェーダーに行列を送る
    //========================================
    ConstantBuffer cb{};
    cb.WVP = XMMatrixTranspose(wvp);// HLSL用に転置

    // GPUにデータ送信
    m_context->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
    // 頂点シェーダーにバッファをセット
    ID3D11Buffer* constantBuffer = m_constantBuffer.Get();

    m_context->VSSetConstantBuffers(
        0,
        1,
        &constantBuffer
    );

    //========================================
    // 5. 描画実行
    //========================================

    m_context->Draw(m_triangleVertexCount, 0);
}


void Renderer::DrawModel(
    const Model& model,
    const Transform& transform,
    const Camera& camera,
    const XMFLOAT4& tint
)
{

    //========================================
    // 1. 頂点データの設定
    //========================================

    ID3D11Buffer* vertexBuffer = model.GetVertexBuffer();
    if (!vertexBuffer)
    {
        Debug::Error("Renderer::DrawModel failed : vertexBuffer is null");
        return;
    }

    if (model.GetVertexCount() == 0)
    {
        Debug::Error("Renderer::DrawModel failed : vertexCount is 0");
        return;
    }


    UINT stride = sizeof(Vertex);// 1頂点のサイズ
    UINT offset = 0;             // 読み込み開始位置

    // 深度テストを有効化
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);

    // 頂点レイアウト設定
    m_context->IASetInputLayout(m_inputLayout.Get());

    // OBJ用の頂点バッファをセット
    m_context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

    // 三角形として描画
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    //========================================
    // 2. シェーダー・テクスチャ設定
    //========================================
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);


    //========================================
    // Material情報を取得
    //========================================

    // Model が保持している Material を取得する。
    // 今後 Material は:
    //
    // - テクスチャ
    // - 色
    // - ライティング設定
    // - シェーダー設定
    //
    // などを持つ予定。
    const Material& material = model.GetMaterial();


    //========================================
    // テクスチャ情報を取得
    //========================================

    // Material が保持しているテクスチャを取得する。
    // nullptr の場合は「テクスチャなしモデル」を意味する。

    ID3D11ShaderResourceView* textureView = material.GetTextureView();

    //========================================
    // Material定数バッファ更新
    //========================================

    // ピクセルシェーダーへ渡す Material 用データ。
    // 今は「テクスチャがあるかどうか」だけ渡している。

    MaterialBuffer materialBuffer{};
    materialBuffer.hasTexture = material.HasTexture() ? 1 : 0;
    materialBuffer.tint = tint;


    // hasTexture:
    // 1 = テクスチャあり
    // 0 = テクスチャなし
    //
    // HLSL側ではこの値を見て:
    //
    // if(hasTexture == 1)
    // {
    //     texture.Sample(...)
    // }
    //
    // のように描画方法を分岐する。

    // GPUへ MaterialBuffer の内容を送信する。
    // UpdateSubresource は CPU側データをGPUバッファへコピーする処理。
    m_context->UpdateSubresource(
        m_materialBuffer.Get(),
        0,
        nullptr,
        &materialBuffer,
        0,
        0
    );

    //========================================
    // デバッグ確認
    //========================================

    // テクスチャなしモデルの場合。
    // 例:
    //
    // - 色のみモデル
    // - 当たり判定表示
    // - デバッグ用メッシュ
    //
    // などでも描画できるようにする予定。
    if (!material.HasTexture())
    {
       // Debug::Warning("Renderer::DrawModel material has no texture");
    }
    //========================================
    // Material定数バッファをピクセルシェーダーへ渡す
    //========================================

    // register(b1) の MaterialBuffer に対応。
    // VS側ではなくPS側で使用するため PSSetConstantBuffers を使う。
    ID3D11Buffer* materialConstantBuffer = m_materialBuffer.Get();
    m_context->PSSetConstantBuffers(
        1,
        1,
        &materialConstantBuffer
    );

    //========================================
    // テクスチャをピクセルシェーダーへ渡す
    //========================================

    // register(t0) の Texture2D diffuseTexture に対応。
    //
    // textureView が nullptr の場合は、
    // 「テクスチャ未設定」として扱われる。
    //
    // nullptr を渡すことで、前回描画したモデルの
    // テクスチャが残る問題を防ぐ。
    m_context->PSSetShaderResources(
        0,
        1,
        &textureView
    );
   
    //========================================
    // サンプラー設定
    //========================================

    // register(s0) の SamplerState に対応。
    //
    // UV座標を使ってテクスチャを読む時の設定。
    // 現在は:
    //
    // - LinearFilter
    // - Wrap
    //
    // を使用している。
    ID3D11SamplerState* samplerState = m_samplerState.Get();

    m_context->PSSetSamplers(
        0,
        1,
        &samplerState
    );

    //========================================
    // 3. 行列（WVP）の作成
    //========================================


    //XMMATRIX world = XMMatrixIdentity();

     // モデルを縮小＋回転
   /* XMMATRIX world =
        XMMatrixScaling(0.5f, 0.5f, 0.5f) *
        XMMatrixRotationY(m_objAngle);*/

    //XMMATRIX world = m_objTransform.GetWorldMatrix();
    //XMMATRIX world = transform.GetWorldMatrix();

    // カメラ設定
    XMMATRIX world = transform.GetWorldMatrix();
    XMMATRIX view = camera.GetViewMatrix();
    XMMATRIX projection = camera.GetProjectionMatrix();

    

    ////透視投影
    //XMMATRIX projection = XMMatrixPerspectiveFovLH(
    //    XMConvertToRadians(45.0),
    //    static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight),
    //    0.1f,
    //    100.0f
    //);

    // 最終変換行列
    XMMATRIX wvp = world * view * projection;

    //========================================
    // 4. シェーダーに行列を送る
    //========================================

    ConstantBuffer cb{};
    cb.WVP = XMMatrixTranspose(wvp);

    m_context->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
    ID3D11Buffer* constantBuffer = m_constantBuffer.Get();

    m_context->VSSetConstantBuffers(
        0,
        1,
        &constantBuffer
    );

    //========================================
    // 5. 描画
    //========================================

    m_context->Draw(model.GetVertexCount(), 0);

}

void Renderer::EndFrame()
{
    UINT syncInterval = m_vsyncEnabled ? 1 : 0;

    m_swapChain->Present(syncInterval, 0);
}

void Renderer::Finalize()
{
    Debug::Log("Renderer::Finalize start");

    if (m_context)
    {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        m_context->ClearState();
        m_context->Flush();
    }

    m_triangleVertexBuffer.Reset();
    m_materialBuffer.Reset();
    m_constantBuffer.Reset();
    m_samplerState.Reset();
    m_inputLayout.Reset();
    m_pixelShader.Reset();
    m_vertexShader.Reset();
    m_depthStencilState.Reset();
    m_depthStencilView.Reset();
    m_depthStencilBuffer.Reset();
    m_renderTargetView.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();

    Debug::Info("Renderer Finalize successfully");
}


