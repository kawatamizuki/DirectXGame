#include "UIRenderer.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Debug.h"

#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

namespace
{
    // UIShader.hlsl の cbuffer と対応(register b0)。
    struct UIConstants
    {
        float twoOverWidth;   // 2 / 画面幅  (ピクセル→画面座標の変換に使う)
        float twoOverHeight;  // 2 / 画面高さ
        float pad[2];
    };

    // windows.hのmin/maxマクロと衝突するため、std::min/maxは使わず自前で書く。
    float MinF(float a, float b) { return (a < b) ? a : b; }
    float MaxF(float a, float b) { return (a > b) ? a : b; }

    float Clamp01(float v)
    {
        if (v < 0.0f) { return 0.0f; }
        if (v > 1.0f) { return 1.0f; }
        return v;
    }

    UIColor LerpColor(const UIColor& a, const UIColor& b, float t)
    {
        return UIColor(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t);
    }

    UIColor WithAlpha(const UIColor& c, float alpha)
    {
        return UIColor(c.x, c.y, c.z, alpha);
    }

    // 角丸の1つの角を構成する点の数(円弧の分割数)。半径が大きいほど細かく分ける。
    int ArcSegmentsForRadius(float radius)
    {
        if (radius < 0.5f)
        {
            return 0; // 角丸なし(4隅の点だけ)
        }
        int segments = static_cast<int>(std::ceil(radius * 0.6f));
        if (segments < 3) { segments = 3; }
        if (segments > 10) { segments = 10; }
        return segments;
    }
}

bool UIRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, const wchar_t* shaderPath)
{
    m_device = device;
    m_context = context;
    m_vertices.reserve(kMaxVertices);

    // フォント: 見つからなくても図形は描けるので、失敗しても初期化は続ける(文字だけ描けない)。
    if (!m_atlas.Initialize(kAtlasSize))
    {
        Debug::Warning("UIRenderer: UI用のフォントが見つからないため、文字は表示されません(Fonts/UIFont.ttf を置くか、Windows標準フォントを確認してください)");
    }
    m_atlas.GetSolidUV(m_solidU, m_solidV);

    // ---- シェーダー ----
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompileFromFile(shaderPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VSMain", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf());
    if (FAILED(hr))
    {
        Debug::Error("UIRenderer: vertex shader compile failed");
        if (errorBlob)
        {
            Debug::Error(static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return false;
    }

    hr = D3DCompileFromFile(shaderPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PSMain", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        Debug::Error("UIRenderer: pixel shader compile failed");
        if (errorBlob)
        {
            Debug::Error(static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return false;
    }

    if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertexShader.GetAddressOf())) ||
        FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixelShader.GetAddressOf())))
    {
        Debug::Error("UIRenderer: CreateShader failed");
        return false;
    }

    // ---- 入力レイアウト(UIShader.hlslのVSInputと対応) ----
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (FAILED(m_device->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.GetAddressOf())))
    {
        Debug::Error("UIRenderer: CreateInputLayout failed");
        return false;
    }

    // ---- 頂点バッファ(毎フレーム書き換える動的バッファ) ----
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = static_cast<UINT>(kMaxVertices * sizeof(Vertex));
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_device->CreateBuffer(&vbDesc, nullptr, m_vertexBuffer.GetAddressOf())))
    {
        Debug::Error("UIRenderer: CreateBuffer(vertex) failed");
        return false;
    }

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(UIConstants);
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf())))
    {
        Debug::Error("UIRenderer: CreateBuffer(constant) failed");
        return false;
    }

    // ---- アトラスのテクスチャ(R8の1チャンネル) ----
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = kAtlasSize;
    texDesc.Height = kAtlasSize;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA texData = {};
    texData.pSysMem = m_atlas.Pixels();
    texData.SysMemPitch = kAtlasSize;
    if (FAILED(m_device->CreateTexture2D(&texDesc, &texData, m_atlasTexture.GetAddressOf())) ||
        FAILED(m_device->CreateShaderResourceView(m_atlasTexture.Get(), nullptr, m_atlasView.GetAddressOf())))
    {
        Debug::Error("UIRenderer: atlas texture creation failed");
        return false;
    }

    // 初期化時に全体を転送済みなので、変更なしの状態にしておく。
    FontAtlas::DirtyRect ignored;
    m_atlas.ConsumeDirtyRect(ignored);

    // ---- サンプラー・描画状態 ----
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    m_device->CreateSamplerState(&samplerDesc, m_linearSampler.GetAddressOf());
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    m_device->CreateSamplerState(&samplerDesc, m_pointSamplerState.GetAddressOf());

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_device->CreateBlendState(&blendDesc, m_blendState.GetAddressOf());

    // UIは3Dの手前に重ねて描くので、深度テストも深度の書き込みもしない。
    D3D11_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = FALSE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    m_device->CreateDepthStencilState(&depthDesc, m_depthState.GetAddressOf());

    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;
    m_device->CreateRasterizerState(&rasterDesc, m_rasterizerState.GetAddressOf());

    m_initialized = true;
    return true;
}

void UIRenderer::BeginFrame(float screenW, float screenH)
{
    m_screenW = (screenW > 1.0f) ? screenW : 1.0f;
    m_screenH = (screenH > 1.0f) ? screenH : 1.0f;
    m_vertices.clear();

    // 前のフレームでアトラスが満杯になっていたら、描画が始まる前のここで焼き直す
    // (描画の途中で破棄すると、既に積んだ文字のUVが無効になるため)。
    m_atlas.ResetIfNeeded();
}

void UIRenderer::EndFrame()
{
    Flush();
}

void UIRenderer::UploadAtlasIfDirty()
{
    FontAtlas::DirtyRect rect;
    if (!m_atlas.ConsumeDirtyRect(rect))
    {
        return;
    }

    D3D11_BOX box = {};
    box.left = static_cast<UINT>(rect.x0);
    box.top = static_cast<UINT>(rect.y0);
    box.right = static_cast<UINT>(rect.x1);
    box.bottom = static_cast<UINT>(rect.y1);
    box.front = 0;
    box.back = 1;

    const uint8_t* source = m_atlas.Pixels() + static_cast<size_t>(rect.y0) * kAtlasSize + rect.x0;
    m_context->UpdateSubresource(m_atlasTexture.Get(), 0, &box, source, kAtlasSize, 0);
}

void UIRenderer::Flush()
{
    if (!m_initialized)
    {
        return;
    }

    // 文字を焼き足していたら、描く前にGPUのテクスチャへ転送しておく。
    UploadAtlasIfDirty();

    if (m_vertices.empty())
    {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        m_vertices.clear();
        return;
    }
    std::memcpy(mapped.pData, m_vertices.data(), m_vertices.size() * sizeof(Vertex));
    m_context->Unmap(m_vertexBuffer.Get(), 0);

    UIConstants constants = {};
    constants.twoOverWidth = 2.0f / m_screenW;
    constants.twoOverHeight = 2.0f / m_screenH;
    m_context->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &constants, 0, 0);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = m_screenW;
    viewport.Height = m_screenH;
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    ID3D11Buffer* vertexBuffer = m_vertexBuffer.Get();
    ID3D11Buffer* constantBuffer = m_constantBuffer.Get();
    ID3D11ShaderResourceView* atlasView = m_atlasView.Get();
    ID3D11SamplerState* sampler = m_pointSampling ? m_pointSamplerState.Get() : m_linearSampler.Get();

    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, &constantBuffer);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    m_context->PSSetShaderResources(0, 1, &atlasView);
    m_context->PSSetSamplers(0, 1, &sampler);
    m_context->OMSetBlendState(m_blendState.Get(), nullptr, 0xFFFFFFFF);
    m_context->OMSetDepthStencilState(m_depthState.Get(), 0);
    m_context->RSSetState(m_rasterizerState.Get());

    m_context->Draw(static_cast<UINT>(m_vertices.size()), 0);

    m_vertices.clear();
}

void UIRenderer::EnsureSpace(size_t vertexCount)
{
    if (m_vertices.size() + vertexCount > kMaxVertices)
    {
        Flush();
    }
}

void UIRenderer::PushVertex(float x, float y, float u, float v, const UIColor& c)
{
    m_vertices.push_back(Vertex{ x, y, u, v, c.x, c.y, c.z, c.w });
}

void UIRenderer::PushSolidTriangle(const Pt& a, const UIColor& ca, const Pt& b, const UIColor& cb, const Pt& c, const UIColor& cc)
{
    EnsureSpace(3);
    PushVertex(a.x, a.y, m_solidU, m_solidV, ca);
    PushVertex(b.x, b.y, m_solidU, m_solidV, cb);
    PushVertex(c.x, c.y, m_solidU, m_solidV, cc);
}

void UIRenderer::PushSolidQuad(const Pt& a, const UIColor& ca, const Pt& b, const UIColor& cb, const Pt& c, const UIColor& cc, const Pt& d, const UIColor& cd)
{
    PushSolidTriangle(a, ca, b, cb, c, cc);
    PushSolidTriangle(a, ca, c, cc, d, cd);
}

void UIRenderer::ComputeOutwardOffsets(const std::vector<Pt>& points, std::vector<Pt>& outOffsets)
{
    size_t n = points.size();
    outOffsets.assign(n, Pt{ 0.0f, 0.0f });
    if (n < 3)
    {
        return;
    }

    Pt centroid{ 0.0f, 0.0f };
    for (const Pt& p : points)
    {
        centroid.x += p.x;
        centroid.y += p.y;
    }
    centroid.x /= static_cast<float>(n);
    centroid.y /= static_cast<float>(n);

    // 辺(a→b)の外向きの単位法線。長さ0の辺はfalseを返す。
    auto edgeNormal = [&](const Pt& a, const Pt& b, Pt& normal) -> bool
    {
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        float length = std::sqrt(dx * dx + dy * dy);
        if (length < 1e-4f)
        {
            return false;
        }
        normal = Pt{ dy / length, -dx / length };
        float mx = (a.x + b.x) * 0.5f - centroid.x;
        float my = (a.y + b.y) * 0.5f - centroid.y;
        if (normal.x * mx + normal.y * my < 0.0f)
        {
            normal.x = -normal.x;
            normal.y = -normal.y;
        }
        return true;
    };

    for (size_t i = 0; i < n; ++i)
    {
        const Pt& prev = points[(i + n - 1) % n];
        const Pt& cur = points[i];
        const Pt& next = points[(i + 1) % n];

        Pt n1{ 0.0f, 0.0f };
        Pt n2{ 0.0f, 0.0f };
        bool has1 = edgeNormal(prev, cur, n1);
        bool has2 = edgeNormal(cur, next, n2);
        if (!has1 && !has2)
        {
            continue;
        }
        if (!has1) { n1 = n2; }
        if (!has2) { n2 = n1; }

        // ミター: 両方の辺からの距離がちょうど1になるように、法線の和を(1+内積)で割る。
        float denominator = 1.0f + (n1.x * n2.x + n1.y * n2.y);
        if (denominator < 0.5f) { denominator = 0.5f; } // 鋭い角で伸びすぎないように
        outOffsets[i] = Pt{ (n1.x + n2.x) / denominator, (n1.y + n2.y) / denominator };
    }
}

void UIRenderer::FillPolygonAA(const std::vector<Pt>& points, const std::vector<UIColor>& colors)
{
    size_t n = points.size();
    if (n < 3 || colors.size() != n)
    {
        return;
    }

    std::vector<Pt> offsets;
    ComputeOutwardOffsets(points, offsets);

    // 縁の幅は1px。本来の輪郭を中心に、内側へ0.5px・外側へ0.5pxの範囲で透明になる。
    constexpr float kHalfFeather = 0.5f;

    Pt center{ 0.0f, 0.0f };
    UIColor centerColor(0.0f, 0.0f, 0.0f, 0.0f);
    for (size_t i = 0; i < n; ++i)
    {
        center.x += points[i].x;
        center.y += points[i].y;
        centerColor.x += colors[i].x;
        centerColor.y += colors[i].y;
        centerColor.z += colors[i].z;
        centerColor.w += colors[i].w;
    }
    float inv = 1.0f / static_cast<float>(n);
    center.x *= inv; center.y *= inv;
    centerColor.x *= inv; centerColor.y *= inv; centerColor.z *= inv; centerColor.w *= inv;

    std::vector<Pt> inner(n);
    std::vector<Pt> outer(n);
    for (size_t i = 0; i < n; ++i)
    {
        inner[i] = Pt{ points[i].x - offsets[i].x * kHalfFeather, points[i].y - offsets[i].y * kHalfFeather };
        outer[i] = Pt{ points[i].x + offsets[i].x * kHalfFeather, points[i].y + offsets[i].y * kHalfFeather };
    }

    for (size_t i = 0; i < n; ++i)
    {
        size_t j = (i + 1) % n;

        // 内側: 中心から各頂点へ扇状に塗る。
        PushSolidTriangle(center, centerColor, inner[i], colors[i], inner[j], colors[j]);

        // 縁: 内側の辺(不透明)から外側の辺(透明)へ。
        UIColor clearI = WithAlpha(colors[i], 0.0f);
        UIColor clearJ = WithAlpha(colors[j], 0.0f);
        PushSolidQuad(inner[i], colors[i], outer[i], clearI, outer[j], clearJ, inner[j], colors[j]);
    }
}

void UIRenderer::BuildRoundedRectPoints(const UIRect& rect, float radius, std::vector<Pt>& out, int segmentsOverride)
{
    out.clear();

    float maxRadius = MinF(rect.w, rect.h) * 0.5f;
    float r = MaxF(0.0f, MinF(radius, maxRadius));
    int segments = (segmentsOverride >= 0) ? segmentsOverride : ArcSegmentsForRadius(r);

    float left = rect.x;
    float top = rect.y;
    float right = rect.x + rect.w;
    float bottom = rect.y + rect.h;

    // 4つの角(右上→右下→左下→左上、画面上で時計回り)。各角は円弧の中心と開始角を持つ。
    // 角度は画面座標(y下向き)で、0=右、90度=下。
    struct Corner { float cx, cy, startDeg; };
    const Corner corners[4] =
    {
        { right - r, top + r,    -90.0f },
        { right - r, bottom - r,   0.0f },
        { left + r,  bottom - r,  90.0f },
        { left + r,  top + r,    180.0f },
    };

    constexpr float kPi = 3.14159265358979f;
    for (const Corner& corner : corners)
    {
        for (int i = 0; i <= segments; ++i)
        {
            float t = (segments > 0) ? static_cast<float>(i) / static_cast<float>(segments) : 0.0f;
            float angle = (corner.startDeg + 90.0f * t) * kPi / 180.0f;
            out.push_back(Pt{ corner.cx + std::cos(angle) * r, corner.cy + std::sin(angle) * r });
        }
    }

    // 角丸なし(segments==0)の時は、各角の点が円弧の中心(=角から半径ぶん内側)になってしまうので、
    // 本来の四隅の位置に直す。
    if (segments == 0)
    {
        out[0] = Pt{ right, top };
        out[1] = Pt{ right, bottom };
        out[2] = Pt{ left, bottom };
        out[3] = Pt{ left, top };
    }
}

void UIRenderer::FillRect(const UIRect& rect, const UIColor& color)
{
    FillRoundedRect(rect, 0.0f, color, color);
}

void UIRenderer::FillRoundedRect(const UIRect& rect, float radius, const UIColor& top, const UIColor& bottom)
{
    if (rect.w <= 0.0f || rect.h <= 0.0f)
    {
        return;
    }

    std::vector<Pt> points;
    BuildRoundedRectPoints(rect, radius, points);

    // 上端から下端へ、各頂点のy座標に応じて色を補間する(縦のグラデーション)。
    std::vector<UIColor> colors(points.size());
    for (size_t i = 0; i < points.size(); ++i)
    {
        float t = Clamp01((points[i].y - rect.y) / rect.h);
        colors[i] = LerpColor(top, bottom, t);
    }

    FillPolygonAA(points, colors);
}

void UIRenderer::StrokeRoundedRect(const UIRect& rect, float radius, float thickness, const UIColor& color)
{
    if (rect.w <= 0.0f || rect.h <= 0.0f || thickness <= 0.0f)
    {
        return;
    }

    float maxThickness = MinF(rect.w, rect.h) * 0.5f;
    float t = MinF(thickness, maxThickness);

    // 外側と内側で点の数(円弧の分割数)を揃える。揃っていないと、帯の外周と内周の点の対応が取れない。
    // 内側の半径が小さい/0でも、同じ分割数で作れば点が同じ位置に重なるだけで済む。
    float maxRadius = MinF(rect.w, rect.h) * 0.5f;
    int segments = ArcSegmentsForRadius(MaxF(0.0f, MinF(radius, maxRadius)));

    std::vector<Pt> outer;
    std::vector<Pt> inner;
    BuildRoundedRectPoints(rect, radius, outer, segments);

    UIRect innerRect{ rect.x + t, rect.y + t, rect.w - t * 2.0f, rect.h - t * 2.0f };
    float innerRadius = MaxF(0.0f, radius - t);
    BuildRoundedRectPoints(innerRect, innerRadius, inner, segments);

    if (outer.size() != inner.size())
    {
        return; // 起こらないはずだが、対応が取れない場合は描かない
    }

    size_t n = outer.size();
    std::vector<Pt> outerOffsets;
    std::vector<Pt> innerOffsets;
    ComputeOutwardOffsets(outer, outerOffsets);
    ComputeOutwardOffsets(inner, innerOffsets);

    constexpr float kHalfFeather = 0.5f;
    UIColor clear = WithAlpha(color, 0.0f);

    for (size_t i = 0; i < n; ++i)
    {
        size_t j = (i + 1) % n;

        // 実体の帯: 外側の輪郭から0.5px内側 〜 内側の輪郭から0.5px外側(縁のぼかしぶんを除く)。
        Pt oi{ outer[i].x - outerOffsets[i].x * kHalfFeather, outer[i].y - outerOffsets[i].y * kHalfFeather };
        Pt oj{ outer[j].x - outerOffsets[j].x * kHalfFeather, outer[j].y - outerOffsets[j].y * kHalfFeather };
        Pt ii{ inner[i].x + innerOffsets[i].x * kHalfFeather, inner[i].y + innerOffsets[i].y * kHalfFeather };
        Pt ij{ inner[j].x + innerOffsets[j].x * kHalfFeather, inner[j].y + innerOffsets[j].y * kHalfFeather };
        PushSolidQuad(oi, color, oj, color, ij, color, ii, color);

        // 外側の縁: 帯の外周(不透明)から0.5px外へ(透明)。
        Pt oOuterI{ outer[i].x + outerOffsets[i].x * kHalfFeather, outer[i].y + outerOffsets[i].y * kHalfFeather };
        Pt oOuterJ{ outer[j].x + outerOffsets[j].x * kHalfFeather, outer[j].y + outerOffsets[j].y * kHalfFeather };
        PushSolidQuad(oi, color, oOuterI, clear, oOuterJ, clear, oj, color);

        // 内側の縁: 帯の内周(不透明)から0.5px内へ(透明)。
        Pt iInnerI{ inner[i].x - innerOffsets[i].x * kHalfFeather, inner[i].y - innerOffsets[i].y * kHalfFeather };
        Pt iInnerJ{ inner[j].x - innerOffsets[j].x * kHalfFeather, inner[j].y - innerOffsets[j].y * kHalfFeather };
        PushSolidQuad(ii, color, iInnerI, clear, iInnerJ, clear, ij, color);
    }
}

void UIRenderer::FillCircle(float cx, float cy, float radius, const UIColor& color)
{
    if (radius <= 0.0f)
    {
        return;
    }

    int segments = static_cast<int>(radius * 1.5f);
    if (segments < 16) { segments = 16; }
    if (segments > 64) { segments = 64; }

    constexpr float kTwoPi = 6.28318530717959f;
    std::vector<Pt> points(static_cast<size_t>(segments));
    for (int i = 0; i < segments; ++i)
    {
        float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
        points[static_cast<size_t>(i)] = Pt{ cx + std::cos(angle) * radius, cy + std::sin(angle) * radius };
    }
    std::vector<UIColor> colors(points.size(), color);
    FillPolygonAA(points, colors);
}

void UIRenderer::FillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, const UIColor& color)
{
    std::vector<Pt> points = { Pt{ x0, y0 }, Pt{ x1, y1 }, Pt{ x2, y2 } };
    std::vector<UIColor> colors(3, color);
    FillPolygonAA(points, colors);
}

void UIRenderer::DrawLine(float x0, float y0, float x1, float y1, float thickness, const UIColor& color)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-4f || thickness <= 0.0f)
    {
        return;
    }

    // 1px未満の細い線は、太さを1pxにして薄さ(アルファ)で表現する(縁のぼかしで消えてしまうため)。
    float effectiveThickness = thickness;
    UIColor effectiveColor = color;
    if (effectiveThickness < 1.0f)
    {
        effectiveColor.w *= effectiveThickness;
        effectiveThickness = 1.0f;
    }

    float nx = -dy / length * effectiveThickness * 0.5f;
    float ny = dx / length * effectiveThickness * 0.5f;
    std::vector<Pt> points =
    {
        Pt{ x0 + nx, y0 + ny }, Pt{ x1 + nx, y1 + ny }, Pt{ x1 - nx, y1 - ny }, Pt{ x0 - nx, y0 - ny },
    };
    std::vector<UIColor> colors(4, effectiveColor);
    FillPolygonAA(points, colors);
}

float UIRenderer::MeasureString(const std::string& utf8, float pixelSize)
{
    float width = 0.0f;
    size_t index = 0;
    while (index < utf8.size())
    {
        uint32_t codepoint = 0;
        int consumed = DecodeUtf8(utf8.data() + index, utf8.size() - index, codepoint);
        if (consumed <= 0)
        {
            break;
        }
        index += static_cast<size_t>(consumed);

        const GlyphInfo* glyph = m_atlas.GetGlyph(codepoint, pixelSize);
        if (glyph)
        {
            width += glyph->advance;
        }
    }
    return width;
}

float UIRenderer::DrawString(float x, float y, const std::string& utf8, float pixelSize, const UITextStyle& style)
{
    if (!m_atlas.HasFont())
    {
        return 0.0f;
    }

    float ascent = m_atlas.GetAscent(pixelSize);
    float baseline = y + ascent;

    // 同じ文字列を、指定した位置のずれ・色で1回描く(縁取り・影・本体で使い回す)。
    auto drawPass = [&](float offsetX, float offsetY, const UIColor& color) -> float
    {
        float penX = x + offsetX;
        size_t index = 0;
        while (index < utf8.size())
        {
            uint32_t codepoint = 0;
            int consumed = DecodeUtf8(utf8.data() + index, utf8.size() - index, codepoint);
            if (consumed <= 0)
            {
                break;
            }
            index += static_cast<size_t>(consumed);

            const GlyphInfo* glyph = m_atlas.GetGlyph(codepoint, pixelSize);
            if (!glyph)
            {
                continue; // アトラスが満杯(次のフレームで焼き直す)
            }

            if (glyph->w > 0.0f && glyph->h > 0.0f)
            {
                // 文字がにじまないよう、画像の左上を整数のピクセルに合わせる。
                float gx = std::round(penX + glyph->xoff);
                float gy = std::round(baseline + offsetY + glyph->yoff);

                EnsureSpace(6);
                PushVertex(gx, gy, glyph->u0, glyph->v0, color);
                PushVertex(gx + glyph->w, gy, glyph->u1, glyph->v0, color);
                PushVertex(gx + glyph->w, gy + glyph->h, glyph->u1, glyph->v1, color);
                PushVertex(gx, gy, glyph->u0, glyph->v0, color);
                PushVertex(gx + glyph->w, gy + glyph->h, glyph->u1, glyph->v1, color);
                PushVertex(gx, gy + glyph->h, glyph->u0, glyph->v1, color);
            }
            penX += glyph->advance;
        }
        return penX - (x + offsetX);
    };

    if (style.shadow)
    {
        drawPass(style.shadowOffset, style.shadowOffset, style.shadowColor);
    }
    if (style.outline)
    {
        const float offsets[8][2] =
        {
            { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f },
            { -1.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f },
        };
        for (const auto& offset : offsets)
        {
            drawPass(offset[0], offset[1], style.outlineColor);
        }
    }

    return drawPass(0.0f, 0.0f, style.color);
}
