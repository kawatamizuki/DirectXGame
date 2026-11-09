#include "AreaToolController.h"
#include <cmath>
#include "CommercialAreaManager.h"
#include "DebugRenderer.h"
#include "EditorGuard.h"
#include "Field.h"
#include "GameClock.h"
#include "GameContext.h"
#include "GameObjectFactory.h"
#include "InputAction.h"
#include "InputManager.h"
#include "Camera.h"
#include "Renderer.h"

using namespace DirectX;

namespace
{
    const XMFLOAT4 kColorUnpublished = { 0.55f, 0.85f, 1.00f, 1.0f };   // 非公開の仮置き
    const XMFLOAT4 kColorPublished = { 1.00f, 0.78f, 0.15f, 1.0f };     // 周知済み
    const XMFLOAT4 kColorValid = { 0.40f, 1.00f, 0.40f, 1.0f };         // 置ける・動かせる
    const XMFLOAT4 kColorInvalid = { 1.00f, 0.30f, 0.30f, 1.0f };       // 置けない
    const XMFLOAT4 kColorMember = { 1.00f, 1.00f, 1.00f, 1.0f };        // 選んだ区画に所属する施設の枠

    constexpr float kLineLift = 0.06f;      // 地面から、枠を少し浮かせる(地面に埋もれないように)

    void AddRect(DebugRenderer& debug, const Field* field, const AreaRect& rect, const XMFLOAT4& color, float inset)
    {
        const float x0 = rect.minX + inset, x1 = rect.maxX - inset;
        const float z0 = rect.minZ + inset, z1 = rect.maxZ - inset;
        if (!(x1 > x0) || !(z1 > z0))
        {
            return;
        }
        auto point = [&](float x, float z) { return XMFLOAT3(x, (field ? field->GetHeightAt(x, z) : 0.0f) + kLineLift, z); };
        const XMFLOAT3 p00 = point(x0, z0), p10 = point(x1, z0), p11 = point(x1, z1), p01 = point(x0, z1);
        debug.AddLine(p00, p10, color);
        debug.AddLine(p10, p11, color);
        debug.AddLine(p11, p01, color);
        debug.AddLine(p01, p00, color);
    }

    void AddOrientedRect(DebugRenderer& debug, const Field* field, const OrientedRect& rect, const XMFLOAT4& color)
    {
        const float c = std::cos(rect.yaw), s = std::sin(rect.yaw);
        const float ax = c, az = -s, bx = s, bz = c;     // OrientedRectsOverlap と同じ規約
        auto point = [&](float x, float z) { return XMFLOAT3(x, (field ? field->GetHeightAt(x, z) : 0.0f) + kLineLift * 2.0f, z); };
        const XMFLOAT3 p0 = point(rect.origin.x, rect.origin.z);
        const XMFLOAT3 p1 = point(rect.origin.x + ax * rect.width, rect.origin.z + az * rect.width);
        const XMFLOAT3 p2 = point(rect.origin.x + ax * rect.width + bx * rect.depth, rect.origin.z + az * rect.width + bz * rect.depth);
        const XMFLOAT3 p3 = point(rect.origin.x + bx * rect.depth, rect.origin.z + bz * rect.depth);
        debug.AddLine(p0, p1, color);
        debug.AddLine(p1, p2, color);
        debug.AddLine(p2, p3, color);
        debug.AddLine(p3, p0, color);
    }
}

void AreaToolController::Initialize(GameContext* context, Field* field, CommercialAreaManager* areas)
{
    m_context = context;
    m_field = field;
    m_areas = areas;
}

void AreaToolController::Notify(const std::string& text) const
{
    if (m_onMessage)
    {
        m_onMessage(text);
    }
}

void AreaToolController::Cancel()
{
    m_dragging = false;
    m_dragValid = false;
}

void AreaToolController::Update()
{
    m_hasHover = false;
    m_hoverOverArea = false;
    m_hoverValid = false;
    if (!m_context || !m_context->input || !m_context->camera || !m_context->renderer || !m_field || !m_areas)
    {
        return;
    }
    // 選んでいた区画が、無くなっていたら(ロード・削除)、外す。
    if (m_selectedId != 0 && !m_areas->Find(m_selectedId))
    {
        m_selectedId = 0;
        Cancel();
    }
    if (m_context->placementTool != PlacementTool::Area)
    {
        // ツールをやめた(メニューで別の物を選んだなど): 動かしかけ・選びを残さない。
        if (m_dragging || m_selectedId != 0) { Cancel(); m_selectedId = 0; }
        return;
    }

    const POINT cursor = m_context->input->GetMousePosition();
    const bool down = m_context->input->IsKeyDown(VK_LBUTTON);
    const bool outsideView = cursor.x < 0 || cursor.y < 0
        || cursor.x >= static_cast<LONG>(m_context->renderer->GetViewWidth())
        || cursor.y >= static_cast<LONG>(m_context->renderer->GetViewHeight());
    const bool overUi = IsMouseOverEditorUi() || m_context->mouseOverUI || outsideView;

    // ウィンドウが前面でなくなった(Alt+Tabなど): 動かしかけは、確定せず、やめる。
    if (m_dragging && !m_context->input->HasFocus())
    {
        Cancel();
        return;
    }

    // カーソルの下の地面。
    const Ray ray = m_context->camera->ScreenPointToRay(cursor, m_context->renderer->GetViewWidth(), m_context->renderer->GetViewHeight());
    XMFLOAT3 hit;
    const bool hasHit = !outsideView && m_field->RaycastGround(ray, hit);

    const int week = m_context->clock ? m_context->clock->GetWeekCount() : 0;
    const float cell = m_field->GetCellSize();

    if (m_dragging)
    {
        m_context->inputConsumed = true;       // 押している間は、他の操作へ抜けない
        if (hasHit && !overUi)
        {
            m_dragValid = m_areas->PreviewRect(m_selectedId, hit.x + m_dragOffsetX, hit.z + m_dragOffsetZ, cell, m_dragRect);
        }
        if (!down)
        {
            // 離した: 置ける位置で、UIの上でなければ、確定する(置けない・UIの上なら、元の位置のまま)。
            if (m_dragValid && hasHit && !overUi)
            {
                if (!m_areas->MoveArea(m_selectedId, hit.x + m_dragOffsetX, hit.z + m_dragOffsetZ, cell, week))
                {
                    Notify("ここへは、動かせません(他の区画と重なります)");
                }
            }
            else if (hasHit && !overUi)
            {
                Notify("ここへは、動かせません(他の区画と重なります)");
            }
            Cancel();
        }
        return;
    }

    if (!hasHit || overUi || m_context->inputConsumed)
    {
        return;
    }
    m_hasHover = true;
    m_hoverX = hit.x;
    m_hoverZ = hit.z;
    const uint32_t under = m_areas->FindAreaAt(hit.x, hit.z);
    m_hoverOverArea = (under != 0);
    m_hoverValid = m_areas->PreviewRect(0, hit.x, hit.z, cell, m_hoverRect);

    if (m_context->input->IsActionPressed(InputAction::Decide))
    {
        m_context->inputConsumed = true;
        if (under != 0)
        {
            // 区画を選ぶ。押したまま動かせば、ドラッグで動かせる。
            m_selectedId = under;
            const CommercialArea* area = m_areas->Find(under);
            m_dragging = true;
            m_dragRect = area->rect;
            m_dragValid = true;
            m_dragOffsetX = area->rect.CenterX() - hit.x;
            m_dragOffsetZ = area->rect.CenterZ() - hit.z;
        }
        else
        {
            const uint32_t id = m_areas->PlaceArea(hit.x, hit.z, cell);
            if (id != 0)
            {
                m_selectedId = id;
            }
            else
            {
                Notify("ここには置けません(他の区画と重なります)");
            }
        }
    }
}

void AreaToolController::Draw(DebugRenderer& debug) const
{
    if (!m_areas)
    {
        return;
    }
    const Field* field = m_field;
    for (const CommercialArea& area : m_areas->Areas())
    {
        const bool selected = (area.id == m_selectedId);
        const XMFLOAT4 color = area.published ? kColorPublished : kColorUnpublished;
        // 動かしている最中の区画は、元の位置を細く残し、動かした先は、下で色を変えて描く。
        AddRect(debug, field, area.rect, color, 0.0f);
        if (selected)
        {
            AddRect(debug, field, area.rect, color, 0.08f);     // 選んだ区画は、二重の枠
            AddRect(debug, field, area.rect, color, 0.16f);
        }
    }
    // 選んだ区画に所属する施設の敷地の枠(どの店が数えられているか)。
    if (m_selectedId != 0 && m_context && m_context->objects)
    {
        if (const AreaMembers* members = m_areas->MembersOf(m_selectedId))
        {
            for (uint32_t id : members->facilityIds)
            {
                if (const GameObject* object = GameObjectFactory::FindById(*m_context->objects, id))
                {
                    AddOrientedRect(debug, field, object->placement.footprintRect, kColorMember);
                }
            }
        }
    }
    if (m_context && m_context->placementTool == PlacementTool::Area)
    {
        if (m_dragging)
        {
            AddRect(debug, field, m_dragRect, m_dragValid ? kColorValid : kColorInvalid, 0.0f);
            AddRect(debug, field, m_dragRect, m_dragValid ? kColorValid : kColorInvalid, 0.1f);
        }
        else if (m_hasHover && !m_hoverOverArea)
        {
            AddRect(debug, field, m_hoverRect, m_hoverValid ? kColorValid : kColorInvalid, 0.0f);    // 置いた場合の枠
        }
    }
}
