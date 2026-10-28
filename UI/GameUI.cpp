#include "GameUI.h"
#include <cmath>
#include <cstdio>
#include "DayNightCycle.h"
#include "UILayoutUtil.h"

namespace
{
    // ---- 基準の寸法(画面の高さ1080の時のピクセル数)。実際には倍率をかけて使う ----
    constexpr float kBarHeight = 60.0f;
    constexpr float kMargin = 12.0f;
    constexpr float kIconRadius = 18.0f;
    constexpr float kTimeFontSize = 24.0f;
    constexpr float kDayFontSize = 13.0f;
    constexpr float kTimelineWidth = 150.0f;
    constexpr float kTimelineHeight = 8.0f;
    constexpr float kCashBoxHeight = 40.0f;
    constexpr float kCashFontSize = 24.0f;      // 他の指標(kChipFontSize)より大きい文字
    constexpr float kCashPadding = 14.0f;
    constexpr float kCoinRadius = 14.0f;
    constexpr float kCashIconGap = 8.0f;
    constexpr float kTagFontSize = 17.0f;       // カーソルの横の建設費タグ
    constexpr float kTagPadding = 8.0f;
    constexpr float kTagOffsetX = 20.0f;        // カーソルからのずらし(カーソルに隠れないように右下へ)
    constexpr float kTagOffsetY = 24.0f;
    constexpr float kChipHeight = 34.0f;
    constexpr float kChipFontSize = 16.0f;
    constexpr float kChipPadding = 10.0f;
    constexpr float kChipIconSize = 18.0f;
    constexpr float kChipIconGap = 6.0f;
    constexpr float kChipGap = 8.0f;
    constexpr float kBlockGap = 24.0f;   // 左のブロック・中央の所持金・右のチップの間に最低限空ける幅


    // 1234567 → "1,234,567"
    std::string FormatWithCommas(int value)
    {
        std::string digits = std::to_string(value < 0 ? -value : value);
        std::string result;
        int count = 0;
        for (size_t i = digits.size(); i-- > 0;)
        {
            result.insert(result.begin(), digits[i]);
            if (++count % 3 == 0 && i > 0)
            {
                result.insert(result.begin(), ',');
            }
        }
        if (value < 0)
        {
            result.insert(result.begin(), '-');
        }
        return result;
    }

    UIColor Color(float r, float g, float b, float a = 1.0f)
    {
        return UIColor(r, g, b, a);
    }
}

void GameUI::Initialize(UIRenderer* uiRenderer, const DayNightCycle* dayNightCycle)
{
    m_ui = uiRenderer;
    m_cycle = dayNightCycle;
}

void GameUI::ComputeLayout(float screenW, float screenH, const HudState& state)
{
    Layout layout;
    float s = UIScaleForHeight(screenH);
    layout.scale = s;
    layout.screenW = screenW;
    layout.screenH = screenH;

    layout.bar = UIRect{ 0.0f, 0.0f, screenW, kBarHeight * s };

    // ---- 左のブロック: 昼夜アイコン、時刻、日数、1日のタイムライン ----
    layout.iconRadius = kIconRadius * s;
    layout.iconCenterX = kMargin * s + layout.iconRadius;
    layout.iconCenterY = layout.bar.h * 0.5f;

    float textX = kMargin * s + layout.iconRadius * 2.0f + 10.0f * s;

    int hour = static_cast<int>(std::floor(state.timeOfDayHours));
    int minute = static_cast<int>((state.timeOfDayHours - std::floor(state.timeOfDayHours)) * 60.0f);
    if (hour < 0) { hour = 0; }
    if (hour > 23) { hour = 23; }
    if (minute < 0) { minute = 0; }
    if (minute > 59) { minute = 59; }
    char timeBuffer[16];
    std::snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d", hour, minute);
    layout.timeText = timeBuffer;

    layout.timeTextSize = UIRoundFontSize(kTimeFontSize * s);
    layout.timeTextX = textX;
    layout.timeTextY = 5.0f * s;
    float timeWidth = m_ui->MeasureString(layout.timeText, layout.timeTextSize);

    // 日数は時刻のすぐ右に、ベースラインを揃えて小さく置く。
    layout.dayText = state.calendarText.empty() ? ("Day " + std::to_string(state.dayCount)) : state.calendarText;
    layout.dayTextSize = UIRoundFontSize(kDayFontSize * s);
    layout.dayTextX = textX + timeWidth + 8.0f * s;
    float timeBaseline = layout.timeTextY + m_ui->GetAscent(layout.timeTextSize);
    layout.dayTextY = timeBaseline - m_ui->GetAscent(layout.dayTextSize);
    float dayWidth = m_ui->MeasureString(layout.dayText, layout.dayTextSize);

    layout.timeline = UIRect{ textX, 41.0f * s, kTimelineWidth * s, kTimelineHeight * s };

    float textBlockRight = layout.dayTextX + dayWidth;
    float timelineRight = layout.timeline.Right();
    layout.leftBlockRight = (textBlockRight > timelineRight) ? textBlockRight : timelineRight;

    // ---- 右のチップ: 人口・雇用・住民。右端から左へ並べる ----
    struct ChipSource
    {
        UIIcon icon;
        std::string text;
    };
    const ChipSource sources[3] =
    {
        { UIIcon::Population, std::string("人口 ") + FormatWithCommas(state.stats.populationTotal) },
        { UIIcon::Shop, std::string("施設 ") + FormatWithCommas(state.stats.facilityCount) }, // 以前は「雇用」(仕事の概念は停止中)
        { UIIcon::Residents, std::string("住民 ") + FormatWithCommas(state.stats.agentCount) },
    };

    float chipFontSize = UIRoundFontSize(kChipFontSize * s);
    float chipHeight = kChipHeight * s;
    float chipY = (layout.bar.h - chipHeight) * 0.5f;

    // ---- 中央の所持金: コインのアイコン+「1,000円」。他の指標より大きな文字 ----
    layout.cashText = FormatWithCommas(state.cash) + "円";
    layout.cashTextSize = UIRoundFontSize(kCashFontSize * s);
    float cashTextWidth = m_ui->MeasureString(layout.cashText, layout.cashTextSize);
    layout.coinRadius = kCoinRadius * s;
    float cashWidth = (kCashPadding * 2.0f + kCashIconGap) * s + layout.coinRadius * 2.0f + cashTextWidth;
    float cashHeight = kCashBoxHeight * s;

    // 画面の真ん中に置く。左のブロックと重なる場合だけ右へずらす。
    float cashX = screenW * 0.5f - cashWidth * 0.5f;
    float minCashX = layout.leftBlockRight + kBlockGap * s;
    if (cashX < minCashX)
    {
        cashX = minCashX;
    }
    layout.cashBox = UIRect{ cashX, (layout.bar.h - cashHeight) * 0.5f, cashWidth, cashHeight };
    layout.coinCenterX = cashX + kCashPadding * s + layout.coinRadius;
    layout.coinCenterY = layout.cashBox.y + cashHeight * 0.5f;
    layout.cashTextX = layout.coinCenterX + layout.coinRadius + kCashIconGap * s;
    layout.cashTextY = layout.cashBox.y + (cashHeight - m_ui->GetLineHeight(layout.cashTextSize)) * 0.5f;
    float cashRight = layout.cashBox.Right();

    // チップは、所持金との間に最低限の隙間が空く範囲に収まる数だけ、右端から並べる。
    float cursorRight = screenW - kMargin * s;
    float limitLeft = cashRight + kBlockGap * s;
    layout.chipCount = 0;
    Chip placed[3];
    for (int i = 2; i >= 0; --i)
    {
        float textWidth = m_ui->MeasureString(sources[i].text, chipFontSize);
        float width = (kChipPadding * 2.0f + kChipIconSize + kChipIconGap) * s + textWidth;
        float left = cursorRight - width;
        if (left < limitLeft)
        {
            break; // これ以上は入らない(左のチップから順に省略される)
        }
        placed[layout.chipCount].rect = UIRect{ left, chipY, width, chipHeight };
        placed[layout.chipCount].text = sources[i].text;
        placed[layout.chipCount].icon = sources[i].icon;
        layout.chipCount++;
        cursorRight = left - kChipGap * s;
    }
    // 右から並べたので、左→右の順に入れ替えて保存する。
    for (int i = 0; i < layout.chipCount; ++i)
    {
        layout.chips[i] = placed[layout.chipCount - 1 - i];
    }

    m_layout = layout;
}


HudResult GameUI::Update(const HudInput& input, const HudState& state)
{
    HudResult result;
    if (!m_ui)
    {
        return result;
    }

    ComputeLayout(input.screenW, input.screenH, state);

    // ゲーム内メニュー。開いている間は、画面全体がメニュー(後ろのゲームへ何も届かない)。
    MenuInput menuInput;
    menuInput.screenW = input.screenW;
    menuInput.screenH = input.screenH;
    menuInput.topInset = m_layout.bar.Bottom();
    menuInput.mouseX = input.mouseX;
    menuInput.mouseY = input.mouseY;
    menuInput.pressed = input.pressed;
    menuInput.down = input.down;
    menuInput.enabled = input.menuEnabled;
    // 「住民の情報」パネルが開いている間は、パネルが画面全体を受け持つ(メニューのボタンは反応しない)。
    ResidentPanelInput panelInput;
    panelInput.screenW = input.screenW;
    panelInput.screenH = input.screenH;
    panelInput.topInset = m_layout.bar.Bottom();
    panelInput.mouseX = input.mouseX;
    panelInput.mouseY = input.mouseY;
    panelInput.pressed = input.pressed;
    panelInput.down = input.down;
    panelInput.blockedByOtherUI = input.blockedByOtherUI;
    const ResidentPanelResult panelResult = m_residentPanel.Update(panelInput, state.residentInfo);
    const bool panelWasOpen = m_residentPanel.IsOpen() || panelResult.closed;

    menuInput.blockedByOtherUI = input.blockedByOtherUI || panelWasOpen;
    menuInput.typedDigits = input.typedDigits;
    menuInput.keyBackspace = input.keyBackspace;
    menuInput.keyEnter = input.keyEnter;
    menuInput.keyEscape = input.keyEscape;
    MenuResult menuResult = m_menu.Update(menuInput, state.menu);
    result.menuCommand = menuResult.command;
    result.menuTabChanged = menuResult.tabChanged;
    result.menuOpen = m_menu.IsOpen();

    bool overBar = !input.blockedByOtherUI && m_layout.bar.Contains(input.mouseX, input.mouseY);
    result.residentPanelOpen = m_residentPanel.IsOpen();
    result.mouseOverUI = overBar || menuResult.mouseOverUI || panelResult.mouseOverUI;

    // バーの上で押したクリックはHUDが受け取る(ワールドへ抜けない)。バーには、押せるものは無い。
    if ((overBar && input.pressed) || menuResult.consumedClick || panelResult.consumedClick)
    {
        result.consumedClick = true;
    }

    return result;
}

GameUI::CostTagLayout GameUI::ComputeCostTagLayout(const HudState& state) const
{
    CostTagLayout tag;
    if (!m_ui || !state.costTag.visible)
    {
        return tag;
    }

    float s = m_layout.scale;
    tag.visible = true;
    tag.fontSize = UIRoundFontSize(kTagFontSize * s);

    // 道路はマス数も一緒に出す(「6マス  -120円」)。建物は金額だけ(「-100円」)。0円は符号を付けない。
    std::string amount = (state.costTag.cost > 0)
        ? "-" + FormatWithCommas(state.costTag.cost) + "円"
        : std::string("0円");
    tag.costText = (state.costTag.cells > 0)
        ? std::to_string(state.costTag.cells) + "マス  " + amount
        : amount;
    if (!state.costTag.affordable)
    {
        tag.warningText = "お金が足りません";
    }

    float pad = kTagPadding * s;
    float lineHeight = m_ui->GetLineHeight(tag.fontSize);
    float textWidth = m_ui->MeasureString(tag.costText, tag.fontSize);
    float warningWidth = tag.warningText.empty() ? 0.0f : m_ui->MeasureString(tag.warningText, tag.fontSize);
    float width = ((textWidth > warningWidth) ? textWidth : warningWidth) + pad * 2.0f;
    float height = lineHeight * (tag.warningText.empty() ? 1.0f : 2.0f) + pad * 2.0f;

    // 既定はカーソルの右下。画面の右端・下端にはみ出す時は、反対側(左・上)へ回す。
    float x = state.cursorX + kTagOffsetX * s;
    float y = state.cursorY + kTagOffsetY * s;
    if (x + width > m_layout.screenW)
    {
        x = state.cursorX - kTagOffsetX * s - width;
    }
    if (y + height > m_layout.screenH)
    {
        y = state.cursorY - kTagOffsetY * s - height;
    }
    // それでも画面の外(極端に狭い画面)なら、画面の中に押し込む。
    if (x + width > m_layout.screenW) { x = m_layout.screenW - width; }
    if (y + height > m_layout.screenH) { y = m_layout.screenH - height; }
    if (x < 0.0f) { x = 0.0f; }
    if (y < 0.0f) { y = 0.0f; }

    tag.rect = UIRect{ x, y, width, height };
    tag.textX = x + pad;
    tag.costTextY = y + pad;
    tag.warningTextY = tag.costTextY + lineHeight;
    return tag;
}

void GameUI::DrawCostTag(const HudState& state)
{
    CostTagLayout tag = ComputeCostTagLayout(state);
    if (!tag.visible)
    {
        return;
    }

    bool affordable = tag.warningText.empty();

    // 払えない時は、箱の枠と文字を赤にして目立たせる(赤い枠は、スキンのTagWarning)。
    m_skin->DrawPanel(*m_ui, tag.rect, affordable ? UIPanelStyle::Tag : UIPanelStyle::TagWarning, m_layout.scale);

    m_ui->DrawString(tag.textX, tag.costTextY, tag.costText, tag.fontSize,
        m_skin->GetTextStyle(affordable ? UITextRole::Normal : UITextRole::Warning));

    if (!affordable)
    {
        m_ui->DrawString(tag.textX, tag.warningTextY, tag.warningText, tag.fontSize, m_skin->GetTextStyle(UITextRole::Warning));
    }
}

void GameUI::Draw(const HudState& state)
{
    if (!m_ui)
    {
        return;
    }

    const Layout& L = m_layout;
    float s = L.scale;
    IUISkin& skin = *m_skin;

    // ---- バー本体 ----
    skin.DrawPanel(*m_ui, L.bar, UIPanelStyle::Bar, s);

    // ---- 左: 昼夜アイコン、時刻、日数、タイムライン ----
    bool daytime = m_cycle ? m_cycle->IsDaytime(state.timeOfDayHours) : true;
    skin.DrawIcon(*m_ui, daytime ? UIIcon::Sun : UIIcon::Moon, L.iconCenterX, L.iconCenterY, L.iconRadius * 2.0f, UIButtonState::Normal);

    m_ui->DrawString(L.timeTextX, L.timeTextY, L.timeText, L.timeTextSize, skin.GetTextStyle(UITextRole::Outlined));
    m_ui->DrawString(L.dayTextX, L.dayTextY, L.dayText, L.dayTextSize, skin.GetTextStyle(UITextRole::OutlinedSub));

    // タイムライン: 24時間ぶんの空の色を並べる。昼夜のどのあたりかが一目で分かる。
    // 空の色は、昼夜サイクルのデータから決まる色なので、スキンではなくここで塗る(外枠と目印は、スキンが描く)。
    {
        const UIRect& tl = L.timeline;
        float segmentWidth = tl.w / 24.0f;
        for (int i = 0; i < 24; ++i)
        {
            UIColor sky = Color(0.5f, 0.7f, 0.9f);
            if (m_cycle)
            {
                LightingState lighting = m_cycle->Evaluate(static_cast<float>(i) + 0.5f);
                sky = Color(lighting.skyColor.x, lighting.skyColor.y, lighting.skyColor.z);
            }
            // 継ぎ目に隙間ができないよう、少しだけ重ねて描く。
            m_ui->FillRect(UIRect{ tl.x + segmentWidth * static_cast<float>(i), tl.y, segmentWidth + 0.75f, tl.h }, sky);
        }
        skin.DrawPanel(*m_ui, UIRect{ tl.x - 1.0f, tl.y - 1.0f, tl.w + 2.0f, tl.h + 2.0f }, UIPanelStyle::Frame, s);

        float markerX = tl.x + tl.w * (state.timeOfDayHours / 24.0f);
        skin.DrawIcon(*m_ui, UIIcon::TimeMarker, markerX, tl.y + tl.h * 0.5f, tl.h, UIButtonState::Normal);
    }

    // ---- 中央: 所持金(コインのアイコン+金額。他の指標より大きな文字) ----
    {
        skin.DrawPanel(*m_ui, L.cashBox, UIPanelStyle::Chip, s);
        skin.DrawIcon(*m_ui, UIIcon::Coin, L.coinCenterX, L.coinCenterY, L.coinRadius * 2.0f, UIButtonState::Normal);
        m_ui->DrawString(L.cashTextX, L.cashTextY, L.cashText, L.cashTextSize, skin.GetTextStyle(UITextRole::Normal));
    }

    // ---- 右: 指標のチップ ----
    UITextStyle chipStyle = skin.GetTextStyle(UITextRole::Normal);
    float chipFontSize = UIRoundFontSize(kChipFontSize * s);
    for (int i = 0; i < L.chipCount; ++i)
    {
        const Chip& chip = L.chips[i];
        skin.DrawPanel(*m_ui, chip.rect, UIPanelStyle::Chip, s);

        float iconSize = kChipIconSize * s;
        float iconCx = chip.rect.x + kChipPadding * s + iconSize * 0.5f;
        float iconCy = chip.rect.y + chip.rect.h * 0.5f;
        skin.DrawIcon(*m_ui, chip.icon, iconCx, iconCy, iconSize, UIButtonState::Normal);

        float textX = chip.rect.x + (kChipPadding + kChipIconSize + kChipIconGap) * s;
        float textY = chip.rect.y + (chip.rect.h - m_ui->GetLineHeight(chipFontSize)) * 0.5f;
        m_ui->DrawString(textX, textY, chip.text, chipFontSize, chipStyle);
    }

    // 世界の建物の警告(「道路がない！」)。メニュー・パネルより下(開いた時の幕で、暗くなる)。数が多い時は、手前の分だけ。
    {
        constexpr size_t kMaxWorldLabels = 60;
        const float labelFont = UIRoundFontSize(kChipFontSize * s);
        const float padX = 8.0f * s;
        const float padY = 4.0f * s;
        size_t drawn = 0;
        for (const HudWorldLabel& label : state.worldLabels)
        {
            if (++drawn > kMaxWorldLabels) { break; }
            const float width = m_ui->MeasureString(label.text, labelFont) + padX * 2.0f;
            const float height = m_ui->GetLineHeight(labelFont) + padY * 2.0f;
            const UIRect rect{ label.x - width * 0.5f, label.y - height, width, height };
            if (rect.y < L.bar.Bottom() || rect.Right() < 0.0f || rect.x > L.screenW || rect.Bottom() > L.screenH) { continue; } // 上部バーの下・画面の中だけ
            skin.DrawPanel(*m_ui, rect, UIPanelStyle::TagWarning, s);
            m_ui->DrawString(rect.x + padX, rect.y + padY, label.text, labelFont, skin.GetTextStyle(UITextRole::Warning));
        }
    }

    // ゲーム内メニュー(右下のボタン、開いた時の一覧)。バーより手前に描く(開いた時の幕は、バーの下から始まる)。
    m_menu.Draw(*m_ui, skin);

    // 「住民の情報」パネル(開いている時だけ。メニューより手前)。
    m_residentPanel.Draw(*m_ui, skin, state.residentInfo);

    // 操作のヒント(左下の小さな箱)。メニュー・パネルが開いている間は出さない。
    if (!state.hintText.empty() && !m_menu.IsOpen() && !m_residentPanel.IsOpen())
    {
        const float hintFont = UIRoundFontSize(kChipFontSize * s);
        const float padX = 16.0f * s;
        const float hintHeight = 40.0f * s;
        const float hintWidth = m_ui->MeasureString(state.hintText, hintFont) + padX * 2.0f;
        const UIRect hintRect{ 12.0f * s, L.screenH - 12.0f * s - hintHeight, hintWidth, hintHeight };
        skin.DrawPanel(*m_ui, hintRect, UIPanelStyle::Chip, s);
        m_ui->DrawString(hintRect.x + padX, hintRect.y + (hintRect.h - m_ui->GetLineHeight(hintFont)) * 0.5f, state.hintText, hintFont, skin.GetTextStyle(UITextRole::Normal));
    }

    // カーソルの横の建設費タグは、HUDの上にも重なりうるので最後(最前面)に描く。
    DrawCostTag(state);
}
