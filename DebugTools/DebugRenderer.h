#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <vector>
#include"DirectXMath.h"


class Camera;

class DebugRenderer
{
public:
    DebugRenderer();
   ~DebugRenderer();
    

    bool Initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context
    );

    void AddLine(
        const DirectX::XMFLOAT3& start,
        const DirectX::XMFLOAT3& end,
        const DirectX::XMFLOAT4& color
    );

    void AddOBB(
        const DirectX::XMFLOAT3& localMin,
        const DirectX::XMFLOAT3& localMax,
        const DirectX::XMMATRIX& world,
        const DirectX::XMFLOAT4& color
    );

    void Flush(const Camera& camera);
    void Clear();

    void Finalize();



private:
    struct DebugLineVertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT4 color;
    };

private:
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    std::vector<DebugLineVertex> m_vertices;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
};