#include "PropPlacer.h"
#include "PropPlacement.h"
#include <algorithm>
#include <cmath>
#include "GameContext.h"
#include "Field.h"
#include "WorldScale.h"
#include "Model.h"
#include "ModelCache.h"
#include "GameObjectFactory.h"
#include "Renderer.h"
#include "Camera.h"
#include "DebugRenderer.h"
#include "InputManager.h"
#include "Debug.h"

using namespace DirectX;

void PropPlacer::Initialize(GameContext* context, Field* field)
{
    m_context = context;
    m_field = field;
}

Transform PropPlacer::ComputePlacement(const Model& model, const XMFLOAT3& groundPoint, float groundHeight, float cellSize)
{
    const PropPlacementResult placement = ComputePropPlacement(model.GetBoundsMin(), model.GetBoundsMax(), groundPoint, groundHeight, cellSize);

    Transform transform;
    transform.scale = { placement.scale, placement.scale, placement.scale };
    transform.position = placement.position;
    return transform;
}

bool PropPlacer::RaycastScreenPoint(float screenX, float screenY, XMFLOAT3& outPoint) const
{
    if (!m_context || !m_field || !m_context->camera || !m_context->renderer)
    {
        return false;
    }

    POINT point{ static_cast<LONG>(screenX), static_cast<LONG>(screenY) };
    const Ray ray = m_context->camera->ScreenPointToRay(point, m_context->renderer->GetViewWidth(), m_context->renderer->GetViewHeight());

    XMFLOAT3 hit;
    if (!m_field->RaycastGround(ray, hit))
    {
        return false;
    }

    // Fieldの範囲の中だけ(Fieldは原点が中心)。遠くの地面や、カメラの後ろへ誤って置かないため。
    if (std::fabs(hit.x) > m_field->GetWorldWidth() * 0.5f || std::fabs(hit.z) > m_field->GetWorldDepth() * 0.5f)
    {
        return false;
    }

    outPoint = hit;
    return true;
}

bool PropPlacer::PlaceAt(const std::string& modelPath, const XMFLOAT3& groundPoint)
{
    if (!m_context || !m_field || !m_context->objects || !m_context->modelCache || modelPath.empty())
    {
        return false;
    }

    Model* model = m_context->modelCache->Get(modelPath);
    if (!model)
    {
        return false;
    }

    const float groundHeight = m_field->GetHeightAt(groundPoint.x, groundPoint.z);
    const Transform transform = ComputePlacement(*model, groundPoint, groundHeight, WorldScale::FromLegacy(1.0f));

    GameObject& prop = GameObjectFactory::Spawn(*m_context->objects, model, transform, ObjectKind::Prop);
    prop.modelPath = modelPath;
    Debug::Info("PropPlacer: 飾りを置きました : " + modelPath);
    return true;
}

bool PropPlacer::PlaceAtScreenPoint(const std::string& modelPath, float screenX, float screenY)
{
    XMFLOAT3 point;
    return RaycastScreenPoint(screenX, screenY, point) && PlaceAt(modelPath, point);
}

void PropPlacer::Update()
{
    if (!m_context)
    {
        return;
    }

    const AssetDragState& drag = m_context->assetDrag;
    if (!drag.dropped || drag.path.empty())
    {
        return;
    }

    POINT mouse = m_context->input->GetMousePosition();
    PlaceAtScreenPoint(drag.path, static_cast<float>(mouse.x), static_cast<float>(mouse.y));
}

void PropPlacer::Draw()
{
    if (!m_context || !m_field || !m_context->modelCache || !m_context->debugRenderer)
    {
        return;
    }

    const AssetDragState& drag = m_context->assetDrag;
    if (!drag.active || !drag.overGameView || drag.path.empty())
    {
        return;
    }

    POINT mouse = m_context->input->GetMousePosition();
    XMFLOAT3 point;
    if (!RaycastScreenPoint(static_cast<float>(mouse.x), static_cast<float>(mouse.y), point))
    {
        return;
    }

    Model* model = m_context->modelCache->Get(drag.path);
    if (!model)
    {
        return;
    }

    const float groundHeight = m_field->GetHeightAt(point.x, point.z);
    const Transform transform = ComputePlacement(*model, point, groundHeight, WorldScale::FromLegacy(1.0f));

    // 置いた時の見た目を半透明で。
    m_context->renderer->DrawModel(*model, transform, *m_context->camera, 0.5f);

    // 1マスの枠(置く大きさの目安)。
    const float half = WorldScale::FromLegacy(1.0f) * 0.5f;
    const float y = groundHeight + 0.01f;
    const XMFLOAT4 color(0.2f, 0.9f, 1.0f, 1.0f);
    const XMFLOAT3 p0(point.x - half, y, point.z - half);
    const XMFLOAT3 p1(point.x + half, y, point.z - half);
    const XMFLOAT3 p2(point.x + half, y, point.z + half);
    const XMFLOAT3 p3(point.x - half, y, point.z + half);
    m_context->debugRenderer->AddLine(p0, p1, color);
    m_context->debugRenderer->AddLine(p1, p2, color);
    m_context->debugRenderer->AddLine(p2, p3, color);
    m_context->debugRenderer->AddLine(p3, p0, color);
}
