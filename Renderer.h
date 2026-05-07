#pragma once
#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include "Model.h"
#include "Transform.h"

class Renderer
{
public:
    Renderer();
    ~Renderer();

    bool Initialize(HWND hwnd);
    void BeginFrame();
    ID3D11Device* GetDevice() const { return m_device; };
    void Update();
    void DrawTriangle();
    void DrawObj();
    void DrawModel(const Model& model, const Transform& transform);
    void EndFrame();
    void Finalize();

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    IDXGISwapChain* m_swapChain;
    ID3D11RenderTargetView* m_renderTargetView;
    ID3D11Texture2D* m_depthStencilBuffer;
    ID3D11DepthStencilView* m_depthStencilView;
    ID3D11DepthStencilState* m_depthStencilState;
    ID3D11VertexShader* m_vertexShader;
    ID3D11PixelShader* m_pixelShader;
    ID3D11InputLayout* m_inputLayout;
    ID3D11SamplerState* m_samplerState;

    //ウィンドウサイズ
    UINT m_windowWidth;
    UINT m_windowHeight;

     //三角形とobjで分ける
  /*  ID3D11Buffer* m_vertexBuffer;*/
    ID3D11Buffer* m_constantBuffer;
  /*  UINT m_vertexCount;*/

    // 三角形用
    ID3D11Buffer* m_triangleVertexBuffer;
    UINT m_triangleVertexCount;

   /* Model m_objModel;
    Transform m_objTransform;*/

    //// OBJ用
    //ID3D11Buffer* m_objVertexBuffer;
    //UINT m_objVertexCount;

    ////回転用(仮）
    //float m_objAngle=0;
   

};