#include "GameMenu.h"
#include <algorithm>
#include "UILayoutUtil.h"

namespace
{
    // ---- 基準の寸法(画面の高さ1080の時のピクセル数)。実際には倍率をかけて使う ----
    constexpr float kMargin = 12.0f;
    constexpr float kToggleWidth = 150.0f;
    constexpr float kToggleHeight = 56.0f;
    constexpr float kListWidth = 210.0f;
    constexpr float kTabHeight = 60.0f;
    constexpr float kTabGap = 6.0f;
    constexpr float kListPadding = 12.0f;
    constexpr float kContentMaxWidth = 700.0f;
    constexpr float kContentPadding = 16.0f;
    constexpr float kTitleHeight = 40.0f;
    constexpr float kHeadingHeight = 26.0f;
    constexpr float kRowGap = 8.0f;
    constexpr float kColumnGap = 16.0f;
    constexpr float kRowMaxHeight = 64.0f;
    constexpr float kRowMinHeight = 32.0f;
    constexpr float kInfoRowHeight = 38.0f;
    constexpr float kSystemButtonMaxWidth = 380.0f;
    constexpr float kPillMaxWidth = 380.0f;
    constexpr float kPillMinWidth = 170.0f;
    constexpr float kCancelWidth = 110.0f;
    constexpr float kCancelHeight = 40.0f;

    constexpr float kLabelFont = 22.0f;
    constexpr float kDetailFont = 18.0f;
    constexpr float kTitleFont = 26.0f;
    constexpr float kHeadingFont = 17.0f;
    constexpr float kInfoFont = 20.0f;

    const char* const kTabLabels[] = { "建築", "情報", "研究", "システム" };
    const UIIcon kTabIcons[] = { UIIcon::Build, UIIcon::Info, UIIcon::Research, UIIcon::System };

    bool ClosesMenu(const MenuCommand& command)
    {
        return command.type == MenuCommandType::SelectBuilding || command.type == MenuCommandType::SelectRoad;
    }
}

GameMenu::Layout GameMenu::ComputeLayout(const MenuInput& input, const MenuModel& model) const
{
    Layout L;
    const float s = UIScaleForHeight(input.screenH);
    L.scale = s;
    L.screenW = input.screenW;
    L.screenH = input.screenH;
    L.enabled = input.enabled;
    if (!input.enabled)
    {
        return L; // メニューを使えない時は、何も出さない
    }
    L.open = m_open;
    L.labelSize = UIRoundFontSize(kLabelFont * s);
    L.detailSize = UIRoundFontSize(kDetailFont * s);

    const float margin = kMargin * s;

    // ---- 右下の「メニュー」/「戻る」: 開いていても閉じていても、同じ場所 ----
    Button toggle;
    toggle.kind = ButtonKind::Toggle;
    toggle.rect = UIRect{ input.screenW - margin - kToggleWidth * s, input.screenH - margin - kToggleHeight * s, kToggleWidth * s, kToggleHeight * s };
    toggle.label = m_open ? "戻る" : "メニュー";
    toggle.icon = m_open ? UIIcon::Back : UIIcon::Menu;
    toggle.centerLabel = true;
    L.buttons.push_back(toggle);

    // ---- 閉じている時: 配置中のしるし(左下)。「建築中: 住宅  [やめる]」 ----
    if (!m_open)
    {
        if (!model.activeToolLabel.empty())
        {
            float width = (std::min)(kPillMaxWidth * s, toggle.rect.x - margin * 2.0f);
            if (width >= kPillMinWidth * s)
            {
                L.activePill = UIRect{ margin, input.screenH - margin - kToggleHeight * s, width, kToggleHeight * s };

                Text text;
                text.size = L.labelSize;
                text.text = "建築中: " + model.activeToolLabel;
                text.x = L.activePill.x + 16.0f * s;
                text.y = L.activePill.y + (L.activePill.h - L.labelSize * 1.3f) * 0.5f;
                L.texts.push_back(text);

                Button cancel;
                cancel.kind = ButtonKind::Cancel;
                cancel.rect = UIRect{ L.activePill.Right() - 8.0f * s - kCancelWidth * s, L.activePill.y + (L.activePill.h - kCancelHeight * s) * 0.5f, kCancelWidth * s, kCancelHeight * s };
                cancel.label = "やめる";
                cancel.centerLabel = true;
                cancel.icon = UIIcon::Back;
                cancel.command.type = MenuCommandType::CancelPlacement;
                L.buttons.push_back(cancel);
            }
        }
        return L;
    }

    // ---- 開いている時 ----
    const float top = input.topInset;
    L.overlay = UIRect{ 0.0f, top, input.screenW, (std::max)(0.0f, input.screenH - top) };

    const float areaTop = top + margin;
    const float areaBottom = toggle.rect.y - margin;
    const float areaHeight = (std::max)(60.0f * s, areaBottom - areaTop);

    // 左の項目リスト(建築・情報・研究・システム)。高さが足りない時は、項目を低くする。
    const int tabCount = static_cast<int>(Tab::Count);
    const float listPad = kListPadding * s;
    const float tabGap = kTabGap * s;
    float tabHeight = (std::min)(kTabHeight * s, (areaHeight - listPad * 2.0f - tabGap * static_cast<float>(tabCount - 1)) / static_cast<float>(tabCount));
    tabHeight = (std::max)(tabHeight, 24.0f * s);
    const float listHeight = listPad * 2.0f + tabHeight * static_cast<float>(tabCount) + tabGap * static_cast<float>(tabCount - 1);
    L.list = UIRect{ margin, areaTop, kListWidth * s, listHeight };

    for (int i = 0; i < tabCount; ++i)
    {
        Button tab;
        tab.kind = ButtonKind::Tab;
        tab.tab = static_cast<Tab>(i);
        tab.rect = UIRect{ L.list.x + listPad, L.list.y + listPad + (tabHeight + tabGap) * static_cast<float>(i), L.list.w - listPad * 2.0f, tabHeight };
        tab.label = kTabLabels[i];
        tab.icon = kTabIcons[i];
        tab.selected = (tab.tab == m_tab);
        L.buttons.push_back(tab);
    }

    // 選んだ項目の中身の枠。リストの右から、画面の右端まで(広すぎる時は上限まで)。
    const float contentX = L.list.Right() + margin;
    const float contentWidth = (std::min)(kContentMaxWidth * s, (std::max)(120.0f * s, input.screenW - margin - contentX));
    L.content = UIRect{ contentX, areaTop, contentWidth, areaHeight };

    const float pad = kContentPadding * s;
    const float bodyTop = L.content.y + pad + kTitleHeight * s;
    const float bodyHeight = (std::max)(0.0f, L.content.Bottom() - pad - bodyTop);
    const float gap = kRowGap * s;
    const float headingHeight = kHeadingHeight * s;

    // 中身の見出し(選んだタブの名前)。
    {
        Text title;
        title.size = UIRoundFontSize(kTitleFont * s);
        title.text = kTabLabels[static_cast<int>(m_tab)];
        title.x = L.content.x + pad;
        title.y = L.content.y + pad;
        title.role = UITextRole::Heading;
        L.texts.push_back(title);
    }

    auto makeItemButton = [&](const UIRect& rect, const MenuItem& item)
    {
        Button button;
        button.kind = ButtonKind::Item;
        button.rect = rect;
        button.label = item.name;
        button.detail = item.detail;
        button.icon = item.icon;
        button.selected = item.selected;
        button.affordable = item.affordable;
        button.command = item.command;
        button.closesMenu = ClosesMenu(item.command);
        return button;
    };

    auto makeHeading = [&](float x, float y, const char* label)
    {
        Text heading;
        heading.size = UIRoundFontSize(kHeadingFont * s);
        heading.text = label;
        heading.x = x;
        heading.y = y;
        heading.role = UITextRole::Sub;
        return heading;
    };

    switch (m_tab)
    {
    case Tab::Build:
    {
        // 左の列=建物、右の列=道路(+引き方)。行の高さは、収まるように縮める。
        const int buildingCount = static_cast<int>(model.buildings.size());
        const int roadCount = static_cast<int>(model.roads.size());
        const int rows = (std::max)(1, (std::max)(buildingCount, roadCount + 1));
        float rowHeight = (bodyHeight - headingHeight * 2.0f - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows);
        rowHeight = (std::min)(kRowMaxHeight * s, (std::max)(kRowMinHeight * s, rowHeight));

        const float columnGap = kColumnGap * s;
        const float columnWidth = (L.content.w - pad * 2.0f - columnGap) * 0.5f;
        const float leftX = L.content.x + pad;
        const float rightX = leftX + columnWidth + columnGap;

        L.texts.push_back(makeHeading(leftX, bodyTop, "建物"));
        L.texts.push_back(makeHeading(rightX, bodyTop, "道路"));

        float y = bodyTop + headingHeight;
        for (int i = 0; i < buildingCount; ++i)
        {
            L.buttons.push_back(makeItemButton(UIRect{ leftX, y + (rowHeight + gap) * static_cast<float>(i), columnWidth, rowHeight }, model.buildings[i]));
        }
        for (int i = 0; i < roadCount; ++i)
        {
            L.buttons.push_back(makeItemButton(UIRect{ rightX, y + (rowHeight + gap) * static_cast<float>(i), columnWidth, rowHeight }, model.roads[i]));
        }

        // 道路の引き方(直線/自由)。道路の列の、道路の下に並べる。
        const int modeCount = static_cast<int>(model.drawModes.size());
        if (modeCount > 0)
        {
            const float modeY = y + (rowHeight + gap) * static_cast<float>(roadCount);
            L.texts.push_back(makeHeading(rightX, modeY, "引き方"));
            const float modeWidth = (columnWidth - gap * static_cast<float>(modeCount - 1)) / static_cast<float>(modeCount);
            for (int i = 0; i < modeCount; ++i)
            {
                Button button = makeItemButton(UIRect{ rightX + (modeWidth + gap) * static_cast<float>(i), modeY + headingHeight, modeWidth, rowHeight }, model.drawModes[i]);
                button.detail.clear(); // 幅が狭いので、補足は出さない
                L.buttons.push_back(button);
            }
        }
        break;
    }
    case Tab::Info:
    {
        float y = bodyTop;
        for (const MenuInfoLine& line : model.info)
        {
            Text label;
            label.size = UIRoundFontSize(kInfoFont * s);
            label.text = line.label;
            label.x = L.content.x + pad;
            label.y = y;
            L.texts.push_back(label);

            Text value = label;
            value.text = line.value;
            value.x = L.content.Right() - pad;
            value.alignRight = true;
            L.texts.push_back(value);

            y += kInfoRowHeight * s;
        }
        break;
    }
    case Tab::Research:
    {
        Text message;
        message.size = UIRoundFontSize(kInfoFont * s);
        message.text = "研究は、準備中です。";
        message.x = L.content.x + pad;
        message.y = bodyTop;
        message.role = UITextRole::Disabled;
        L.texts.push_back(message);

        Text hint;
        hint.size = UIRoundFontSize(kHeadingFont * s);
        hint.text = "街が育つと、ここで新しい建物や道路を研究できるようになります。";
        hint.x = L.content.x + pad;
        hint.y = bodyTop + 40.0f * s;
        hint.role = UITextRole::Sub;
        L.texts.push_back(hint);
        break;
    }
    case Tab::System:
    {
        const float buttonWidth = (std::min)(kSystemButtonMaxWidth * s, L.content.w - pad * 2.0f);
        const float buttonHeight = (std::min)(kToggleHeight * s, (std::max)(kRowMinHeight * s, (bodyHeight - gap) * 0.5f));

        MenuItem save;
        save.icon = UIIcon::Save;
        save.name = "セーブ";
        save.detail = "F5";
        save.command.type = MenuCommandType::Save;
        MenuItem load;
        load.icon = UIIcon::Load;
        load.name = "ロード";
        load.detail = "F9";
        load.command.type = MenuCommandType::Load;

        float y = bodyTop;
        L.buttons.push_back(makeItemButton(UIRect{ L.content.x + pad, y, buttonWidth, buttonHeight }, save));
        y += buttonHeight + gap;
        L.buttons.push_back(makeItemButton(UIRect{ L.content.x + pad, y, buttonWidth, buttonHeight }, load));
        y += buttonHeight + gap;

        if (!model.statusText.empty())
        {
            Text status;
            status.size = UIRoundFontSize(kHeadingFont * s);
            status.text = model.statusText;
            status.x = L.content.x + pad;
            status.y = y;
            status.role = UITextRole::Sub;
            L.texts.push_back(status);
        }
        break;
    }
    default:
        break;
    }

    return L;
}

int GameMenu::FindButtonAt(float x, float y) const
{
    for (size_t i = 0; i < m_layout.buttons.size(); ++i)
    {
        const Button& button = m_layout.buttons[i];
        if (button.enabled && button.rect.Contains(x, y))
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

MenuResult GameMenu::Update(const MenuInput& input, const MenuModel& model)
{
    MenuResult result;

    if (!input.enabled)
    {
        m_open = false; // 使えない状態(Editingなど)では、開いたままにしない
    }

    m_layout = ComputeLayout(input, model);
    m_hover = -1;
    m_down = false;
    if (!m_layout.enabled)
    {
        return result;
    }

    // ImGuiのパネルの上では反応しない(ImGuiが手前に描かれるため)。
    const bool usable = !input.blockedByOtherUI;
    if (usable)
    {
        m_hover = FindButtonAt(input.mouseX, input.mouseY);
        m_down = input.down;
    }

    // カーソルがメニューの上か。開いている間は、後ろのゲームへ何も届かないよう、画面全体(幕)がメニュー。
    bool over = false;
    if (usable)
    {
        over = (m_hover >= 0) || (m_layout.open && m_layout.overlay.Contains(input.mouseX, input.mouseY));
        if (!m_layout.open && m_layout.activePill.w > 0.0f && m_layout.activePill.Contains(input.mouseX, input.mouseY))
        {
            over = true;
        }
    }
    result.mouseOverUI = over;

    if (!over || !input.pressed)
    {
        return result;
    }
    result.consumedClick = true;

    if (m_hover < 0)
    {
        return result; // 何も無い所(幕の上)のクリックは、受け取るだけ
    }

    const Button button = m_layout.buttons[m_hover];
    bool changed = false;
    switch (button.kind)
    {
    case ButtonKind::Toggle:
        m_open = !m_open;
        changed = true;
        break;
    case ButtonKind::Tab:
        if (m_tab != button.tab)
        {
            m_tab = button.tab;
            changed = true;
        }
        break;
    case ButtonKind::Item:
    case ButtonKind::Cancel:
        result.command = button.command;
        if (button.closesMenu)
        {
            m_open = false;
            changed = true;
        }
        break;
    }

    // 開閉・タブが変わったら、今フレームの描画に合わせて配置を作り直す。
    if (changed)
    {
        m_layout = ComputeLayout(input, model);
        m_hover = usable ? FindButtonAt(input.mouseX, input.mouseY) : -1;
    }
    return result;
}

UIButtonState GameMenu::StateOf(const Button& button, int index) const
{
    if (!button.enabled) { return UIButtonState::Disabled; }
    if (button.selected) { return UIButtonState::Selected; }
    if (index == m_hover) { return m_down ? UIButtonState::Pressed : UIButtonState::Hover; }
    return UIButtonState::Normal;
}

void GameMenu::Draw(UIRenderer& ui, IUISkin& skin) const
{
    const Layout& L = m_layout;
    if (!L.enabled)
    {
        return;
    }
    const float s = L.scale;

    if (L.open)
    {
        skin.DrawPanel(ui, L.overlay, UIPanelStyle::Overlay, s);
        skin.DrawPanel(ui, L.list, UIPanelStyle::Window, s);
        skin.DrawPanel(ui, L.content, UIPanelStyle::Window, s);
    }
    else if (L.activePill.w > 0.0f)
    {
        skin.DrawPanel(ui, L.activePill, UIPanelStyle::Chip, s);
    }

    for (size_t i = 0; i < L.buttons.size(); ++i)
    {
        const Button& button = L.buttons[i];
        const UIButtonState state = StateOf(button, static_cast<int>(i));
        skin.DrawButton(ui, button.rect, state, s);

        const float iconSize = button.rect.h * 0.62f;
        const UITextStyle labelStyle = skin.GetTextStyle(state == UIButtonState::Disabled ? UITextRole::Disabled : UITextRole::Normal);
        const float labelY = button.rect.y + (button.rect.h - ui.GetLineHeight(L.labelSize)) * 0.5f;

        float iconCx = 0.0f;
        float labelX = 0.0f;
        if (button.centerLabel)
        {
            // アイコン+文字を、1つの塊として中央に。
            const float labelWidth = ui.MeasureString(button.label, L.labelSize);
            const float gapWidth = 8.0f * s;
            const float total = iconSize + gapWidth + labelWidth;
            const float startX = button.rect.x + (button.rect.w - total) * 0.5f;
            iconCx = startX + iconSize * 0.5f;
            labelX = startX + iconSize + gapWidth;
        }
        else
        {
            iconCx = button.rect.x + button.rect.h * 0.5f;
            labelX = button.rect.x + button.rect.h * 0.95f;
        }

        skin.DrawIcon(ui, button.icon, iconCx, button.rect.y + button.rect.h * 0.5f, iconSize, state);
        ui.DrawString(labelX, labelY, button.label, L.labelSize, labelStyle);

        if (!button.detail.empty())
        {
            const UITextStyle detailStyle = skin.GetTextStyle(button.affordable ? UITextRole::Sub : UITextRole::Warning);
            const float detailWidth = ui.MeasureString(button.detail, L.detailSize);
            const float detailY = button.rect.y + (button.rect.h - ui.GetLineHeight(L.detailSize)) * 0.5f;
            ui.DrawString(button.rect.Right() - 14.0f * s - detailWidth, detailY, button.detail, L.detailSize, detailStyle);
        }
    }

    for (const Text& text : L.texts)
    {
        const UITextStyle style = skin.GetTextStyle(text.role);
        float x = text.x;
        if (text.alignRight)
        {
            x -= ui.MeasureString(text.text, text.size);
        }
        ui.DrawString(x, text.y, text.text, text.size, style);
    }
}
