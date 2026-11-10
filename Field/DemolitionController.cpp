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

    // 2つのスライスの四角が、面積を持って重なるか(辺が接するだけなら、重ならない)。分離軸判定。
    bool WindowsOverlap(const RoadCutWindow& a, const RoadCutWindow& b)
    {
        const float eps = 0.01f;
        const float cx = b.center.x - a.center.x;
        const float cz = b.center.z - a.center.z;
        const float axes[4][2] = { { a.dirX, a.dirZ }, { a.dirZ, -a.dirX }, { b.dirX, b.dirZ }, { b.dirZ, -b.dirX } };
        for (const auto& n : axes)
        {
            const float d = std::fabs(cx * n[0] + cz * n[1]);
            const float ra = std::fabs(a.dirX * n[0] + a.dirZ * n[1]) * a.halfLength + std::fabs(a.dirZ * n[0] - a.dirX * n[1]) * a.halfWidth;
            const float rb = std::fabs(b.dirX * n[0] + b.dirZ * n[1]) * b.halfLength + std::fabs(b.dirZ * n[0] - b.dirX * n[1]) * b.halfWidth;
            if (d >= ra + rb - eps) { return false; }
        }
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

    // 解体で無くなる、道路の継ぎ目の埋め草(道幅の正方形。RoadMeshBuilder の AppendNodeCap)。
    // 消える部分を、正方形を4つに分けた区画(半分×半分)ごとに持つ。
    struct VanishingJointCap
    {
        XMFLOAT3 center{};          // 継ぎ目の位置(正方形の中心)
        float half = 0.0f;          // 正方形の半分の幅(集まる道路の、いちばん広い道幅の半分)
        bool vanish[2][2] = {};     // 区画が消えるか。[x の側(0=-,1=+)][z の側(0=-,1=+)]。残る道路の路面になる区画は false
    };

    // 2つの点が、XZ平面で同じ位置か。
    bool IsSamePointXZ(const XMFLOAT3& a, const XMFLOAT3& b)
    {
        return std::fabs(a.x - b.x) < 1e-3f && std::fabs(a.z - b.z) < 1e-3f;
    }

    // 選んだスライスを切り取ると、埋め草が無くなる継ぎ目(切った後、そこに集まる道路が1本以下になる所)を探す。
    std::vector<VanishingJointCap> FindVanishingJointCaps(const RoadSystem& roads, const std::vector<RoadCutWindow>& windows)
    {
        std::vector<VanishingJointCap> caps;
        if (windows.empty())
        {
            return caps;
        }
        const RoadCutResult preview = roads.PreviewCut(windows);
        if (!preview.changed)
        {
            return caps;
        }
        const std::vector<RoadSegment>& segments = roads.GetSegments();
        for (const RoadNode& node : roads.GetNodes())
        {
            if (node.connectedSegmentIndices.size() < 2)
            {
                continue;
            }
            float halfWidth = 0.0f;
            bool allCurve = true;
            for (size_t index : node.connectedSegmentIndices)
            {
                if (index >= segments.size()) { continue; }
                halfWidth = (std::max)(halfWidth, segments[index].width * 0.5f);
                if (segments[index].drawMode != RoadDrawMode::Curve) { allCurve = false; }
            }
            // 滑らかな曲線どうしの継ぎ目(2本)は、埋め草を敷かず、継ぎ目の縁を共有する(メッシュ作りと同じ規則)。
            if (halfWidth < 0.0001f || (node.connectedSegmentIndices.size() == 2 && allCurve))
            {
                continue;
            }
            int remaining = 0;
            for (const RoadSegment& segment : preview.segments)
            {
                if (IsSamePointXZ(segment.start, node.position) || IsSamePointXZ(segment.end, node.position)) { ++remaining; }
            }
            if (remaining >= 2)
            {
                continue;   // 埋め草は、残る
            }
            VanishingJointCap cap;
            cap.center = node.position;
            cap.half = halfWidth;
            bool any = false;
            for (int ix = 0; ix < 2; ++ix)
            {
                for (int iz = 0; iz < 2; ++iz)
                {
                    // 区画の中心が、残る道路(この継ぎ目で終わる1本)の路面の上なら、その区画は消えない。
                    const float qx = node.position.x + (ix == 0 ? -0.5f : 0.5f) * halfWidth;
                    const float qz = node.position.z + (iz == 0 ? -0.5f : 0.5f) * halfWidth;
                    bool onRemainingRoad = false;
                    for (const RoadSegment& segment : preview.segments)
                    {
                        const bool atStart = IsSamePointXZ(segment.start, node.position);
                        if (!atStart && !IsSamePointXZ(segment.end, node.position)) { continue; }
                        const XMFLOAT3& farPoint = atStart ? segment.end : segment.start;
                        const float dx = farPoint.x - node.position.x;
                        const float dz = farPoint.z - node.position.z;
                        const float length = std::sqrt(dx * dx + dz * dz);
                        if (length < 1e-4f) { continue; }
                        const float ux = dx / length;
                        const float uz = dz / length;
                        const float along = (qx - node.position.x) * ux + (qz - node.position.z) * uz;
                        const float across = (qx - node.position.x) * uz - (qz - node.position.z) * ux;
                        if (along >= -1e-3f && along <= length && std::fabs(across) <= segment.width * 0.5f) { onRemainingRoad = true; break; }
                    }
                    cap.vanish[ix][iz] = !onRemainingRoad;
                    any = any || cap.vanish[ix][iz];
                }
            }
            if (any)
            {
                caps.push_back(cap);
            }
        }
        return caps;
    }

    // スライスの四角が、埋め草の正方形の中に、すっぽり入っているか(その印は、埋め草の印にまとめる)。
    bool WindowInsideCap(const RoadCutWindow& window, const VanishingJointCap& cap)
    {
        const float perpX = window.dirZ;
        const float perpZ = -window.dirX;
        const float eps = 0.01f;
        for (int su = -1; su <= 1; su += 2)
        {
            for (int sv = -1; sv <= 1; sv += 2)
            {
                const float x = window.center.x + window.dirX * su * window.halfLength + perpX * sv * window.halfWidth;
                const float z = window.center.z + window.dirZ * su * window.halfLength + perpZ * sv * window.halfWidth;
                if (std::fabs(x - cap.center.x) > cap.half + eps || std::fabs(z - cap.center.z) > cap.half + eps) { return false; }
            }
        }
        return true;
    }

    // 消える埋め草を、ひとまとまりの印(枠と×)で描く。
    // 全部消える = 正方形1つ、半分消える(片方の道路が残る) = 長方形1つ、それ以外(斜めの道路など) = 区画ごと。
    void DrawVanishingJointCap(DebugRenderer& renderer, const VanishingJointCap& cap, const XMFLOAT4& color)
    {
        const float xs[3] = { cap.center.x - cap.half, cap.center.x, cap.center.x + cap.half };
        const float zs[3] = { cap.center.z - cap.half, cap.center.z, cap.center.z + cap.half };
        auto box = [&](float x0, float x1, float z0, float z1)
        {
            // 向きはワールドのX軸(dirX=1)。長さ方向=X、幅方向=Z。
            RoadCutWindow w;
            w.center = XMFLOAT3((x0 + x1) * 0.5f, 0.0f, (z0 + z1) * 0.5f);
            w.dirX = 1.0f;
            w.dirZ = 0.0f;
            w.halfLength = (x1 - x0) * 0.5f;
            w.halfWidth = (z1 - z0) * 0.5f;
            DrawWindowMarker(renderer, w, color, true);
        };
        int count = 0;
        for (int ix = 0; ix < 2; ++ix) { for (int iz = 0; iz < 2; ++iz) { if (cap.vanish[ix][iz]) { ++count; } } }
        if (count == 4)
        {
            box(xs[0], xs[2], zs[0], zs[2]);
            return;
        }
        if (count == 2)
        {
            for (int ix = 0; ix < 2; ++ix)
            {
                if (cap.vanish[ix][0] && cap.vanish[ix][1]) { box(xs[ix], xs[ix + 1], zs[0], zs[2]); return; }
            }
            for (int iz = 0; iz < 2; ++iz)
            {
                if (cap.vanish[0][iz] && cap.vanish[1][iz]) { box(xs[0], xs[2], zs[iz], zs[iz + 1]); return; }
            }
        }
        for (int ix = 0; ix < 2; ++ix)
        {
            for (int iz = 0; iz < 2; ++iz)
            {
                if (cap.vanish[ix][iz]) { box(xs[ix], xs[ix + 1], zs[iz], zs[iz + 1]); }
            }
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

    const POINT cursor = m_context->input->GetMousePosition();
    const bool down = m_context->input->IsKeyDown(VK_LBUTTON);

    // カーソルが、ゲームの画面の外(ウィンドウの外・エディタのパネルより外)にある時は、UIの上と、同じ扱いにする
    // (選びかけは、そのまま。新しくは選ばない。そこで離したら、取り消す)。
    const bool outsideView = m_context->renderer
        && (cursor.x < 0 || cursor.y < 0
            || cursor.x >= static_cast<LONG>(m_context->renderer->GetViewWidth())
            || cursor.y >= static_cast<LONG>(m_context->renderer->GetViewHeight()));
    const bool overUi = IsMouseOverEditorUi() || m_context->mouseOverUI || outsideView;

    // ドラッグ中に、ウィンドウが前面でなくなった(Alt+Tabなど): ボタンが「離された」扱いになるが、確定はしない(取り消す)。
    if (m_dragging && !m_context->input->HasFocus())
    {
        Cancel();
        return;
    }

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
    // 道路だけを選んでいる時は、切り取る長さを、マスの数で、一緒に出す(建物が混ざる時は、金額だけ)。
    int cost = 0;
    int roadCells = 0;
    if (m_dragging)
    {
        cost = m_selection.BuildingCost();
        if (!m_roadSlices.empty())
        {
            const RoadCutResult preview = m_roadSystem->PreviewCut(SelectedWindows());
            cost += CalculateRoadDemolitionCost(preview.removed);
            if (m_selection.Empty() && m_field)
            {
                roadCells = static_cast<int>(std::lround(preview.removedLength / m_field->GetCellSize()));
            }
        }
    }
    else if (m_hoverId != 0)
    {
        cost = kBuildingDemolitionCost;
    }
    else if (m_hasHoverRoad)
    {
        const RoadCutResult preview = m_roadSystem->PreviewCut({ m_hoverRoad.window });
        cost = CalculateRoadDemolitionCost(preview.removed);
        if (m_field) { roadCells = static_cast<int>(std::lround(preview.removedLength / m_field->GetCellSize())); }
    }
    if (cost > 0)
    {
        m_context->costPreview.visible = true;
        m_context->costPreview.cost = cost;
        m_context->costPreview.cells = roadCells;
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
    std::vector<OrientedRect> footprints;
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
        footprints.push_back(obj->placement.footprintRect); // 消した後は分からなくなるので、先に控える(青マスを消すのに使う)
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

    // 建物が登録していた青マスのうち、道路の脇でも、他の建物の敷地でもない物は、消す(跡が青いまま残らないように)。
    if (m_buildController && m_roadSystem)
    {
        m_buildController->ReleaseOrientationCells(footprints, m_roadSystem->BuildFrontageZones());
    }

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

    // 折れ曲がりの角(2本が集まる継ぎ目)では、2本の道路の最後のスライスの四角が、重なる。重なった×が密になって乱れるので、
    // 角の埋め草の正方形の中にあって、先に選んだ別の向きのスライスと重なるスライスは、枠だけにする(×は、先の方に残る)。
    // 直線の道路(向きが同じ)・T字路・十字路(3本以上)は、今までと同じ。
    std::vector<std::pair<XMFLOAT3, float>> corners;   // 角の継ぎ目の位置と、埋め草の半分の幅
    if (m_roadSystem && m_roadSlices.size() >= 2)
    {
        const std::vector<RoadSegment>& segments = m_roadSystem->GetSegments();
        for (const RoadNode& node : m_roadSystem->GetNodes())
        {
            if (node.connectedSegmentIndices.size() != 2) { continue; }
            float halfWidth = 0.0f;
            for (size_t index : node.connectedSegmentIndices)
            {
                if (index < segments.size()) { halfWidth = (std::max)(halfWidth, segments[index].width * 0.5f); }
            }
            if (halfWidth > 0.0001f) { corners.emplace_back(node.position, halfWidth); }
        }
    }
    // 解体で無くなる継ぎ目の埋め草(角の正方形)。中にすっぽり入るスライスの印は描かず、埋め草の印(枠と×1つ)にまとめる。
    // (スライスごとに描くと、角の正方形の一部にしか×が付かず、角の全体が選ばれているように見えなかった。)
    std::vector<VanishingJointCap> caps;
    if (m_roadSystem && !m_roadSlices.empty())
    {
        caps = FindVanishingJointCaps(*m_roadSystem, SelectedWindows());
    }
    for (size_t i = 0; i < m_roadSlices.size(); ++i)
    {
        const RoadCutWindow& window = m_roadSlices[i].window;
        bool insideCap = false;
        for (const VanishingJointCap& cap : caps)
        {
            if (WindowInsideCap(window, cap)) { insideCap = true; break; }
        }
        if (insideCap)
        {
            continue;
        }
        bool cross = true;
        for (const auto& corner : corners)
        {
            if (std::fabs(window.center.x - corner.first.x) > corner.second || std::fabs(window.center.z - corner.first.z) > corner.second) { continue; }
            for (size_t j = 0; j < i && cross; ++j)
            {
                const RoadCutWindow& other = m_roadSlices[j].window;
                const bool parallel = std::fabs(window.dirX * other.dirZ - window.dirZ * other.dirX) < 0.1f;
                if (!parallel && WindowsOverlap(window, other)) { cross = false; }
            }
            if (!cross) { break; }
        }
        DrawWindowMarker(renderer, window, kSelectedColor, cross);
    }
    for (const VanishingJointCap& cap : caps)
    {
        DrawVanishingJointCap(renderer, cap, kSelectedColor);
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
