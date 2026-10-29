#include "CityCameraController.h"
#include <algorithm>
#include <cmath>
#include "Camera.h"
#include "Collision.h"
#include "Ray.h"

using namespace DirectX;

namespace
{
    // 1フレームの時間の上限。ウィンドウを動かしていて長く止まった後などに、カメラが飛ばないようにする。
    constexpr float kMaxDeltaSeconds = 0.1f;
}

void CityCameraController::SetBounds(float halfWidth, float halfDepth)
{
    m_boundsHalfWidth = (std::max)(0.0f, halfWidth);
    m_boundsHalfDepth = (std::max)(0.0f, halfDepth);
}

XMFLOAT3 CityCameraController::ComputeCameraPosition(const XMFLOAT3& focus, float yaw, float pitch, float distance)
{
    // 向き(yaw)は、水平に見る方向 (sin yaw, 0, cos yaw)。カメラは、その反対側の斜め上に居る。
    const float horizontal = distance * std::cos(pitch);
    return XMFLOAT3(
        focus.x - std::sin(yaw) * horizontal,
        focus.y + distance * std::sin(pitch),
        focus.z - std::cos(yaw) * horizontal);
}

bool CityCameraController::RaycastGround(const Camera& camera, float cursorX, float cursorY, float viewWidth, float viewHeight, XMFLOAT3& outPoint)
{
    if (viewWidth < 1.0f || viewHeight < 1.0f)
    {
        return false;
    }

    POINT point{ static_cast<LONG>(cursorX), static_cast<LONG>(cursorY) };
    const Ray ray = camera.ScreenPointToRay(point, static_cast<UINT>(viewWidth), static_cast<UINT>(viewHeight));

    // 地面(y=0)より下を向いている(レイが地面へ向かっている)時だけ、当たりとする。
    // (IntersectRayPlaneは、平面がレイの後ろ側にある時も、交点を返すことがあるため)
    if (ray.direction.y >= -1e-4f)
    {
        return false;
    }
    return ::IntersectRayPlane(ray, XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), outPoint);
}

Camera CityCameraController::MakeCameraAt(const Camera& camera, const XMFLOAT3& focus, float distance) const
{
    Camera result = camera; // 投影(視野角・縦横比)は、今のカメラと同じ
    const XMFLOAT3 position = ComputeCameraPosition(focus, m_yaw, m_settings.pitchRadians, distance);
    result.SetPosition(position.x, position.y, position.z);
    result.SetTarget(focus.x, focus.y, focus.z);
    return result;
}

void CityCameraController::ClampTargetFocus()
{
    const float limitX = m_boundsHalfWidth + m_settings.boundsMargin;
    const float limitZ = m_boundsHalfDepth + m_settings.boundsMargin;
    m_targetFocus.x = (std::max)(-limitX, (std::min)(limitX, m_targetFocus.x));
    m_targetFocus.z = (std::max)(-limitZ, (std::min)(limitZ, m_targetFocus.z));
    m_targetFocus.y = 0.0f; // 注視点は、いつも地面の上
    m_targetDistance = (std::max)(m_settings.minDistance, (std::min)(m_settings.maxDistance, m_targetDistance));
}

void CityCameraController::ApplyToCamera(Camera& camera) const
{
    const XMFLOAT3 position = ComputeCameraPosition(m_focus, m_yaw, m_settings.pitchRadians, m_distance);
    camera.SetPosition(position.x, position.y, position.z);
    camera.SetTarget(m_focus.x, m_focus.y, m_focus.z);
}

void CityCameraController::Reset(Camera& camera)
{
    m_yaw = 0.0f;
    m_targetFocus = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_targetDistance = m_settings.defaultDistance;
    ClampTargetFocus();
    m_focus = m_targetFocus;
    m_distance = m_targetDistance;
    ApplyToCamera(camera);
}

void CityCameraController::AdoptFrom(Camera& camera)
{
    const XMFLOAT3 position = camera.GetPosition();
    const XMFLOAT3 forward = camera.GetForward();

    // 向き(yaw)は、今の視線の水平成分から。真上・真下を向いていて水平成分が無ければ、今のyawのまま。
    const float horizontalLength = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (horizontalLength > 1e-3f)
    {
        m_yaw = std::atan2(forward.x, forward.z);
    }

    // 注視点: 視線が地面(y=0)に当たる点。当たらない(水平・上向き)時は、カメラの真下から、水平方向へ既定の距離だけ先。
    if (forward.y < -0.05f && position.y > 0.0f)
    {
        const float t = -position.y / forward.y;
        m_targetFocus = XMFLOAT3(position.x + forward.x * t, 0.0f, position.z + forward.z * t);
        m_targetDistance = t;
    }
    else
    {
        const float lead = m_settings.defaultDistance * std::cos(m_settings.pitchRadians);
        m_targetFocus = XMFLOAT3(position.x + std::sin(m_yaw) * lead, 0.0f, position.z + std::cos(m_yaw) * lead);
        m_targetDistance = m_settings.defaultDistance;
    }

    ClampTargetFocus();
    m_focus = m_targetFocus;
    m_distance = m_targetDistance;
    ApplyToCamera(camera);
}

void CityCameraController::Update(const CityCameraInput& input, Camera& camera)
{
    const float dt = (std::max)(0.0f, (std::min)(kMaxDeltaSeconds, input.deltaSeconds));

    // ---- 1. キーでの平行移動(向きに合わせて、奥・手前・左右へ) ----
    // 向き(sin yaw, 0, cos yaw)が「奥」、その右(cos yaw, 0, -sin yaw)が「右」。斜めに押した時に速くなりすぎないよう、長さを1にそろえる。
    float panX = input.panX;
    float panZ = input.panZ;
    const float panLength = std::sqrt(panX * panX + panZ * panZ);
    if (panLength > 1.0f)
    {
        panX /= panLength;
        panZ /= panLength;
    }
    if (panLength > 0.0f && dt > 0.0f)
    {
        const float speed = m_settings.panSpeedPerDistance * m_targetDistance * (input.fast ? m_settings.fastMultiplier : 1.0f);
        const float sinYaw = std::sin(m_yaw);
        const float cosYaw = std::cos(m_yaw);
        m_targetFocus.x += (sinYaw * panZ + cosYaw * panX) * speed * dt;
        m_targetFocus.z += (cosYaw * panZ - sinYaw * panX) * speed * dt;
    }

    // ---- 2. ドラッグ(地面をつかんで動かす) ----
    // 前のフレームのカーソルの下の地面の点と、今のカーソルの下の地面の点の差だけ、注視点を動かす。
    // こうすると、つかんだ地面の点が、カーソルの下に残る(ズームの距離によらず、手に付いてくる)。
    // つかむ動きは、遅れずに付いてくる必要があるので、今の値にも同じだけ足す(なめらかな追従をしない)。
    if (input.dragging && input.cursorValid)
    {
        // 画面に見えている「今の」カメラで求める(目標のカメラでなく)。見えている地面の点が、手に付いてくるように。
        const Camera visibleCamera = MakeCameraAt(camera, m_focus, m_distance);
        XMFLOAT3 grabbed;
        XMFLOAT3 now;
        if (RaycastGround(visibleCamera, input.prevCursorX, input.prevCursorY, input.viewWidth, input.viewHeight, grabbed) &&
            RaycastGround(visibleCamera, input.cursorX, input.cursorY, input.viewWidth, input.viewHeight, now))
        {
            // dragScaleが1より小さいと、つかんだ点が、カーソルより少なく動く(感度を下げる)。
            const float deltaX = (grabbed.x - now.x) * input.dragScale;
            const float deltaZ = (grabbed.z - now.z) * input.dragScale;
            m_targetFocus.x += deltaX;
            m_targetFocus.z += deltaZ;
            m_focus.x += deltaX;
            m_focus.z += deltaZ;
        }
    }

    // ---- 3. ズーム(縮尺) ----
    // 距離を、ホイールの目盛りぶん掛ける。カーソルの下の地面の点 H が、画面の同じ場所に残るよう、
    // 注視点も H を中心に同じ比率で縮める(カメラの位置も、同じ比率で H へ寄る・離れる)。
    if (input.wheelNotches != 0.0f)
    {
        const float requested = m_targetDistance * std::pow(m_settings.zoomPerNotch, input.wheelNotches);
        const float newDistance = (std::max)(m_settings.minDistance, (std::min)(m_settings.maxDistance, requested));
        const float ratio = newDistance / m_targetDistance;

        XMFLOAT3 anchor;
        if (input.cursorValid &&
            RaycastGround(MakeCameraAt(camera, m_focus, m_distance), input.cursorX, input.cursorY, input.viewWidth, input.viewHeight, anchor))
        {
            m_targetFocus.x = anchor.x + (m_targetFocus.x - anchor.x) * ratio;
            m_targetFocus.z = anchor.z + (m_targetFocus.z - anchor.z) * ratio;
        }
        m_targetDistance = newDistance;
    }

    // ---- 追跡: 目標の注視点を、追跡する点にする(ズームの後。ズームで、追跡する点から外れないように) ----
    if (input.followActive)
    {
        m_targetFocus.x = input.followX;
        m_targetFocus.z = input.followZ;
    }

    ClampTargetFocus();

    // ---- 4. 目標へ、なめらかに追いつく(1 - exp(-k*dt) は、フレームの長さによらず、同じ速さで近づく) ----
    const float follow = 1.0f - std::exp(-m_settings.smoothing * dt);
    m_focus.x += (m_targetFocus.x - m_focus.x) * follow;
    m_focus.z += (m_targetFocus.z - m_focus.z) * follow;
    m_focus.y = 0.0f;
    m_distance += (m_targetDistance - m_distance) * follow;

    ApplyToCamera(camera);
}
