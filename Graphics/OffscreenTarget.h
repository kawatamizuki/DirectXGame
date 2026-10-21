#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

// 画面(スワップチェーン)ではなく、テクスチャに描くための入れ物。
// 色(RGBA8)と深度を持ち、描いた結果は、ImGuiなどで画像として表示する(GetSRV)か、CPUに読み戻す(ReadPixels)ことができる。
// サムネイルの生成や、モデルの詳細表示など、「画面とは別の小さな絵」が欲しい時に使う汎用の部品。
//
// 使い方: Begin(clearColor) → (Renderer::DrawModelなどで描く) → End()。
// Begin〜Endの間だけ描画先とビューポートがこのターゲットになり、Endで元(通常は画面)に戻る。
class OffscreenTarget
{
public:
    // 幅・高さのピクセル数を指定して作る。作り直す時は、先にReleaseするか、そのままInitializeを呼ぶ(前の物は解放される)。
    bool Initialize(ID3D11Device* device, UINT width, UINT height);
    void Release();

    bool IsValid() const { return m_colorTexture != nullptr; }
    UINT GetWidth() const { return m_width; }
    UINT GetHeight() const { return m_height; }

    // 描いた結果を画像として表示するためのビュー(ImGui::Imageなどに渡す)。
    ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }

    // 描画先をこのターゲットに切り替える(今の描画先・ビューポートは覚えておく)。色と深度をクリアする。
    // clearColorはRGBAの4つの値。Begin〜Endは対で呼ぶこと(入れ子にはできない)。
    void Begin(ID3D11DeviceContext* context, const float clearColor[4]);

    // Beginで覚えておいた描画先・ビューポートに戻す。
    void End(ID3D11DeviceContext* context);

    // 描いた色をCPUに読み戻す。outRgbaは幅*高さ*4バイト(1画素=R,G,B,A。行の隙間なし、上の行から)に詰めて返す。
    // GPUの完了を待つので少し重い(サムネイルの保存時だけ使う)。失敗したらfalse。
    bool ReadPixels(ID3D11DeviceContext* context, std::vector<uint8_t>& outRgba);

private:
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_colorTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_depthTexture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_dsv;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_staging; // 読み戻し用(最初のReadPixelsで作る)

    UINT m_width = 0;
    UINT m_height = 0;

    // Beginで覚えておく、元の描画先とビューポート。
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_savedRtv;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_savedDsv;
    D3D11_VIEWPORT m_savedViewport{};
    UINT m_savedViewportCount = 0;
    bool m_inScope = false;
};
