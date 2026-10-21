#include "ModelPreviewRenderer.h"
#include <algorithm>
#include <cmath>
#include "Renderer.h"
#include "Model.h"
#include "OffscreenTarget.h"
#include "Camera.h"
#include "Transform.h"

using namespace DirectX;

namespace
{
    // 描画する時のモデルの最大の辺の長さ。カメラの距離(約3.4)と視野角(40度)から、画面にちょうど収まる大きさ。
    constexpr float kFittedSize = 1.6f;

    // カメラの基準の距離。zoomで割る。
    constexpr float kBaseDistance = 3.4f;
}

void ModelPreviewRenderer::Initialize(Renderer* renderer)
{
    m_renderer = renderer;
}

const float* ModelPreviewRenderer::BackgroundColor()
{
    static const float color[4] = { 0.62f, 0.80f, 0.95f, 1.0f };
    return color;
}

float ModelPreviewRenderer::ComputeFitScale(const Model& model)
{
    const XMFLOAT3 boundsMin = model.GetBoundsMin();
    const XMFLOAT3 boundsMax = model.GetBoundsMax();
    float longest = (std::max)({ boundsMax.x - boundsMin.x, boundsMax.y - boundsMin.y, boundsMax.z - boundsMin.z, 1e-4f });
    return kFittedSize / longest;
}

float ModelPreviewRenderer::GetFittedSize()
{
    return kFittedSize;
}

Transform ModelPreviewRenderer::MakeFittedTransform(const Model& model, const XMFLOAT3& center, float yawRadians)
{
    const XMFLOAT3 boundsMin = model.GetBoundsMin();
    const XMFLOAT3 boundsMax = model.GetBoundsMax();
    const float scale = ComputeFitScale(model);

    Transform transform;
    transform.scale = { scale, scale, scale };

    // 回転は、モデルの中心のまわりで行う。回転してから平行移動するので、「中心を原点へ」ずらした分も回して足す。
    const XMVECTOR rotation = XMQuaternionRotationAxis(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), yawRadians);
    XMStoreFloat4(&transform.rotationQuat, rotation);

    const XMVECTOR localCenter = XMVectorSet(
        (boundsMin.x + boundsMax.x) * 0.5f * scale,
        (boundsMin.y + boundsMax.y) * 0.5f * scale,
        (boundsMin.z + boundsMax.z) * 0.5f * scale, 0.0f);
    const XMVECTOR rotatedCenter = XMVector3Rotate(localCenter, rotation);
    transform.position =
    {
        center.x - XMVectorGetX(rotatedCenter),
        center.y - XMVectorGetY(rotatedCenter),
        center.z - XMVectorGetZ(rotatedCenter)
    };
    return transform;
}

Camera ModelPreviewRenderer::MakeOrbitCamera(const SceneView& view, float aspect)
{
    const float pitch = (std::max)(-1.5f, (std::min)(1.5f, view.pitch));
    const float distance = (std::max)(0.5f, view.distance);

    Camera camera;
    camera.SetPosition(
        view.target.x + distance * std::sin(view.yaw) * std::cos(pitch),
        view.target.y + distance * std::sin(pitch),
        view.target.z + distance * std::cos(view.yaw) * std::cos(pitch));
    camera.SetTarget(view.target.x, view.target.y, view.target.z);
    camera.SetProjection(XMConvertToRadians(40.0f), aspect, 0.05f, 1000.0f);
    return camera;
}

void ModelPreviewRenderer::RenderScene(const std::vector<SceneModel>& models, OffscreenTarget& target, const SceneView& view,
    const LightingState& lighting, float backgroundBrightness)
{
    if (!m_renderer || !target.IsValid())
    {
        return;
    }

    const Camera camera = MakeOrbitCamera(view, static_cast<float>(target.GetWidth()) / static_cast<float>(target.GetHeight()));

    const float* base = BackgroundColor();
    const float brightness = (std::max)(0.0f, (std::min)(1.0f, backgroundBrightness));
    const float background[4] = { base[0] * brightness, base[1] * brightness, base[2] * brightness, 1.0f };

    ID3D11DeviceContext* context = m_renderer->GetContext();
    m_renderer->ApplyLighting(lighting);
    target.Begin(context, background);
    for (const SceneModel& item : models)
    {
        if (item.model && item.model->GetVertexCount() > 0)
        {
            m_renderer->DrawModel(*item.model, item.transform, camera);
        }
    }
    target.End(context);
    m_renderer->ApplyLighting(m_renderer->GetLighting());
}

void ModelPreviewRenderer::Render(const Model& model, OffscreenTarget& target, const PreviewView& view, const LightingState& lighting)
{
    if (!m_renderer || !target.IsValid() || model.GetVertexCount() == 0)
    {
        return;
    }

    // モデルを原点に置き、最大の辺がkFittedSizeになるよう拡縮する(拡縮してから平行移動するので、位置もその倍率ぶん)。
    const XMFLOAT3 boundsMin = model.GetBoundsMin();
    const XMFLOAT3 boundsMax = model.GetBoundsMax();
    const float scale = ComputeFitScale(model);

    Transform transform;
    transform.scale = { scale, scale, scale };
    transform.position =
    {
        -(boundsMin.x + boundsMax.x) * 0.5f * scale,
        -(boundsMin.y + boundsMax.y) * 0.5f * scale,
        -(boundsMin.z + boundsMax.z) * 0.5f * scale
    };

    // カメラはモデル(原点)の周りの球面上に置く。ピッチは真上・真下に行き過ぎない範囲に収める。
    const float pitch = (std::max)(-1.5f, (std::min)(1.5f, view.pitch));
    const float zoom = (std::max)(0.25f, (std::min)(8.0f, view.zoom));
    const float distance = kBaseDistance / zoom;

    Camera camera;
    camera.SetPosition(
        distance * std::sin(view.yaw) * std::cos(pitch),
        distance * std::sin(pitch),
        distance * std::cos(view.yaw) * std::cos(pitch));
    camera.SetTarget(0.0f, 0.0f, 0.0f);
    camera.SetProjection(XMConvertToRadians(40.0f), static_cast<float>(target.GetWidth()) / static_cast<float>(target.GetHeight()), 0.05f, 100.0f);

    ID3D11DeviceContext* context = m_renderer->GetContext();

    // 固定の光で描き、終わったら今の光に戻す(ゲーム内の画面の光に影響を残さない)。
    m_renderer->ApplyLighting(lighting);
    target.Begin(context, BackgroundColor());
    m_renderer->DrawModel(model, transform, camera);
    target.End(context);
    m_renderer->ApplyLighting(m_renderer->GetLighting());
}
