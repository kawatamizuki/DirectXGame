#pragma once
#include <DirectXMath.h>

class Transform
{
public:
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    // Inspectorï\é¶ÅEì¸óÕópEuleräp
    DirectX::XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f };

    // é¿ç€ÇÃâÒì]épê®
    DirectX::XMFLOAT4 rotationQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
    DirectX::XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };

    void SetRotationEuler(const DirectX::XMFLOAT3& euler);
    void SyncEulerFromQuaternion();//rotationQuatÇ©ÇÁrotation(Eular)Ç…ïœä∑
    void NormalizeRotation();

    DirectX::XMMATRIX GetWorldMatrix() const;
};