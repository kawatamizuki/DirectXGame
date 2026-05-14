#include "Transform.h"

using namespace DirectX;

XMMATRIX Transform::GetWorldMatrix() const
{
    XMMATRIX s = XMMatrixScaling(scale.x, scale.y, scale.z);
    XMMATRIX r = XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z);
    XMMATRIX t = XMMatrixTranslation(position.x, position.y, position.z);

    return s * r * t;
}