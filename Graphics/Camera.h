#pragma once
#include <DirectXMath.h>

class Camera
{
public:
    Camera();

    void Update();

    DirectX::XMMATRIX GetViewMatrix() const;
    DirectX::XMMATRIX GetProjectionMatrix() const;
    DirectX::XMFLOAT3 GetForward() const;
    DirectX::XMFLOAT3 GetRight() const;

   const  DirectX::XMFLOAT3& GetPosition() const;
   float GetFovY() const;

    void AddYawPitch(float yawDelta, float pitchDelta);
    void MoveForward(float distance);
    void MoveRight(float distance);
    void MoveUp(float distance);
    void Focus( const DirectX::XMFLOAT3& target,float distance);

    void SetPosition(float x, float y, float z);
    void SetTarget(float x, float y, float z);
    void SetProjection(float fovY, float aspect, float nearZ, float farZ);

private:
    void UpdateTargetFromYawPitch();

    DirectX::XMFLOAT3 m_position;
    DirectX::XMFLOAT3 m_target;
    DirectX::XMFLOAT3 m_up;

    float m_fovY;
    float m_aspect;
    float m_nearZ;
    float m_farZ;

    float m_yaw;
    float m_pitch;
};
