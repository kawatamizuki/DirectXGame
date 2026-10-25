#include "TitleMenu.h"
#include "UILayoutUtil.h"

namespace
{
    // ---- 基準の寸法(画面の高さ1080の時のピクセル数)。実際には倍率をかけて使う ----
    constexpr float kTitleFont = 84.0f;
    constexpr float kTitleTopRatio = 0.14f;     // タイトルの板の上端(画面の高さの割合)
    constexpr float kTitleBoxHeight = 150.0f;
    constexpr float kTitlePadding = 70.0f;      // タイトルの文字の左右の余白(板の幅=文字の幅+この2倍)
    constexpr float kButtonWidth = 380.0f;
    constexpr float kButtonHeight = 84.0f;
    constexpr float kButtonGap = 26.0f;
    constexpr float kButtonsTopRatio = 0.68f;   // 最初のボタンの上端(画面の高さの割合)。背景の街(画面の中ほど)に、重ならないように下げる
    constexpr float kLabelFont = 38.0f;
}

TitleMenu::Layout TitleMenu::ComputeLayout(const TitleInput& input) const
{
    Layout L;
    const float s = UIScaleForHeight(input.screenH);
    L.scale = s;
    L.screenW = input.screenW;
    L.screenH = input.screenH;
    L.title = m_title;
    L.titleCenterX = input.screenW * 0.5f;
    L.titleTop = input.screenH * kTitleTopRatio;
    L.titleSize = UIRoundFontSize(kTitleFont * s);
    L.titleBoxHeight = kTitleBoxHeight * s;
    L.labelSize = UIRoundFontSize(kLabelFont * s);

    // ボタンは、画面の横の中心に、縦に並べる。幅が足りない画面では、画面の幅に収める。
    float buttonWidth = kButtonWidth * s;
    const float maxWidth = input.screenW - 40.0f * s;
    if (buttonWidth > maxWidth) { buttonWidth = (maxWidth > 40.0f * s) ? maxWidth : 40.0f * s; }
    const float buttonHeight = kButtonHeight * s;
    const float gap = kButtonGap * s;
    float y = input.screenH * kButtonsTopRatio;
    // 画面が低くて、下にはみ出す時は、ボタンを上へ寄せる(タイトルの板に少し重なっても、押せる位置を優先する)。
    const float totalHeight = buttonHeight * 2.0f + gap;
    const float lowest = input.screenH - totalHeight - 8.0f * s;
    if (y > lowest) { y = (lowest > 0.0f) ? lowest : 0.0f; }

    const struct { const char* label; TitleCommand command; } items[] =
    {
        { "スタート", TitleCommand::Start },
        { "終了",     TitleCommand::Quit }
    };
    for (const auto& item : items)
    {
        Button button;
        button.rect = UIRect{ (input.screenW - buttonWidth) * 0.5f, y, buttonWidth, buttonHeight };
        button.label = item.label;
        button.command = item.command;
        L.buttons.push_back(button);
        y += buttonHeight + gap;
    }
    return L;
}

TitleResult TitleMenu::Update(const TitleInput& input)
{
    TitleResult result;
    m_layout = ComputeLayout(input);
    m_hover = -1;
    m_down = false;

    // ImGuiのパネルの上では反応しない(ImGuiが手前に描かれるため)。
    if (input.blockedByOtherUI)
    {
        return result;
    }

    for (size_t i = 0; i < m_layout.buttons.size(); ++i)
    {
        if (m_layout.buttons[i].rect.Contains(input.mouseX, input.mouseY))
        {
            m_hover = static_cast<int>(i);
            break;
        }
    }
    m_down = input.down;
    result.mouseOverButton = (m_hover >= 0);

    if (m_hover >= 0 && input.pressed)
    {
        result.command = m_layout.buttons[m_hover].command;
    }
    return result;
}

void TitleMenu::Draw(UIRenderer& ui, IUISkin& skin) const
{
    const Layout& L = m_layout;
    if (L.buttons.empty())
    {
        return; // Updateがまだ呼ばれていない
    }
    const float s = L.scale;

    // ---- タイトル: 板(箱)の上に、縁取りの大きな文字 ----
    {
        const float textWidth = ui.MeasureString(L.title, L.titleSize);
        const float boxWidth = textWidth + kTitlePadding * s * 2.0f;
        const UIRect box{ L.titleCenterX - boxWidth * 0.5f, L.titleTop, boxWidth, L.titleBoxHeight };
        skin.DrawPanel(ui, box, UIPanelStyle::Window, s);

        const float textY = box.y + (box.h - ui.GetLineHeight(L.titleSize)) * 0.5f;
        ui.DrawString(L.titleCenterX - textWidth * 0.5f, textY, L.title, L.titleSize, skin.GetTextStyle(UITextRole::Outlined));
    }

    // ---- ボタン ----
    for (size_t i = 0; i < L.buttons.size(); ++i)
    {
        const Button& button = L.buttons[i];
        UIButtonState state = UIButtonState::Normal;
        if (static_cast<int>(i) == m_hover)
        {
            state = m_down ? UIButtonState::Pressed : UIButtonState::Hover;
        }
        skin.DrawButton(ui, button.rect, state, s);

        const float labelWidth = ui.MeasureString(button.label, L.labelSize);
        ui.DrawString(button.rect.x + (button.rect.w - labelWidth) * 0.5f,
                      button.rect.y + (button.rect.h - ui.GetLineHeight(L.labelSize)) * 0.5f,
                      button.label, L.labelSize, skin.GetTextStyle(UITextRole::Normal));
    }
}
