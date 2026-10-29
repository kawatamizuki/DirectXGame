#include "MoveInPanel.h"
#include <algorithm>
#include <cmath>
#include "UILayoutUtil.h"

namespace
{
    // ---- 基準の寸法(画面の高さ1080の時のピクセル数)。実際には倍率をかけて使う ----
    constexpr float kMargin = 16.0f;
    constexpr float kPanelWidth = 640.0f;
    constexpr float kPadding = 20.0f;
    constexpr float kTitleFont = 26.0f;
    constexpr float kSubtitleFont = 17.0f;
    constexpr float kTextFont = 19.0f;
    constexpr float kDetailFont = 15.0f;
    constexpr float kTitleBlockHeight = 62.0f;
    constexpr float kRowHeight = 62.0f;
    constexpr float kRowGap = 6.0f;
    constexpr float kButtonHeight = 44.0f;
    constexpr float kDeclineWidth = 280.0f;
    constexpr float kPageButtonWidth = 80.0f;
    constexpr float kPageTextWidth = 70.0f;
}

MoveInPanel::Layout MoveInPanel::ComputeLayout(const MoveInInput& input, const MoveInModel& model) const
{
    Layout L;
    const float s = UIScaleForHeight(input.screenH);
    L.scale = s;
    L.open = m_open;
    if (!m_open)
    {
        return L;
    }
    L.overlay = UIRect{ 0.0f, input.topInset, input.screenW, (std::max)(0.0f, input.screenH - input.topInset) };

    const float margin = kMargin * s;
    const float pad = kPadding * s;
    const float width = (std::min)(kPanelWidth * s, input.screenW - margin * 2.0f);
    const float availableHeight = (std::max)(180.0f * s, input.screenH - input.topInset - margin * 2.0f);

    // 1ページに入る行数(タイトルと下のボタンの分を引いた残り)。
    const float fixedHeight = kTitleBlockHeight * s + kButtonHeight * s + pad * 3.0f;
    const float rowStep = (kRowHeight + kRowGap) * s;
    L.rowsPerPage = (std::max)(1, static_cast<int>(std::floor((availableHeight - fixedHeight) / rowStep)));
    const int rowCount = static_cast<int>(model.rows.size());
    L.pageCount = (std::max)(1, (rowCount + L.rowsPerPage - 1) / L.rowsPerPage);
    L.page = (std::max)(0, (std::min)(m_page, L.pageCount - 1));

    // 高さは、1ページの行数で決める(ページを送っても、枠の大きさが変わらないように、最大の行数で)。
    const int heightRows = (std::max)(1, (std::min)(rowCount, L.rowsPerPage));
    const float panelHeight = (std::min)(availableHeight, fixedHeight + rowStep * static_cast<float>(heightRows));
    L.panel = UIRect{ (input.screenW - width) * 0.5f, input.topInset + margin + (availableHeight - panelHeight) * 0.5f, width, panelHeight };

    L.titleSize = UIRoundFontSize(kTitleFont * s);
    L.titleX = L.panel.x + pad;
    L.titleY = L.panel.y + pad;
    L.subtitleSize = UIRoundFontSize(kSubtitleFont * s);
    L.subtitleX = L.panel.x + pad;
    L.subtitleY = L.titleY + L.titleSize * 1.35f;
    L.textSize = UIRoundFontSize(kTextFont * s);
    L.detailSize = UIRoundFontSize(kDetailFont * s);

    // 下のボタン: 右に「新しい住民を迎える」、左にページ送り(ページが複数の時だけ)。
    const float buttonH = kButtonHeight * s;
    const float buttonY = L.panel.Bottom() - pad - buttonH;
    const float declineW = (std::min)(kDeclineWidth * s, L.panel.w - pad * 2.0f);
    L.declineButton = UIRect{ L.panel.Right() - pad - declineW, buttonY, declineW, buttonH };
    if (L.pageCount > 1)
    {
        const float pageW = kPageButtonWidth * s;
        L.prevButton = UIRect{ L.panel.x + pad, buttonY, pageW, buttonH };
        L.pageTextX = L.prevButton.Right() + 10.0f * s;
        L.pageTextY = buttonY + (buttonH - L.textSize * 1.3f) * 0.5f;
        L.nextButton = UIRect{ L.pageTextX + kPageTextWidth * s, buttonY, pageW, buttonH };
        L.pageText = std::to_string(L.page + 1) + " / " + std::to_string(L.pageCount);
    }

    // 今のページの行。下のボタンより下には、置かない。
    float y = L.panel.y + pad + kTitleBlockHeight * s;
    const float rowsBottom = buttonY - pad * 0.5f;
    const int first = L.page * L.rowsPerPage;
    for (int i = first; i < rowCount && i < first + L.rowsPerPage; ++i)
    {
        const float h = kRowHeight * s;
        if (y + h > rowsBottom + 0.01f)
        {
            break;
        }
        RowLayout r;
        r.rect = UIRect{ L.panel.x + pad, y, L.panel.w - pad * 2.0f, h };
        r.titleX = r.rect.x + 14.0f * s;
        r.titleY = r.rect.y + 7.0f * s;
        r.detailX = r.titleX;
        r.detailY = r.titleY + L.textSize * 1.3f;
        r.modelIndex = static_cast<size_t>(i);
        L.rows.push_back(r);
        y += rowStep;
    }
    return L;
}

MoveInResult MoveInPanel::Update(const MoveInInput& input, const MoveInModel& model)
{
    MoveInResult result;
    m_hoverRow = -1;
    m_hoverDecline = m_hoverPrev = m_hoverNext = false;
    m_layout = ComputeLayout(input, model);
    if (!m_open)
    {
        return result;
    }
    m_page = m_layout.page; // 範囲に収めた値を保つ(住民が減って、ページが無くなった時など)

    // 開いている間は、画面全体が、パネル(後ろのゲームへ何も届かない)。ImGuiの上は、反応しない。
    const bool usable = !input.blockedByOtherUI;
    result.mouseOverUI = usable;
    if (!usable)
    {
        return result;
    }

    for (size_t i = 0; i < m_layout.rows.size(); ++i)
    {
        if (m_layout.rows[i].rect.Contains(input.mouseX, input.mouseY)) { m_hoverRow = static_cast<int>(i); break; }
    }
    m_hoverDecline = m_layout.declineButton.Contains(input.mouseX, input.mouseY);
    m_hoverPrev = m_layout.pageCount > 1 && m_layout.prevButton.Contains(input.mouseX, input.mouseY);
    m_hoverNext = m_layout.pageCount > 1 && m_layout.nextButton.Contains(input.mouseX, input.mouseY);

    if (!input.pressed)
    {
        return result;
    }
    result.consumedClick = true; // パネルの外のクリックも、受け取るだけ(閉じない。誤操作を避ける)

    if (m_hoverRow >= 0)
    {
        const size_t modelIndex = m_layout.rows[static_cast<size_t>(m_hoverRow)].modelIndex;
        if (modelIndex < model.rows.size())
        {
            result.chosenResidentId = model.rows[modelIndex].residentId;
            m_open = false;
            m_layout = ComputeLayout(input, model);
        }
    }
    else if (m_hoverDecline)
    {
        result.declined = true;
        m_open = false;
        m_layout = ComputeLayout(input, model);
    }
    else if (m_hoverPrev && m_page > 0)
    {
        --m_page;
        m_layout = ComputeLayout(input, model);
    }
    else if (m_hoverNext && m_page + 1 < m_layout.pageCount)
    {
        ++m_page;
        m_layout = ComputeLayout(input, model);
    }
    return result;
}

void MoveInPanel::Draw(UIRenderer& ui, IUISkin& skin, const MoveInModel& model) const
{
    const Layout& L = m_layout;
    if (!L.open)
    {
        return;
    }
    const float s = L.scale;

    skin.DrawPanel(ui, L.overlay, UIPanelStyle::Overlay, s);
    skin.DrawPanel(ui, L.panel, UIPanelStyle::Window, s);

    ui.DrawString(L.titleX, L.titleY, model.title, L.titleSize, skin.GetTextStyle(UITextRole::Heading));
    ui.DrawString(L.subtitleX, L.subtitleY, model.subtitle, L.subtitleSize, skin.GetTextStyle(UITextRole::Sub));

    for (size_t i = 0; i < L.rows.size(); ++i)
    {
        const RowLayout& r = L.rows[i];
        if (r.modelIndex >= model.rows.size())
        {
            continue;
        }
        const MoveInRow& row = model.rows[r.modelIndex];
        skin.DrawButton(ui, r.rect, (static_cast<int>(i) == m_hoverRow) ? UIButtonState::Hover : UIButtonState::Normal, s);
        ui.DrawString(r.titleX, r.titleY, row.title, L.textSize, skin.GetTextStyle(UITextRole::Normal));
        ui.DrawString(r.detailX, r.detailY, row.detail, L.detailSize, skin.GetTextStyle(row.warning ? UITextRole::Warning : UITextRole::Sub));
    }

    // 下のボタン。
    auto drawButtonLabel = [&](const UIRect& rect, const std::string& label, UIButtonState state)
    {
        skin.DrawButton(ui, rect, state, s);
        const float size = UIRoundFontSize(kTextFont * s);
        const float labelWidth = ui.MeasureString(label, size);
        ui.DrawString(rect.x + (rect.w - labelWidth) * 0.5f, rect.y + (rect.h - ui.GetLineHeight(size)) * 0.5f, label, size,
                      skin.GetTextStyle(state == UIButtonState::Disabled ? UITextRole::Disabled : UITextRole::Normal));
    };
    drawButtonLabel(L.declineButton, model.declineLabel, m_hoverDecline ? UIButtonState::Hover : UIButtonState::Normal);
    if (L.pageCount > 1)
    {
        drawButtonLabel(L.prevButton, "前へ", L.page > 0 ? (m_hoverPrev ? UIButtonState::Hover : UIButtonState::Normal) : UIButtonState::Disabled);
        drawButtonLabel(L.nextButton, "次へ", (L.page + 1 < L.pageCount) ? (m_hoverNext ? UIButtonState::Hover : UIButtonState::Normal) : UIButtonState::Disabled);
        ui.DrawString(L.pageTextX, L.pageTextY, L.pageText, L.textSize, skin.GetTextStyle(UITextRole::Sub));
    }
}
