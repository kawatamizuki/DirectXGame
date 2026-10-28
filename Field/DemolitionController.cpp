#include "DemolitionController.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <optional>
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
#include "RoadProximity.h"
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

    // 道路のスライス(切り取る範囲の四角)を、枠と対角線(×)で描く。
    void DrawWindowMarker(DebugRenderer& renderer, const RoadCutWindow& window, const XMFLOAT4& color, bool cross)
    {
        const float perpX = window.dirZ;
        const float perpZ = -window.dirX;
        const float y = 0.06f; // 道路(0.02)・建物の枠(0.05)より、少し上に浮かせる
        auto corner = [&](float u, float v)
        {
            return XMFLOAT3(window.center.x + window.dirX * u + perpX * v, y, window.center.z + window.dirZ * u + perpZ * v);
        };
        const XMFLOAT3 c[4] = { corner(-window.halfLength, -window.halfWidth), corner(window.halfLength, -window.halfWidth),
                                corner(window.halfLength, window.halfWidth), corner(-window.halfLength, window.halfWidth) };
        for (int i = 0; i < 4; ++i)
        {
            renderer.AddLine(c[i], c[(i + 1) % 4], color);
        }
        if (cross)
        {
            renderer.AddLine(c[0], c[2], color);
            renderer.AddLine(c[1], c[3], color);
        }
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
    m_roadSlices.clear();
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

bool DemolitionController::PickRoadSliceAt(const POINT& cursor, RoadSlice& out) const
{
    if (!m_context || !m_context->camera || !m_context->renderer || !m_field || !m_roadSystem)
    {
        return false;
    }
    const Ray ray = m_context->camera->ScreenPointToRay(cursor, m_context->renderer->GetViewWidth(), m_context->renderer->GetViewHeight());
    XMFLOAT3 hit;
    if (!m_field->RaycastGround(ray, hit))
    {
        return false;
    }

    // 道路面の上(中心線から、道幅の半分以内)にある時だけ。
    const std::vector<RoadSegment>& segments = m_roadSystem->GetSegments();
    const std::optional<NearestRoadPoint> nearest = FindNearestPointOnRoad(segments, hit, 0.0f, 0.5f);
    if (!nearest.has_value())
    {
        return false;
    }
    out.segmentIndex = nearest->segmentIndex;
    out.window = m_roadSystem->MakeSliceAt(nearest->segmentIndex, nearest->point, out.sliceIndex);
    return true;
}

void DemolitionController::AddPickAt(const POINT& cursor)
{
    const uint32_t buildingId = PickBuildingAt(cursor);
    if (buildingId != 0)
    {
        m_selection.Add(buildingId);
        return;
    }
    RoadSlice slice;
    if (PickRoadSliceAt(cursor, slice))
    {
        // 同じスライスは、何度なぞっても、1回だけ。
        for (const RoadSlice& existing : m_roadSlices)
        {
            if (existing.segmentIndex == slice.segmentIndex && existing.sliceIndex == slice.sliceIndex)
            {
                return;
            }
        }
        m_roadSlices.push_back(slice);
    }
}

std::vector<RoadCutWindow> DemolitionController::SelectedWindows() const
{
    std::vector<RoadCutWindow> windows;
    windows.reserve(m_roadSlices.size());
    for (const RoadSlice& slice : m_roadSlices)
    {
        windows.push_back(slice.window);
    }
    return windows;
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
        AddPickAt(p);
    }
}

void DemolitionController::Update()
{
    m_hoverId = 0;
    m_hasHoverRoad = false;
    if (!m_context || !m_context->input || m_context->placementTool != PlacementTool::Demolish)
    {
        // 解体ツールをやめた(メニューで別の物を選んだなど): 選びかけを残さない。
        if (m_dragging || !m_selection.Empty() || !m_roadSlices.empty()) { Cancel(); }
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
        if (m_hoverId == 0)
        {
            m_hasHoverRoad = PickRoadSliceAt(cursor, m_hoverRoad);
        }
        if (m_context->input->IsActionPressed(InputAction::Decide))
        {
            m_dragging = true;
            m_lastCursor = cursor;
            m_selection.Clear();
            m_roadSlices.clear();
            AddPickAt(cursor);
            m_context->inputConsumed = true;
        }
    }

    // カーソルの横の費用タグ: 選んでいるもの全部の合計(選んでいなければ、カーソルの下の1つぶん)。
    int cost = 0;
    if (m_dragging)
    {
        cost = m_selection.BuildingCost();
        if (!m_roadSlices.empty())
        {
            cost += CalculateRoadDemolitionCost(m_roadSystem->PreviewCut(SelectedWindows()).removed);
        }
    }
    else if (m_hoverId != 0)
    {
        cost = kBuildingDemolitionCost;
    }
    else if (m_hasHoverRoad)
    {
        cost = CalculateRoadDemolitionCost(m_roadSystem->PreviewCut({ m_hoverRoad.window }).removed);
    }
    if (cost > 0)
    {
        m_context->costPreview.visible = true;
        m_context->costPreview.cost = cost;
        m_context->costPreview.cells = 0;
        m_context->costPreview.affordable = !m_context->economy || CanPayDemolition(m_context->economy->GetCash(), cost);
    }
}

void DemolitionController::FinishDrag(bool overUi)
{
    m_dragging = false;
    if (overUi || (m_selection.Empty() && m_roadSlices.empty()))
    {
        Cancel(); // UIの上で離した: 取り消す(何も払わない・消さない)
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
    // 道路は、今の道路に対して、実際に切れるかを計算し直す(費用は、切り取る長さから決める)。
    const std::vector<RoadCutWindow> windows = SelectedWindows();
    const RoadCutResult preview = windows.empty() ? RoadCutResult() : m_roadSystem->PreviewCut(windows);
    const int roadCost = preview.changed ? CalculateRoadDemolitionCost(preview.removed) : 0;
    Cancel();
    if (ids.empty() && !preview.changed)
    {
        return;
    }

    const int buildingCost = static_cast<int>(ids.size()) * kBuildingDemolitionCost;
    const int cost = buildingCost + roadCost;
    if (m_context->economy && !CanPayDemolition(m_context->economy->GetCash(), cost))
    {
        // 払えない時は、全体を取り消す(一部だけ消さない)。
        Notify("お金が足りません: 解体に " + std::to_string(cost) + "円かかります(所持金 " + std::to_string(m_context->economy->GetCash()) + "円)");
        return;
    }
    if (m_context->economy && !m_context->economy->Spend(cost))
    {
        return; // CanPayDemolitionで確認済みなので、通常は起きない
    }

    const int demolished = DemolishBuildings(ids);
    const float removedLength = preview.changed ? DemolishRoads(windows) : 0.0f;

    std::string message;
    if (demolished > 0) { message += std::to_string(demolished) + "棟"; }
    if (removedLength > 0.0f)
    {
        // 取り除いた道路の長さを、マスの数で知らせる(マス1つぶん = スライス1つぶん)。
        const float cells = removedLength / (m_field ? m_field->GetCellSize() : 1.0f);
        char text[48];
        std::snprintf(text, sizeof(text), "道路 %.1fマス", cells);
        if (!message.empty()) { message += "・"; }
        message += text;
    }
    Notify(message + "を解体しました(-" + std::to_string(cost) + "円)");
}

float DemolitionController::DemolishRoads(const std::vector<RoadCutWindow>& windows)
{
    if (!m_context || !m_roadSystem || windows.empty())
    {
        return 0.0f;
    }
    const RoadCutResult cut = m_roadSystem->ApplyCut(windows);
    if (!cut.changed)
    {
        return 0.0f;
    }

    // 占有を、道路と建物から作り直す。住民の、無効になった経路を整える(帰宅・迂回・立ち往生)。
    RebuildOccupancy();
    if (m_demandSystem && m_field)
    {
        m_demandSystem->OnRoadsChanged(cut.removed, m_roadSystem->GetSegments(), *m_context);
    }
    if (m_onWorldChanged)
    {
        m_onWorldChanged();
    }
    return cut.removedLength;
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

    for (const RoadSlice& slice : m_roadSlices)
    {
        DrawWindowMarker(renderer, slice.window, kSelectedColor, true);
    }
    if (m_hasHoverRoad && !m_dragging)
    {
        DrawWindowMarker(renderer, m_hoverRoad.window, kHoverColor, false);
    }

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
