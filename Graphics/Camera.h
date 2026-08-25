#pragma once
#include <DirectXMath.h>

class Camera
{
public:
    Camera();

    void Update();

    DirectX::XMMATRIX GetViewMatrix() const;
    DirectX::XMMATRIX GetProjectionMatrix() const;

    void SetPosition(float x, float y, float z);
    void SetTarget(float x, float y, float z);
    void SetProjection(float fovY, float aspect, float nearZ, float farZ);

private:
    DirectX::XMFLOAT3 m_position;
    DirectX::XMFLOAT3 m_target;
    DirectX::XMFLOAT3 m_up;

    float m_fovY;
    float m_aspect;
    float m_nearZ;
    float m_farZ;
};
