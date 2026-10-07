#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include "FontAtlas.h"

// 画面上の長方形(ピクセル単位、左上原点)。
struct UIRect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool Contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    float Right() const { return x + w; }
    float Bottom() const { return y + h; }
};

using UIColor = DirectX::XMFLOAT4;

// 文字の描き方。縁取り・影は、背景の色が変わっても読めるようにするためのもの。
struct UITextStyle
{
    UIColor color{ 0.0f, 0.0f, 0.0f, 1.0f };
    bool outline = false;
    UIColor outlineColor{ 1.0f, 1.0f, 1.0f, 0.85f };
    bool shadow = false;
    UIColor shadowColor{ 0.0f, 0.0f, 0.0f, 0.35f };
    float shadowOffset = 1.5f;
};

// ゲームプレイ用UIの2D描画の土台(ImGuiとは独立)。
// ピクセル座標(左上原点)の図形と文字を頂点として溜めておき、EndFrameで1回のDrawにまとめて描く。
// 図形も文字も同じ1枚のテクスチャ(FontAtlas。左上に図形用の白い領域がある)を使うので、
// 画像素材(ドット絵など)を後から足す時も、同じアトラスに画像を詰める形で拡張できる。
// 呼び出しの流れ: BeginFrame(画面サイズ) → FillRect等/DrawString → EndFrame()。
class UIRenderer
{
public:
    // device/contextはRendererのものを借りる。シェーダーは実行時にファイルから読む
    // (SimpleShader.hlslなどと同じ。作業フォルダ基準)。
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, const wchar_t* shaderPath = L"UIShader.hlsl");

    // 1フレームの描画を始める。screenW/screenHは今のウィンドウ(描画先)の大きさ。
    // 前のフレームでアトラスが満杯になっていた場合は、ここで焼き直す。
    void BeginFrame(float screenW, float screenH);

    // 溜めた図形・文字をまとめて描く。
    void EndFrame();

    // ---- 図形 ----
    void FillRect(const UIRect& rect, const UIColor& color);

    // 角丸の四角。上端の色topから下端の色bottomへ縦にグラデーションする(同じ色なら単色)。
    void FillRoundedRect(const UIRect& rect, float radius, const UIColor& top, const UIColor& bottom);

    // 角丸の四角の枠線(内側にthicknessぶんの太さ)。
    void StrokeRoundedRect(const UIRect& rect, float radius, float thickness, const UIColor& color);

    void FillCircle(float cx, float cy, float radius, const UIColor& color);
    void FillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, const UIColor& color);
    void DrawLine(float x0, float y0, float x1, float y1, float thickness, const UIColor& color);

    // ---- 文字 ----
    // (x, y)は1行ぶんの箱の左上。pixelSizeは行の高さ(ピクセル)。描いた幅を返す。
    // 名前をDrawTextにしないのは、windows.hのDrawTextマクロ(DrawTextA/W)に置き換えられてしまうため。
    float DrawString(float x, float y, const std::string& utf8, float pixelSize, const UITextStyle& style);
    float MeasureString(const std::string& utf8, float pixelSize);
    float GetLineHeight(float pixelSize) const { return m_atlas.GetLineHeight(pixelSize); }
    // 行の上端からベースラインまでの距離(大きさの違う文字をベースラインで揃える時に使う)。
    float GetAscent(float pixelSize) const { return m_atlas.GetAscent(pixelSize); }
    bool HasFont() const { return m_atlas.HasFont(); }

    // テクスチャの拡大縮小を最近傍(くっきり)にするか線形(なめらか)にするか。
    // ドット絵素材を使う時のために用意してある(今の図形・文字は線形のままでよい)。
    void SetPointSampling(bool enabled) { m_pointSampling = enabled; }

private:
    struct Vertex
    {
        float x, y;
        float u, v;
        float r, g, b, a;
    };

    struct Pt
    {
        float x, y;
    };

    // 頂点を溜める上限(超えそうになったらその時点で一度描いて空にする)。3の倍数。
    static constexpr size_t kMaxVertices = 24576;
    static constexpr int kAtlasSize = 1024;

    void Flush();
    void EnsureSpace(size_t vertexCount);
    void PushVertex(float x, float y, float u, float v, const UIColor& c);
    void PushSolidTriangle(const Pt& a, const UIColor& ca, const Pt& b, const UIColor& cb, const Pt& c, const UIColor& cc);
    void PushSolidQuad(const Pt& a, const UIColor& ca, const Pt& b, const UIColor& cb, const Pt& c, const UIColor& cc, const Pt& d, const UIColor& cd);
    void UploadAtlasIfDirty();

    // 凸多角形を、外周に幅1pxの透明へ向かう縁を付けて塗る(ジャギー対策)。colorsは各頂点の色。
    void FillPolygonAA(const std::vector<Pt>& points, const std::vector<UIColor>& colors);

    // 角丸四角の外周の点を時計回りに作る。radiusが小さい/0なら4隅の点だけになる。
    // segmentsOverrideが0以上なら、半径から決まる円弧の分割数の代わりにその値を使う
    // (枠線の外側と内側で点の数を揃えるため)。
    static void BuildRoundedRectPoints(const UIRect& rect, float radius, std::vector<Pt>& out, int segmentsOverride = -1);

    // 凸多角形の各頂点の外向きの法線(ミター。辺からの距離がちょうど1になる長さ)を求める。
    static void ComputeOutwardOffsets(const std::vector<Pt>& points, std::vector<Pt>& outOffsets);

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_atlasTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_atlasView;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_linearSampler;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_pointSamplerState;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState;

    FontAtlas m_atlas;
    std::vector<Vertex> m_vertices;
    float m_screenW = 1.0f;
    float m_screenH = 1.0f;
    float m_solidU = 0.0f;
    float m_solidV = 0.0f;
    bool m_pointSampling = false;
    bool m_initialized = false;
};
