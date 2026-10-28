#include "ResidentInfoPanel.h"
#include <algorithm>
#include "UILayoutUtil.h"

namespace
{
    // ---- 基準の寸法(画面の高さ1080の時のピクセル数)。実際には倍率をかけて使う ----
    constexpr float kMargin = 16.0f;
    constexpr float kPanelWidth = 520.0f;
    constexpr float kPadding = 20.0f;
    constexpr float kTitleFont = 26.0f;
    constexpr float kSubtitleFont = 17.0f;
    constexpr float kTextFont = 19.0f;
    constexpr float kNoteFont = 16.0f;
    constexpr float kRowHeight = 28.0f;
    constexpr float kHeaderHeight = 30.0f;
    constexpr float kNoteHeight = 46.0f;
    constexpr float kRowMinScale = 0.55f;       // 高さが足りない時、行をここまで縮める
    constexpr float kLabelWidth = 190.0f;       // ラベルの幅(ゲージは、その右)
    constexpr float kValueWidth = 120.0f;       // 値の幅(右寄せ)
    constexpr float kGaugeHeight = 12.0f;
    constexpr float kCloseWidth = 140.0f;
    constexpr float kCloseHeight = 44.0f;
    constexpr float kTitleBlockHeight = 62.0f;

    float RowHeightOf(const ResidentInfoRow& row)
    {
        switch (row.kind)
        {
        case ResidentInfoRow::Kind::Header: return kHeaderHeight;
        case ResidentInfoRow::Kind::Note:   return kNoteHeight;
        case ResidentInfoRow::Kind::Value:  break;
        }
        return kRowHeight;
    }
}

ResidentInfoPanel::Layout ResidentInfoPanel::ComputeLayout(const ResidentPanelInput& input, const ResidentInfoModel& model) const
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
    const float availableHeight = (std::max)(120.0f * s, input.screenH - input.topInset - margin * 2.0f);

    // 行の高さの合計(基準の寸法)。足りない時は、行を縮める。
    float rowsTotal = 0.0f;
    for (const ResidentInfoRow& row : model.rows) { rowsTotal += RowHeightOf(row); }
    if (!model.emptyMessage.empty()) { rowsTotal = kRowHeight * 2.0f; }
    const float fixedHeight = (kTitleBlockHeight + kCloseHeight) * s + pad * 3.0f;
    float rowScale = s;
    if (fixedHeight + rowsTotal * s > availableHeight)
    {
        rowScale = (std::max)(kRowMinScale * s, (availableHeight - fixedHeight) / (std::max)(1.0f, rowsTotal));
    }
    const float panelHeight = (std::min)(availableHeight, fixedHeight + rowsTotal * rowScale);

    // 右寄せ。画面の右端から、margin離す。
    L.panel = UIRect{ input.screenW - margin - width, input.topInset + margin, width, panelHeight };

    L.titleSize = UIRoundFontSize(kTitleFont * s);
    L.titleX = L.panel.x + pad;
    L.titleY = L.panel.y + pad;
    L.subtitleSize = UIRoundFontSize(kSubtitleFont * s);
    L.subtitleX = L.panel.x + pad;
    L.subtitleY = L.titleY + L.titleSize * 1.35f;
    L.textSize = UIRoundFontSize(kTextFont * (rowScale / s) * s);
    L.noteSize = UIRoundFontSize(kNoteFont * (rowScale / s) * s);

    L.closeButton = UIRect{ L.panel.Right() - pad - kCloseWidth * s, L.panel.Bottom() - pad - kCloseHeight * s, kCloseWidth * s, kCloseHeight * s };

    float y = L.panel.y + pad + kTitleBlockHeight * s;
    const float left = L.panel.x + pad;
    const float right = L.panel.Right() - pad;
    const float labelWidth = kLabelWidth * s;
    const float valueWidth = kValueWidth * s;

    if (!model.emptyMessage.empty())
    {
        L.emptyX = left;
        L.emptyY = y;
        return L;
    }

    // 「閉じる」ボタンより下には、行を置かない。画面が極端に小さくて、縮めても入らない時は、下の行を出さない(上から順に、入るだけ)。
    const float rowsBottom = L.closeButton.y - pad * 0.5f;
    for (const ResidentInfoRow& row : model.rows)
    {
        RowLayout r;
        const float height = RowHeightOf(row) * rowScale;
        if (y + height > rowsBottom + 0.01f)
        {
            break;
        }
        r.rowRect = UIRect{ left, y, right - left, height };
        r.labelX = left;
        r.labelY = y + (std::max)(0.0f, (height - L.textSize * 1.3f) * 0.5f);
        r.valueRight = right;
        if (row.kind == ResidentInfoRow::Kind::Value && row.gauge >= 0.0f)
        {
            const float gaugeX = left + labelWidth;
            const float gaugeW = (std::max)(0.0f, right - valueWidth - gaugeX - 8.0f * s);
            const float gaugeH = kGaugeHeight * s;
            r.gaugeRect = UIRect{ gaugeX, y + (height - gaugeH) * 0.5f, gaugeW, gaugeH };
        }
        if (row.kind == ResidentInfoRow::Kind::Note)
        {
            r.labelY = y + 2.0f * s;
            r.noteY = r.labelY + L.textSize * 1.3f;
        }
        L.rows.push_back(r);
        y += height;
    }
    return L;
}

ResidentPanelResult ResidentInfoPanel::Update(const ResidentPanelInput& input, const ResidentInfoModel& model)
{
    ResidentPanelResult result;
    m_hoverClose = false;
    m_layout = ComputeLayout(input, model);
    if (!m_open)
    {
        return result;
    }

    // 開いている間は、画面全体が、パネル(後ろのゲームへ何も届かない)。ImGuiの上は、反応しない。
    const bool usable = !input.blockedByOtherUI;
    result.mouseOverUI = usable;
    if (!usable)
    {
        return result;
    }
    m_hoverClose = m_layout.closeButton.Contains(input.mouseX, input.mouseY);

    if (!input.pressed)
    {
        return result;
    }
    result.consumedClick = true;

    // 「閉じる」・パネルの外のクリックで閉じる。パネルの中(何も無い所)は、受け取るだけ。
    if (m_hoverClose || !m_layout.panel.Contains(input.mouseX, input.mouseY))
    {
        m_open = false;
        result.closed = true;
        m_layout = ComputeLayout(input, model);
    }
    return result;
}

void ResidentInfoPanel::Draw(UIRenderer& ui, IUISkin& skin, const ResidentInfoModel& model) const
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

    if (!model.emptyMessage.empty())
    {
        ui.DrawString(L.emptyX, L.emptyY, model.emptyMessage, L.textSize, skin.GetTextStyle(UITextRole::Disabled));
    }

    for (size_t i = 0; i < model.rows.size() && i < L.rows.size(); ++i)
    {
        const ResidentInfoRow& row = model.rows[i];
        const RowLayout& r = L.rows[i];

        switch (row.kind)
        {
        case ResidentInfoRow::Kind::Header:
        {
            ui.DrawString(r.labelX, r.labelY, row.label, L.textSize, skin.GetTextStyle(UITextRole::Sub));
            // 見出しの下の線。
            ui.FillRect(UIRect{ r.rowRect.x, r.rowRect.Bottom() - 2.0f * s, r.rowRect.w, 1.5f * s }, UIColor(0.55f, 0.40f, 0.25f, 0.45f));
            break;
        }
        case ResidentInfoRow::Kind::Value:
        {
            ui.DrawString(r.labelX, r.labelY, row.label, L.textSize, skin.GetTextStyle(UITextRole::Normal));
            if (r.gaugeRect.w > 0.0f)
            {
                skin.DrawGauge(ui, r.gaugeRect, row.gauge, row.marker, row.warning, s);
            }
            const UITextStyle valueStyle = skin.GetTextStyle(row.warning ? UITextRole::Warning : UITextRole::Normal);
            const float valueWidth = ui.MeasureString(row.value, L.textSize);
            ui.DrawString(r.valueRight - valueWidth, r.labelY, row.value, L.textSize, valueStyle);
            break;
        }
        case ResidentInfoRow::Kind::Note:
        {
            ui.DrawString(r.labelX, r.labelY, row.label, L.textSize, skin.GetTextStyle(UITextRole::Normal));
            ui.DrawString(r.labelX, r.noteY, row.value, L.noteSize, skin.GetTextStyle(row.warning ? UITextRole::Warning : UITextRole::Sub));
            break;
        }
        }
    }

    // 「閉じる」ボタン。
    const UIButtonState closeState = m_hoverClose ? UIButtonState::Hover : UIButtonState::Normal;
    skin.DrawButton(ui, L.closeButton, closeState, s);
    const std::string closeLabel = "閉じる";
    const float closeSize = UIRoundFontSize(kTextFont * s);
    const float closeWidth = ui.MeasureString(closeLabel, closeSize);
    ui.DrawString(L.closeButton.x + (L.closeButton.w - closeWidth) * 0.5f,
                  L.closeButton.y + (L.closeButton.h - ui.GetLineHeight(closeSize)) * 0.5f,
                  closeLabel, closeSize, skin.GetTextStyle(UITextRole::Normal));
}
