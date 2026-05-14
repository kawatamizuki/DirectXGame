#include "Camera.h"

using namespace DirectX;

Camera::Camera()
    : m_position(0.0f, 0.0f, -10.0f)
    , m_target(0.0f, 0.0f, 0.0f)
    , m_up(0.0f, 1.0f, 0.0f)
    , m_fovY(XMConvertToRadians(45.0f))
    , m_aspect(800.0f / 600.0f)
    , m_nearZ(0.1f)
    , m_farZ(100.0f)
{
}

void Camera::Update()
{
}

XMMATRIX Camera::GetViewMatrix() const
{
    XMVECTOR eye = XMLoadFloat3(&m_position);
    XMVECTOR target = XMLoadFloat3(&m_target);
    XMVECTOR up = XMLoadFloat3(&m_up);
    XMVECTOR direction = XMVectorSubtract(target, eye);

    float lengthSq = XMVectorGetX(XMVector3LengthSq(direction));

    // position ‚Æ target ‚ª‹ß‚·‚¬‚é‚Æ‹ü•ûŒü‚ªì‚ê‚È‚¢‚½‚ß•â³‚·‚é
    if (lengthSq < 0.0001f)
    {
        target = XMVectorAdd(
            eye,
            XMVectorSet(0.0f, 0.0f, 0.01f, 0.0f)
        );
    }
    return XMMatrixLookAtLH(eye, target, up);
}

XMMATRIX Camera::GetProjectionMatrix() const
{
    return XMMatrixPerspectiveFovLH(
        m_fovY,
        m_aspect,
        m_nearZ,
        m_farZ
    );
}

void Camera::SetPosition(float x, float y, float z)
{
    m_position = { x, y, z };
}

void Camera::SetTarget(float x, float y, float z)
{
    m_target = { x, y, z };
}

void Camera::SetProjection(float fovY, float aspect, float nearZ, float farZ)
{
    m_fovY = fovY;
    m_aspect = aspect;
    m_nearZ = nearZ;
    m_farZ = farZ;
}