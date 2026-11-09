#include "AreaPanel.h"
#include <algorithm>
#include <sstream>
#include "UILayoutUtil.h"

namespace
{
    // ---- 基準の寸法(画面の高さ1080の時のピクセル数)。実際には倍率をかけて使う ----
    constexpr float kMargin = 16.0f;
    constexpr float kPanelWidth = 420.0f;
    constexpr float kPadding = 18.0f;
    constexpr float kTitleFont = 24.0f;
    constexpr float kSubtitleFont = 17.0f;
    constexpr float kTextFont = 18.0f;
    constexpr float kNoteFont = 15.0f;
    constexpr float kRowHeight = 28.0f;
    constexpr float kHeaderHeight = 30.0f;
    constexpr float kNoteHeight = 44.0f;
    constexpr float kRowMinScale = 0.55f;
    constexpr float kLabelWidth = 170.0f;
    constexpr float kValueWidth = 130.0f;
    constexpr float kGaugeHeight = 12.0f;
    constexpr float kButtonHeight = 42.0f;
    constexpr float kButtonGap = 10.0f;
    constexpr float kTitleBlockHeight = 56.0f;
    constexpr float kConfirmLineHeight = 24.0f;

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

    std::vector<std::string> SplitLines(const std::string& text)
    {
        std::vector<std::string> lines;
        std::stringstream stream(text);
        std::string line;
        while (std::getline(stream, line, '\n'))
        {
            lines.push_back(line);
        }
        return lines;
    }
}

AreaPanel::Layout AreaPanel::ComputeLayout(const AreaPanelInput& input, const AreaPanelModel& model) const
{
    Layout L;
    const float s = UIScaleForHeight(input.screenH);
    L.scale = s;
    L.visible = model.visible;
    if (!model.visible)
    {
        return L;
    }
    const float margin = kMargin * s;
    const float pad = kPadding * s;
    const float width = (std::min)(kPanelWidth * s, input.screenW - margin * 2.0f);
    // 右下の「メニュー」ボタン・左下の操作のヒントと重ならないよう、下を空ける(約110)。行が多い時は、行を縮める。
    const float availableHeight = (std::max)(160.0f * s, input.screenH - input.topInset - margin * 2.0f - 110.0f * s);

    // 下の部分: 通常 = ボタンの1段。確認中 = 確認の文 + ボタンの1段。
    const bool confirming = (m_confirm != Confirm::None);
    if (confirming)
    {
        L.confirmLines = SplitLines(m_confirm == Confirm::Publish ? model.publishConfirm : model.deleteConfirm);
    }
    const float confirmHeight = confirming ? (static_cast<float>(L.confirmLines.size()) * kConfirmLineHeight + 10.0f) * s : 0.0f;
    const float bottomHeight = confirmHeight + kButtonHeight * s;

    float rowsTotal = 0.0f;
    for (const ResidentInfoRow& row : model.rows) { rowsTotal += RowHeightOf(row); }
    const float fixedHeight = kTitleBlockHeight * s + pad * 3.0f + bottomHeight;
    float rowScale = s;
    if (fixedHeight + rowsTotal * s > availableHeight)
    {
        rowScale = (std::max)(kRowMinScale * s, (availableHeight - fixedHeight) / (std::max)(1.0f, rowsTotal));
    }
    const float panelHeight = (std::min)(availableHeight, fixedHeight + rowsTotal * rowScale);

    // 右寄せ。画面の右端から margin、上は HUD の下から margin。
    L.panel = UIRect{ input.screenW - margin - width, input.topInset + margin, width, panelHeight };
    L.titleSize = UIRoundFontSize(kTitleFont * s);
    L.titleX = L.panel.x + pad;
    L.titleY = L.panel.y + pad;
    L.subtitleSize = UIRoundFontSize(kSubtitleFont * s);
    L.subtitleX = L.panel.x + pad;
    L.subtitleY = L.titleY + L.titleSize * 1.35f;
    L.textSize = UIRoundFontSize(kTextFont * (rowScale / s) * s);
    L.noteSize = UIRoundFontSize(kNoteFont * (rowScale / s) * s);

    // ボタンの段(パネルの下端)。
    const float buttonsY = L.panel.Bottom() - pad - kButtonHeight * s;
    std::vector<std::string> labels;
    std::vector<bool> enabled;
    if (confirming)
    {
        labels = { m_confirm == Confirm::Publish ? "はい、周知する" : "はい", "やめる" };
        enabled = { true, true };
    }
    else
    {
        if (!model.publishLabel.empty()) { labels.push_back(model.publishLabel); enabled.push_back(true); }
        labels.push_back(model.deleteLabel.empty() ? "削除" : model.deleteLabel); enabled.push_back(true);
        labels.push_back("閉じる"); enabled.push_back(true);
    }
    const float gap = kButtonGap * s;
    const float count = static_cast<float>(labels.size());
    const float buttonWidth = (L.panel.w - pad * 2.0f - gap * (count - 1.0f)) / count;
    for (size_t i = 0; i < labels.size(); ++i)
    {
        ButtonLayout b;
        b.rect = UIRect{ L.panel.x + pad + (buttonWidth + gap) * static_cast<float>(i), buttonsY, buttonWidth, kButtonHeight * s };
        b.label = labels[i];
        b.enabled = enabled[i];
        L.buttons.push_back(b);
    }
    if (confirming)
    {
        L.confirmX = L.panel.x + pad;
        L.confirmLineHeight = kConfirmLineHeight * s;
        L.confirmY = buttonsY - confirmHeight + 4.0f * s;
    }

    // 行。ボタン(と確認の文)より下には置かない。
    float y = L.panel.y + pad + kTitleBlockHeight * s;
    const float left = L.panel.x + pad;
    const float right = L.panel.Right() - pad;
    const float labelWidth = kLabelWidth * s;
    const float valueWidth = kValueWidth * s;
    const float rowsBottom = buttonsY - confirmHeight - pad * 0.5f;
    for (const ResidentInfoRow& row : model.rows)
    {
        ResidentInfoPanel::RowLayout r;
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

AreaPanelResult AreaPanel::Update(const AreaPanelInput& input, const AreaPanelModel& model)
{
    AreaPanelResult result;
    m_hoverButton = -1;
    if (!model.visible)
    {
        m_confirm = Confirm::None;      // 区画の選びが外れたら、確認の途中も、消す
        m_layout = ComputeLayout(input, model);
        return result;
    }
    m_layout = ComputeLayout(input, model);
    const bool usable = !input.blockedByOtherUI;
    if (!usable)
    {
        return result;
    }
    const bool over = m_layout.panel.Contains(input.mouseX, input.mouseY);
    result.mouseOverUI = over;
    if (!over)
    {
        return result;
    }
    for (size_t i = 0; i < m_layout.buttons.size(); ++i)
    {
        if (m_layout.buttons[i].enabled && m_layout.buttons[i].rect.Contains(input.mouseX, input.mouseY))
        {
            m_hoverButton = static_cast<int>(i);
        }
    }
    if (!input.pressed)
    {
        return result;
    }
    result.consumedClick = true;
    if (m_hoverButton < 0)
    {
        return result;
    }

    if (m_confirm != Confirm::None)
    {
        // 確認中: 「はい」で確定、「やめる」で戻る。
        if (m_hoverButton == 0)
        {
            (m_confirm == Confirm::Publish ? result.publishConfirmed : result.deleteConfirmed) = true;
        }
        m_confirm = Confirm::None;
    }
    else
    {
        const bool hasPublish = !model.publishLabel.empty();
        int index = m_hoverButton;
        if (hasPublish)
        {
            if (index == 0)
            {
                m_confirm = model.publishConfirm.empty() ? Confirm::None : Confirm::Publish;
                result.publishConfirmed = model.publishConfirm.empty();
                index = -1;
            }
            else
            {
                --index;
            }
        }
        if (index == 0)
        {
            // 削除(撤回)。確認の文があれば、確認の手順へ。無ければ、すぐ。
            if (model.deleteConfirm.empty()) { result.deleteConfirmed = true; }
            else { m_confirm = Confirm::Delete; }
        }
        else if (index == 1)
        {
            result.closeRequested = true;
        }
    }
    m_layout = ComputeLayout(input, model);
    return result;
}

void AreaPanel::Draw(UIRenderer& ui, IUISkin& skin, const AreaPanelModel& model) const
{
    const Layout& L = m_layout;
    if (!L.visible)
    {
        return;
    }
    const float s = L.scale;
    skin.DrawPanel(ui, L.panel, UIPanelStyle::Window, s);
    ui.DrawString(L.titleX, L.titleY, model.title, L.titleSize, skin.GetTextStyle(UITextRole::Heading));
    ui.DrawString(L.subtitleX, L.subtitleY, model.subtitle, L.subtitleSize, skin.GetTextStyle(UITextRole::Sub));

    for (size_t i = 0; i < model.rows.size() && i < L.rows.size(); ++i)
    {
        const ResidentInfoRow& row = model.rows[i];
        const ResidentInfoPanel::RowLayout& r = L.rows[i];
        switch (row.kind)
        {
        case ResidentInfoRow::Kind::Header:
            ui.DrawString(r.labelX, r.labelY, row.label, L.textSize, skin.GetTextStyle(UITextRole::Sub));
            ui.FillRect(UIRect{ r.rowRect.x, r.rowRect.Bottom() - 2.0f * s, r.rowRect.w, 1.5f * s }, UIColor(0.55f, 0.40f, 0.25f, 0.45f));
            break;
        case ResidentInfoRow::Kind::Value:
        {
            ui.DrawString(r.labelX, r.labelY, row.label, L.textSize, skin.GetTextStyle(UITextRole::Normal));
            if (r.gaugeRect.w > 0.0f)
            {
                skin.DrawGauge(ui, r.gaugeRect, row.gauge, row.marker, row.warning, s);
            }
            const float valueWidth = ui.MeasureString(row.value, L.textSize);
            ui.DrawString(r.valueRight - valueWidth, r.labelY, row.value, L.textSize, skin.GetTextStyle(row.warning ? UITextRole::Warning : UITextRole::Normal));
            break;
        }
        case ResidentInfoRow::Kind::Note:
            ui.DrawString(r.labelX, r.labelY, row.label, L.textSize, skin.GetTextStyle(UITextRole::Normal));
            ui.DrawString(r.labelX, r.noteY, row.value, L.noteSize, skin.GetTextStyle(row.warning ? UITextRole::Warning : UITextRole::Sub));
            break;
        }
    }

    // 確認の文(確認の手順の間だけ)。
    for (size_t i = 0; i < L.confirmLines.size(); ++i)
    {
        ui.DrawString(L.confirmX, L.confirmY + L.confirmLineHeight * static_cast<float>(i), L.confirmLines[i], L.textSize, skin.GetTextStyle(UITextRole::Warning));
    }

    // ボタン。
    const float buttonFont = UIRoundFontSize(kTextFont * s);
    for (size_t i = 0; i < L.buttons.size(); ++i)
    {
        const ButtonLayout& b = L.buttons[i];
        const UIButtonState state = !b.enabled ? UIButtonState::Disabled : (static_cast<int>(i) == m_hoverButton ? UIButtonState::Hover : UIButtonState::Normal);
        skin.DrawButton(ui, b.rect, state, s);
        const float labelWidth = ui.MeasureString(b.label, buttonFont);
        ui.DrawString(b.rect.x + (b.rect.w - labelWidth) * 0.5f, b.rect.y + (b.rect.h - ui.GetLineHeight(buttonFont)) * 0.5f,
                      b.label, buttonFont, skin.GetTextStyle(b.enabled ? UITextRole::Normal : UITextRole::Disabled));
    }
}
