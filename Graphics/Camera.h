#pragma once
#include <DirectXMath.h>
#include <Windows.h>
#include "Ray.h"

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

    // スクリーン座標(クライアント座標)からワールド空間のRayを生成する
    // mousePos    : InputManager::GetMousePosition() の値(クライアント座標)
    // windowWidth / windowHeight : Renderer::GetWindowWidth/Height()
    Ray ScreenPointToRay(const POINT& mousePos, UINT windowWidth, UINT windowHeight) const;

    // ワールド座標 → スクリーン座標(クライアント座標、ピクセル)。ImGuiのオーバーレイを
    // ワールド上の位置に追従させるために使う(ScreenPointToRayの逆方向)。
    // カメラの後ろ側にある場合(描画されない位置)はfalseを返す。
    bool WorldToScreen(const DirectX::XMFLOAT3& worldPos, UINT windowWidth, UINT windowHeight, POINT& outScreenPos) const;

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
