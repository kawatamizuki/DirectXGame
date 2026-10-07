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
    , m_yaw(0.0f)
    , m_pitch(0.0f)
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

    // position と target が近すぎると視線方向が作れないため補正する
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

DirectX::XMFLOAT3 Camera::GetForward() const
{
    using namespace DirectX;

    XMVECTOR forward =
        XMVector3Normalize(
            XMLoadFloat3(&m_target) -
            XMLoadFloat3(&m_position)
        );

    XMFLOAT3 result;
    XMStoreFloat3(&result, forward);

    return result;
}

 const DirectX::XMFLOAT3&Camera::GetPosition() const
{
    return m_position;
}

 float Camera::GetFovY() const
 {
     return m_fovY;
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

// ========================================
// yaw / pitch から
// target方向を更新する関数
// ========================================
// FreeCamera用
//
// yaw   : 左右回転
// pitch : 上下回転
//
// position + forward を target にする
// ========================================
void Camera::UpdateTargetFromYawPitch()
{
    using namespace DirectX;

    // ========================================
    // forwardベクトル作成
    // ========================================
    // yaw / pitch から
    // 前方向ベクトルを計算
    //
    // X : 左右
    // Y : 上下
    // Z : 前後
    // ========================================
    XMVECTOR forward =
        XMVectorSet(
            cosf(m_pitch) * sinf(m_yaw),
            sinf(m_pitch),
            cosf(m_pitch) * cosf(m_yaw),
            0.0f
        );

    // 正規化
    forward =
        XMVector3Normalize(forward);

    // ========================================
    // 現在位置
    // ========================================
    XMVECTOR pos =
        XMLoadFloat3(&m_position);

    // ========================================
    // target = position + forward
    // ========================================
    XMVECTOR target =
        pos + forward;

    // XMFLOAT3へ保存
    XMStoreFloat3(
        &m_target,
        target
    );
}

// ========================================
// yaw / pitch 加算
// ========================================
// マウス移動量を加算する用
// ========================================
void Camera::AddYawPitch(
    float yawDelta,
    float pitchDelta)
{
    // ========================================
    // 回転加算
    // ========================================
    m_yaw += yawDelta;
    m_pitch += pitchDelta;

    // ========================================
    // pitch制限
    // ========================================
    // 真上/真下を向くと
    // カメラが壊れるため制限
    // ========================================
    float limit =
        DirectX::XMConvertToRadians(89.0f);

    if (m_pitch > limit)
    {
        m_pitch = limit;
    }

    if (m_pitch < -limit)
    {
        m_pitch = -limit;
    }

    // ========================================
    // target更新
    // ========================================
    UpdateTargetFromYawPitch();
}

// ========================================
// カメラ右方向取得
// ========================================
// A/D移動用
// ========================================
DirectX::XMFLOAT3 Camera::GetRight() const
{
    using namespace DirectX;

    // ========================================
    // 前方向
    // ========================================
    XMVECTOR forward =
        XMLoadFloat3(&m_target) -
        XMLoadFloat3(&m_position);

    forward =
        XMVector3Normalize(forward);

    // ========================================
    // 上方向
    // ========================================
    XMVECTOR up =
        XMLoadFloat3(&m_up);

    // ========================================
    // right = up × forward
    // ========================================
    XMVECTOR right =
        XMVector3Cross(up, forward);

    right =
        XMVector3Normalize(right);

    XMFLOAT3 result;

    XMStoreFloat3(
        &result,
        right
    );

    return result;
}

// ========================================
// 前後移動
// ========================================
// W / S移動用
// ========================================
void Camera::MoveForward(float distance)
{
    // 前方向取得
    DirectX::XMFLOAT3 forward =
        GetForward();

    // ========================================
    // position移動
    // ========================================
    m_position.x += forward.x * distance;
    m_position.y += forward.y * distance;
    m_position.z += forward.z * distance;

    // ========================================
    // targetも同時移動
    // ========================================
    // 視線方向維持
    // ========================================
    m_target.x += forward.x * distance;
    m_target.y += forward.y * distance;
    m_target.z += forward.z * distance;
}

// ========================================
// 左右移動
// ========================================
// A / D移動用
// ========================================
void Camera::MoveRight(float distance)
{
    // 右方向取得
    DirectX::XMFLOAT3 right =
        GetRight();

    // ========================================
    // position移動
    // ========================================
    m_position.x += right.x * distance;
    m_position.y += right.y * distance;
    m_position.z += right.z * distance;

    // ========================================
    // targetも同時移動
    // ========================================
    m_target.x += right.x * distance;
    m_target.y += right.y * distance;
    m_target.z += right.z * distance;
}

// ========================================
// 上下移動
// ========================================
void Camera::MoveUp(float distance)
{
    // position移動
    m_position.y += distance;

    // targetも移動
    m_target.y += distance;
}

// ========================================
// 選択中のオブジェクトにカメラを向ける
// ========================================
void Camera::Focus(const DirectX::XMFLOAT3& target,float distance)
{
    using namespace DirectX;

    XMFLOAT3 forward =
        GetForward();

    m_target = target;

    m_position =
    {
        target.x - forward.x * distance,
        target.y - forward.y * distance,
        target.z - forward.z * distance
    };
}

// ========================================
// スクリーン座標(クライアント座標) → ワールド空間Ray
// ========================================
// DebugEditor::CreateMouseRay から移動(Cameraの情報だけ使うのでここに位置)
// ========================================
Ray Camera::ScreenPointToRay(const POINT& mousePos, UINT windowWidth, UINT windowHeight) const
{
    Ray ray;

    float mouseX = static_cast<float>(mousePos.x);
    float mouseY = static_cast<float>(mousePos.y);

    float width = static_cast<float>(windowWidth);
    float height = static_cast<float>(windowHeight);

    // NDC変換
    float ndcX = (2.0f * mouseX / width) - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY / height);

    XMMATRIX projection = GetProjectionMatrix();
    XMMATRIX view = GetViewMatrix();

    XMMATRIX invView = XMMatrixInverse(nullptr, view);
    XMMATRIX invProj = XMMatrixInverse(nullptr, projection);

    // Near座標
    XMVECTOR nearPoint = XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
    nearPoint = XMVector3TransformCoord(nearPoint, invProj);
    nearPoint = XMVector3TransformCoord(nearPoint, invView);

    // Far座標
    XMVECTOR farPoint = XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);
    farPoint = XMVector3TransformCoord(farPoint, invProj);
    farPoint = XMVector3TransformCoord(farPoint, invView);

    // Ray方向
    XMVECTOR direction = XMVector3Normalize(farPoint - nearPoint);

    XMStoreFloat3(&ray.origin, nearPoint);
    XMStoreFloat3(&ray.direction, direction);

    return ray;
}

// ========================================
// ワールド座標 → スクリーン座標(クライアント座標)
// ========================================
// ScreenPointToRayの逆方向。ImGuiのオーバーレイUIをワールド上の位置に
// 追従させるために使う(道路配置の確認アイコンなど)。
// ========================================
bool Camera::WorldToScreen(const XMFLOAT3& worldPos, UINT windowWidth, UINT windowHeight, POINT& outScreenPos) const
{
    XMMATRIX view = GetViewMatrix();
    XMMATRIX projection = GetProjectionMatrix();

    XMVECTOR worldVec = XMLoadFloat3(&worldPos);
    XMVECTOR viewSpace = XMVector3TransformCoord(worldVec, view);

    // ビュー空間Z(LH座標系では前方が+Z)がnearZより手前(=カメラの後ろ側)なら描画されない
    if (XMVectorGetZ(viewSpace) < m_nearZ)
    {
        return false;
    }

    XMVECTOR clipSpace = XMVector3TransformCoord(viewSpace, projection);

    float ndcX = XMVectorGetX(clipSpace);
    float ndcY = XMVectorGetY(clipSpace);

    float width = static_cast<float>(windowWidth);
    float height = static_cast<float>(windowHeight);

    outScreenPos.x = static_cast<LONG>((ndcX * 0.5f + 0.5f) * width);
    outScreenPos.y = static_cast<LONG>((1.0f - (ndcY * 0.5f + 0.5f)) * height);

    return true;
}
