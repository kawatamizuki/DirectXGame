#include "DemolitionController.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include "GameContext.h"
#include "Field.h"
#include "IOccupancyGrid.h"
#include "RoadSystem.h"
#include "BuildController.h"
#include "DemandSystem.h"
#include "GameEconomy.h"
#include "GameObjectFactory.h"
#include "Model.h"
#include "Camera.h"
#include "Renderer.h"
#include "DebugRenderer.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Collision.h"
#include "EditorGuard.h"
#include "Debug.h"

using namespace DirectX;

namespace
{
    constexpr float kPickStepPixels = 4.0f;   // ドラッグ中の補間の刻み(画面のピクセル)。建物より十分小さい

    // 赤いしるし(選んだ建物)と、オレンジのしるし(カーソルの下の建物。まだ選んでいない)。
    const XMFLOAT4 kSelectedColor(1.0f, 0.15f, 0.15f, 1.0f);
    const XMFLOAT4 kHoverColor(1.0f, 0.65f, 0.1f, 1.0f);

    // 建物の足元の四角(ワールド座標の4隅。反時計回り)。配置の時に確定した形(footprintRect)があればそれ、無ければ、モデルの箱を地面に落とした外側の四角。
    bool FootprintCorners(const GameObject& obj, XMFLOAT3 out[4])
    {
        const OrientedRect& rect = obj.placement.footprintRect;
        if (rect.width > 0.0001f && rect.depth > 0.0001f)
        {
            const float c = std::cos(rect.yaw);
            const float s = std::sin(rect.yaw);
            const XMFLOAT3 axisX(c, 0.0f, -s);
            const XMFLOAT3 axisZ(s, 0.0f, c);
            out[0] = rect.origin;
            out[1] = XMFLOAT3(rect.origin.x + axisX.x * rect.width, 0.0f, rect.origin.z + axisX.z * rect.width);
            out[2] = XMFLOAT3(out[1].x + axisZ.x * rect.depth, 0.0f, out[1].z + axisZ.z * rect.depth);
            out[3] = XMFLOAT3(rect.origin.x + axisZ.x * rect.depth, 0.0f, rect.origin.z + axisZ.z * rect.depth);
            return true;
        }
        if (!obj.model)
        {
            return false;
        }
        const XMFLOAT3 mn = obj.model->GetBoundsMin();
        const XMFLOAT3 mx = obj.model->GetBoundsMax();
        const XMMATRIX world = obj.transform.GetWorldMatrix();
        float minX = FLT_MAX, maxX = -FLT_MAX, minZ = FLT_MAX, maxZ = -FLT_MAX;
        for (int i = 0; i < 8; ++i)
        {
            const XMFLOAT3 corner{ (i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z };
            XMFLOAT3 p;
            XMStoreFloat3(&p, XMVector3Transform(XMLoadFloat3(&corner), world));
            minX = (std::min)(minX, p.x); maxX = (std::max)(maxX, p.x);
            minZ = (std::min)(minZ, p.z); maxZ = (std::max)(maxZ, p.z);
        }
        out[0] = XMFLOAT3(minX, 0.0f, minZ);
        out[1] = XMFLOAT3(maxX, 0.0f, minZ);
        out[2] = XMFLOAT3(maxX, 0.0f, maxZ);
        out[3] = XMFLOAT3(minX, 0.0f, maxZ);
        return true;
    }

    // 建物の足元の四角を、赤(オレンジ)の枠と、対角線(×)で描く。×は、「消える」ことを示す。
    void DrawMarker(DebugRenderer& renderer, const GameObject& obj, const XMFLOAT4& color, bool cross)
    {
        XMFLOAT3 corners[4];
        if (!FootprintCorners(obj, corners))
        {
            return;
        }
        const float y = 0.05f; // 地面・道路(0.02)より少し上に浮かせる
        for (int i = 0; i < 4; ++i) { corners[i].y = y; }
        for (int i = 0; i < 4; ++i)
        {
            renderer.AddLine(corners[i], corners[(i + 1) % 4], color);
        }
        // 中心に向けて少し縮めた2本目の枠(線が細くても、目立つように)。
        const XMFLOAT3 center((corners[0].x + corners[2].x) * 0.5f, y, (corners[0].z + corners[2].z) * 0.5f);
        XMFLOAT3 inner[4];
        for (int i = 0; i < 4; ++i)
        {
            inner[i] = XMFLOAT3(center.x + (corners[i].x - center.x) * 0.92f, y, center.z + (corners[i].z - center.z) * 0.92f);
        }
        for (int i = 0; i < 4; ++i)
        {
            renderer.AddLine(inner[i], inner[(i + 1) % 4], color);
        }
        if (cross)
        {
            renderer.AddLine(corners[0], corners[2], color);
            renderer.AddLine(corners[1], corners[3], color);
        }
    }
}

void DemolitionController::Initialize(GameContext* context, Field* field, IOccupancyGrid* occupancy,
                                      RoadSystem* roadSystem, BuildController* buildController, DemandSystem* demandSystem)
{
    m_context = context;
    m_field = field;
    m_occupancy = occupancy;
    m_roadSystem = roadSystem;
    m_buildController = buildController;
    m_demandSystem = demandSystem;
}

void DemolitionController::Cancel()
{
    m_dragging = false;
    m_selection.Clear();
}

void DemolitionController::Notify(const std::string& text) const
{
    if (m_onMessage)
    {
        m_onMessage(text);
    }
}

uint32_t DemolitionController::PickBuildingAt(const POINT& cursor) const
{
    if (!m_context || !m_context->objects || !m_context->camera || !m_context->renderer)
    {
        return 0;
    }
    const Ray ray = m_context->camera->ScreenPointToRay(cursor, m_context->renderer->GetViewWidth(), m_context->renderer->GetViewHeight());

    uint32_t hitId = 0;
    float nearest = FLT_MAX;
    for (const GameObject& obj : *m_context->objects)
    {
        if (obj.kind != ObjectKind::Building || !obj.model)
        {
            continue;
        }
        float distance = 0.0f;
        if (IntersectRayOBB(ray, obj.transform, obj.model->GetBoundsMin(), obj.model->GetBoundsMax(), distance) && distance < nearest)
        {
            nearest = distance;
            hitId = obj.id;
        }
    }
    return hitId;
}

void DemolitionController::AddPicksAlong(const POINT& from, const POINT& to)
{
    const float dx = static_cast<float>(to.x - from.x);
    const float dy = static_cast<float>(to.y - from.y);
    const float length = std::sqrt(dx * dx + dy * dy);
    const int steps = (std::max)(1, static_cast<int>(std::ceil(length / kPickStepPixels)));
    for (int i = 1; i <= steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const POINT p{ from.x + static_cast<LONG>(std::lround(dx * t)), from.y + static_cast<LONG>(std::lround(dy * t)) };
        m_selection.Add(PickBuildingAt(p));
    }
}

void DemolitionController::Update()
{
    m_hoverId = 0;
    if (!m_context || !m_context->input || m_context->placementTool != PlacementTool::Demolish)
    {
        // 解体ツールをやめた(メニューで別の物を選んだなど): 選びかけを残さない。
        if (m_dragging || !m_selection.Empty()) { Cancel(); }
        return;
    }

    const bool overUi = IsMouseOverEditorUi() || m_context->mouseOverUI;
    const POINT cursor = m_context->input->GetMousePosition();
    const bool down = m_context->input->IsKeyDown(VK_LBUTTON);

    if (m_dragging)
    {
        m_context->inputConsumed = true; // 押している間は、他の操作へ抜けない
        if (!down)
        {
            FinishDrag(overUi);
            return;
        }
        if (!overUi)
        {
            AddPicksAlong(m_lastCursor, cursor);
        }
        m_lastCursor = cursor;
    }
    else if (!overUi && !m_context->inputConsumed)
    {
        m_hoverId = PickBuildingAt(cursor);
        if (m_context->input->IsActionPressed(InputAction::Decide))
        {
            m_dragging = true;
            m_lastCursor = cursor;
            m_selection.Clear();
            m_selection.Add(m_hoverId);
            m_context->inputConsumed = true;
        }
    }

    // カーソルの横の費用タグ: 選んでいる棟数ぶん(選んでいなければ、カーソルの下の1棟ぶん)。
    const size_t count = m_dragging ? m_selection.Count() : (m_hoverId != 0 ? 1 : 0);
    if (count > 0)
    {
        const int cost = static_cast<int>(count) * kBuildingDemolitionCost;
        m_context->costPreview.visible = true;
        m_context->costPreview.cost = cost;
        m_context->costPreview.cells = 0;
        m_context->costPreview.affordable = !m_context->economy || CanPayDemolition(m_context->economy->GetCash(), cost);
    }
}

void DemolitionController::FinishDrag(bool overUi)
{
    m_dragging = false;
    if (overUi || m_selection.Empty())
    {
        m_selection.Clear(); // UIの上で離した: 取り消す(何も払わない・消さない)
        return;
    }

    // まだ存在する建物だけを数える(選んでから離すまでの間に、ロードなどで消えていないか)。
    std::vector<uint32_t> ids;
    for (uint32_t id : m_selection.Ids())
    {
        const GameObject* obj = GameObjectFactory::FindById(*m_context->objects, id);
        if (obj && obj->kind == ObjectKind::Building)
        {
            ids.push_back(id);
        }
    }
    m_selection.Clear();
    if (ids.empty())
    {
        return;
    }

    const int cost = static_cast<int>(ids.size()) * kBuildingDemolitionCost;
    if (m_context->economy && !CanPayDemolition(m_context->economy->GetCash(), cost))
    {
        // 払えない時は、全体を取り消す(一部だけ消さない)。
        Notify("お金が足りません: " + std::to_string(ids.size()) + "棟の解体に " + std::to_string(cost) +
               "円かかります(所持金 " + std::to_string(m_context->economy->GetCash()) + "円)");
        return;
    }
    if (m_context->economy && !m_context->economy->Spend(cost))
    {
        return; // CanPayDemolitionで確認済みなので、通常は起きない
    }

    const int demolished = DemolishBuildings(ids);
    Notify(std::to_string(demolished) + "棟を解体しました(-" + std::to_string(cost) + "円)");
}

int DemolitionController::DemolishBuildings(const std::vector<uint32_t>& ids)
{
    if (!m_context || !m_context->objects)
    {
        return 0;
    }
    std::vector<GameObject>& objects = *m_context->objects;

    // 消す前に、住民・集計の処理に渡す情報(種類・位置)を集める。重複したidは、1回だけ。
    std::vector<DemolishedBuilding> infos;
    for (uint32_t id : ids)
    {
        const GameObject* obj = GameObjectFactory::FindById(objects, id);
        if (!obj || obj->kind != ObjectKind::Building)
        {
            continue;
        }
        const bool already = std::any_of(infos.begin(), infos.end(), [id](const DemolishedBuilding& b) { return b.id == id; });
        if (already)
        {
            continue;
        }
        DemolishedBuilding info;
        info.id = id;
        info.type = obj->buildingType;
        info.position = obj->transform.position;
        infos.push_back(info);
    }
    if (infos.empty())
    {
        return 0;
    }

    // オブジェクトを消す。Despawnは、末尾と入れ替えて消すので、添字は毎回、idから探し直す(添字を持ち回らない)。
    for (const DemolishedBuilding& info : infos)
    {
        for (size_t i = 0; i < objects.size(); ++i)
        {
            if (objects[i].id == info.id)
            {
                GameObjectFactory::Despawn(objects, i);
                break;
            }
        }
    }

    // 住民・施設の集計を整える(オブジェクトを消した後。住民のマーカーを探す時に、消えた建物が混ざらないように)。
    if (m_demandSystem)
    {
        for (const DemolishedBuilding& info : infos)
        {
            m_demandSystem->OnBuildingDemolished(info, *m_context);
        }
    }

    RebuildOccupancy();

    if (m_onWorldChanged)
    {
        m_onWorldChanged();
    }
    Debug::Info("DemolitionController: 建物を " + std::to_string(infos.size()) + " 棟、解体しました");
    return static_cast<int>(infos.size());
}

void DemolitionController::RebuildOccupancy()
{
    if (!m_occupancy)
    {
        return;
    }
    // 占有は、マスの集合(参照回数なし)と、矩形の一覧でできていて、1つだけを解除できない。
    // 全部消して、材料(道路の区間・建物の敷地)から作り直す。
    m_occupancy->Clear();
    if (m_roadSystem) { m_roadSystem->ReregisterOccupancy(); }
    if (m_buildController) { m_buildController->ReregisterOccupancy(); }
}

void DemolitionController::Draw()
{
    if (!m_context || !m_context->debugRenderer || !m_context->objects || m_context->placementTool != PlacementTool::Demolish)
    {
        return;
    }
    DebugRenderer& renderer = *m_context->debugRenderer;

    for (uint32_t id : m_selection.Ids())
    {
        if (const GameObject* obj = GameObjectFactory::FindById(*m_context->objects, id))
        {
            DrawMarker(renderer, *obj, kSelectedColor, true);
        }
    }
    if (m_hoverId != 0 && !m_selection.Contains(m_hoverId))
    {
        if (const GameObject* obj = GameObjectFactory::FindById(*m_context->objects, m_hoverId))
        {
            DrawMarker(renderer, *obj, kHoverColor, false);
        }
    }
}
