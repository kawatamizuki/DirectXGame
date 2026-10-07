#pragma once
#include <DirectXMath.h>

class Transform
{
public:
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    // Inspector表示・入力用Euler角
    DirectX::XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f };

    // 実際の回転姿勢
    DirectX::XMFLOAT4 rotationQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
    DirectX::XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };

    void SetRotationEuler(const DirectX::XMFLOAT3& euler);
    void SyncEulerFromQuaternion();//rotationQuatからrotation(Eular)に変換
    void NormalizeRotation();

    DirectX::XMMATRIX GetWorldMatrix() const;
};