#pragma once
#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <vector>
#include "Model.h"
#include "Transform.h"
#include "Camera.h"
#include "LightingState.h"

class Renderer
{
public:
    Renderer();
    ~Renderer();

    bool Initialize(HWND hwnd);

    // 次のBeginFrame以降の描画に使う光の状態を設定する(空の色=画面クリア色もここから決まる)。
    // 光の状態を誰がどう決めるか(昼夜サイクルなど)はRendererは知らない。毎フレーム、
    // BeginFrameの前に呼ぶ。一度も呼ばれない場合はLightingStateの既定値(昼の光)で描く。
    void SetLighting(const LightingState& lighting);

    void BeginFrame();
    void Resize(UINT width, UINT height);
    void SetVSyncEnabled(bool enabled);
    bool IsVSyncEnabled() const;
    ID3D11Device* GetDevice() const { return m_device.Get(); };
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
    void Update();
    UINT GetWindowWidth() const { return m_windowWidth; }
    UINT GetWindowHeight() const { return m_windowHeight; }
    void DrawTriangle();
    // alpha: 1.0未満を渡すと半透明合成される(配置プレビューのゴースト表示などに使う)。省略時は不透明。
    void DrawModel(const Model& model, const Transform& transform, const Camera& camera, float alpha = 1.0f);
    void EndFrame();
    void Finalize();
    
  

private:
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_depthStencilBuffer;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_depthStencilView;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStencilState;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState; // 半透明合成(アルファブレンド)用
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerState;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_materialBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_lightBuffer; // ピクセルシェーダー用の光の状態(register b2)
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_triangleVertexBuffer;

    // 今フレームで使う光の状態(SetLightingで更新される)。
    LightingState m_lighting;

    //Vsyncを使うか否か
    bool m_vsyncEnabled;

    //ウィンドウサイズ
    UINT m_windowWidth;
    UINT m_windowHeight;

   

    // 三角形用
    //ID3D11Buffer* m_triangleVertexBuffer;
    UINT m_triangleVertexCount;

};