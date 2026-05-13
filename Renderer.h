#pragma once
#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <vector>
#include "Model.h"
#include "Transform.h"
#include "Camera.h"

class Renderer
{
public:
    Renderer();
    ~Renderer();

    bool Initialize(HWND hwnd);
    void BeginFrame();
    void SetVSyncEnabled(bool enabled);
    bool IsVSyncEnabled() const;
    ID3D11Device* GetDevice() const { return m_device.Get(); };
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
    void Update();
    void DrawTriangle();
    void DrawModel(const Model& model, const Transform& transform, const Camera& camera);
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
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerState;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_materialBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_triangleVertexBuffer;

    //Vsyncを使うか否か
    bool m_vsyncEnabled;

    //ウィンドウサイズ
    UINT m_windowWidth;
    UINT m_windowHeight;

   

    // 三角形用
    //ID3D11Buffer* m_triangleVertexBuffer;
    UINT m_triangleVertexCount;

};