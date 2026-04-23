#include <d3dcompiler.h>
#include "Renderer.h"
#include "ObjLoader.h"
#include"Debug.h"



#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

/*頂点情報*/
struct Vertex
{
    float x, y, z;        // 位置
    float nx, ny, nz;     // 法線
    float u, v;           // UV
    float r, g, b, a;     // 色
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
    ,m_windowWidth(0)
    ,m_windowHeight(0)
   // , m_vertexBuffer(nullptr)
    , m_constantBuffer(nullptr)
    //, m_vertexCount(0)
    , m_triangleVertexBuffer(nullptr)
    , m_triangleVertexCount(0)
    , m_objVertexBuffer(nullptr)
    , m_objVertexCount(0)


{
}

Renderer::~Renderer()
{
    Finalize();
}

bool Renderer::Initialize(HWND hwnd)
{
    Debug::Log("Renderer::Initialize start");

    //========================================
    // 1. DirectX本体と画面表示用の仕組みを作る
    //========================================

    // 初期ウィンドウサイズとバックバッファ設定
    constexpr UINT kDefaultWindowWidth = 800;
    constexpr UINT kDefaultWindowHeight = 600;

    m_windowWidth = kDefaultWindowWidth;
    m_windowHeight = kDefaultWindowHeight;

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
        Debug::Error("D3D11CreateDeviceAndSwapChain failed");
        return false;
    }

    //========================================
    // 2. 色の描画先(RenderTargetView)を作る
    //========================================

    // スワップチェインからバックバッファを取り出す
    ID3D11Texture2D* backBuffer = nullptr;
    hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (FAILED(hr))
    {
        Debug::Error("SwapChain::GetBuffer failed");
        return false;
    }
    //描画先として使える形にする
    hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTargetView);
    backBuffer->Release();

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
        Debug::Error("CreateTexture2D for depth buffer failed");
        return false;
    }
    // 深度バッファを描画時に使うためのビューを作成
    hr = m_device->CreateDepthStencilView(m_depthStencilBuffer, nullptr, &m_depthStencilView);
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

    hr = m_device->CreateDepthStencilState(&dsDesc, &m_depthStencilState);
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

   //========================================
   // 5. シェーダーをコンパイルして作成する
   //========================================

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    const wchar_t* kShaderFile = L"SimpleShader.hlsl";
    const char* kVSMain = "VSMain";
    const char* kPSMain = "PSMain";

    //========================================
    // 6. 定数バッファを作成する
    //========================================

    // 頂点シェーダーにWVP行列を送るためのバッファ
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.ByteWidth = sizeof(ConstantBuffer);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

     hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);
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
        &vsBlob,
        &errorBlob
    );

    if (FAILED(hr))
    {
        Debug::Error("Vertex shader compile failed");

        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());//シェーダーのコンパイルエラーを出力
            errorBlob->Release();
            errorBlob = nullptr;
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
        &psBlob,
        &errorBlob
    );


    if (FAILED(hr))
    {
        Debug::Error("Pixel shader compile failed");

        if (vsBlob)
        {
            vsBlob->Release();
            vsBlob = nullptr;
        }

        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
            errorBlob = nullptr;
        }

        return false;
    }
    // コンパイルした頂点シェーダーをGPUで使える形にする
    hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        &m_vertexShader
    );

    if (FAILED(hr))
    {
        Debug::Error("CreateVertexShader failed");

        if (vsBlob)
        {
            vsBlob->Release();
            vsBlob = nullptr;
        }

        if (psBlob)
        {
            psBlob->Release();
            psBlob = nullptr;
        }

        return false;
    }


    // コンパイルしたピクセルシェーダーをGPUで使える形にする
    hr = m_device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        &m_pixelShader
    );

    if (FAILED(hr))
    {
        Debug::Error("CreatePixelShader failed");

        if (vsBlob)
        {
            vsBlob->Release();
            vsBlob = nullptr;
        }

        if (psBlob)
        {
            psBlob->Release();
            psBlob = nullptr;
        }

        return false;
    }

    //========================================
    // 7. 入力レイアウトを作る
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
        &m_inputLayout
    );

    // シェーダー作成後はコンパイル結果のBlobは不要
    vsBlob->Release();
    psBlob->Release();
    if (FAILED(hr))
    {
        Debug::Error("CreateInputLayout failed");
        return false;
    }

    //========================================
    // 8. 三角形描画用の頂点バッファを作る
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

    hr = m_device->CreateBuffer(&bufferDesc, &triangleInitData, &m_triangleVertexBuffer);
    if (FAILED(hr))
    {
        Debug::Error("CreateBuffer for triangle vertex buffer failed");
        return false;
    }

    m_triangleVertexCount = _countof(triangleVertices);
    Debug::Info("Triangle vertex buffer created. vertexCount = " + std::to_string(m_triangleVertexCount));
    //========================================
    // 9. OBJモデル用の頂点バッファを作る
    //========================================
    std::vector<ObjVertex> objVertices;

    if (!ObjLoader::Load("Models/cube.obj", objVertices))
    {
        Debug::Error("ObjLoader::Load failed (Models/cube.obj)");
        return false;
    }

    Debug::Info("OBJ loaded successfully. vertexCount = " + std::to_string(objVertices.size()));

    // OBJファイルを読み込み、ObjVertexの配列として受け取る
    m_objVertexCount = static_cast<UINT>(objVertices.size());
    Debug::Info("Triangle vertex buffer created. vertexCount = " + std::to_string(m_triangleVertexCount));

    // 現在の描画用Vertex構造体に変換する
    std::vector<Vertex> objConverted;
    objConverted.reserve(objVertices.size());

    for (const auto& v : objVertices)
    {
        objConverted.push_back({
            v.x, v.y, v.z,
            v.nx, v.ny, v.nz,
            v.u, v.v,
            v.r, v.g, v.b, v.a
            });
    }

    // OBJ用頂点バッファを作成
    D3D11_BUFFER_DESC objBufferDesc = {};
    objBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    objBufferDesc.ByteWidth = sizeof(Vertex) * static_cast<UINT>(objConverted.size());
    objBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
      
    D3D11_SUBRESOURCE_DATA objInitData = {};
    objInitData.pSysMem = objConverted.data();

    hr = m_device->CreateBuffer(&objBufferDesc, &objInitData, &m_objVertexBuffer);
    if (FAILED(hr))
    {
        Debug::Error("CreateBuffer for OBJ vertex buffer failed");
        return false;
    }

    Debug::Info(
        "OBJ vertex buffer created. vertexCount = " +
        std::to_string(objConverted.size())
    );

    return true;

    Debug::Info("Renderer initialized successfully");
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

void Renderer::Update()
{
    m_objAngle += 0.01f;
}

void Renderer::DrawTriangle()
{

    //========================================
    // 1. 頂点データの設定
    //========================================

    UINT stride = sizeof(Vertex);// 1頂点のサイズ
    UINT offset = 0;             // 読み込み開始位置

    // 深度テストを有効化（手前のものを優先表示）
    m_context->OMSetDepthStencilState(m_depthStencilState, 0);

    // 頂点の構造（POSITION, COLOR）をGPUに伝える
    m_context->IASetInputLayout(m_inputLayout);

    // 使用する頂点バッファをセット（今回は三角形）
    m_context->IASetVertexBuffers(0, 1, &m_triangleVertexBuffer, &stride, &offset);

    // 三角形リストとして描画する設定
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    //========================================
    // 2. シェーダーの設定
    //========================================

    // 頂点変換を行うシェーダー
    m_context->VSSetShader(m_vertexShader, nullptr, 0);

    // 色を決めるシェーダー
    m_context->PSSetShader(m_pixelShader, nullptr, 0);

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
        800.0f / 600.0f,
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
    m_context->UpdateSubresource(m_constantBuffer, 0, nullptr, &cb, 0, 0);
    // 頂点シェーダーにバッファをセット
    m_context->VSSetConstantBuffers(0, 1, &m_constantBuffer);

    //========================================
    // 5. 描画実行
    //========================================

    m_context->Draw(m_triangleVertexCount, 0);
}

void Renderer::DrawObj()
{

    //========================================
    // 1. 頂点データの設定
    //========================================


    UINT stride = sizeof(Vertex);// 1頂点のサイズ
    UINT offset = 0;             // 読み込み開始位置

    // 深度テストを有効化
    m_context->OMSetDepthStencilState(m_depthStencilState, 0);

    // 頂点レイアウト設定
    m_context->IASetInputLayout(m_inputLayout);

    // OBJ用の頂点バッファをセット
    m_context->IASetVertexBuffers(0, 1, &m_objVertexBuffer, &stride, &offset);

    // 三角形として描画
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


    //========================================
    // 2. シェーダー設定
    //========================================
    m_context->VSSetShader(m_vertexShader, nullptr, 0);
    m_context->PSSetShader(m_pixelShader, nullptr, 0);


    //========================================
    // 3. 行列（WVP）の作成
    //========================================
    

    //XMMATRIX world = XMMatrixIdentity();

     // モデルを縮小＋回転
    XMMATRIX world =
        XMMatrixScaling(0.5f, 0.5f, 0.5f) *
        XMMatrixRotationY(m_objAngle);

    // カメラ設定
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

    // 最終変換行列
    XMMATRIX wvp = world * view * projection;

    //========================================
    // 4. シェーダーに行列を送る
    //========================================

    ConstantBuffer cb{};
    cb.WVP = XMMatrixTranspose(wvp);

    m_context->UpdateSubresource(m_constantBuffer, 0, nullptr, &cb, 0, 0);
    m_context->VSSetConstantBuffers(0, 1, &m_constantBuffer);

    //========================================
    // 5. 描画
    //========================================

    m_context->Draw(m_objVertexCount, 0);

}

void Renderer::EndFrame()
{
    m_swapChain->Present(1, 0);
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

    // 先にGPUリソース類を解放
    if (m_objVertexBuffer)
    {
        m_objVertexBuffer->Release();
        m_objVertexBuffer = nullptr;
    }

    if (m_triangleVertexBuffer)
    {
        m_triangleVertexBuffer->Release();
        m_triangleVertexBuffer = nullptr;
    }

    if (m_constantBuffer)
    {
        m_constantBuffer->Release();
        m_constantBuffer = nullptr;
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

    if (m_depthStencilState)
    {
        m_depthStencilState->Release();
        m_depthStencilState = nullptr;
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

    if (m_renderTargetView)
    {
        m_renderTargetView->Release();
        m_renderTargetView = nullptr;
    }

    // 最後に本体側
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

    Debug::Info("Renderer Finalize successfully");
}