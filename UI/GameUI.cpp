#include "GameUI.h"
#include <cmath>
#include <cstdio>
#include "DayNightCycle.h"

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

    // 画面の高さに応じた倍率。1080を基準に、小さい画面でも読める0.75〜大きい画面での2.0に収める。
    float ScaleForHeight(float screenH)
    {
        float scale = screenH / 1080.0f;
        if (scale < 0.75f) { scale = 0.75f; }
        if (scale > 2.0f) { scale = 2.0f; }
        return scale;
    }

    // 文字サイズは整数ピクセルに丸める(FontAtlasが整数サイズで焼くため、描画と測定で同じ大きさになるように)。
    float RoundSize(float size)
    {
        float rounded = std::floor(size + 0.5f);
        return (rounded < 1.0f) ? 1.0f : rounded;
    }

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
    float s = ScaleForHeight(screenH);
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

    layout.timeTextSize = RoundSize(kTimeFontSize * s);
    layout.timeTextX = textX;
    layout.timeTextY = 5.0f * s;
    float timeWidth = m_ui->MeasureString(layout.timeText, layout.timeTextSize);

    // 日数は時刻のすぐ右に、ベースラインを揃えて小さく置く。
    layout.dayText = "Day " + std::to_string(state.dayCount);
    layout.dayTextSize = RoundSize(kDayFontSize * s);
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
        ChipIcon icon;
        std::string text;
    };
    const ChipSource sources[3] =
    {
        { ChipIcon::Population, std::string("人口 ") + FormatWithCommas(state.stats.populationTotal) },
        { ChipIcon::Jobs, std::string("雇用 ") + FormatWithCommas(state.stats.jobsFilled) + "/" + FormatWithCommas(state.stats.jobCapacityTotal) },
        { ChipIcon::Residents, std::string("住民 ") + FormatWithCommas(state.stats.agentCount) },
    };

    float chipFontSize = RoundSize(kChipFontSize * s);
    float chipHeight = kChipHeight * s;
    float chipY = (layout.bar.h - chipHeight) * 0.5f;

    // ---- 中央の所持金: コインのアイコン+「1,000円」。他の指標より大きな文字 ----
    layout.cashText = FormatWithCommas(state.cash) + "円";
    layout.cashTextSize = RoundSize(kCashFontSize * s);
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

    bool over = !input.blockedByOtherUI && m_layout.bar.Contains(input.mouseX, input.mouseY);
    result.mouseOverUI = over;

    // バーの上で押したクリックはHUDが受け取る(ワールドへ抜けない)。
    // 今のバーにはボタンが無いので、押せるものは無い(将来メニューなどのボタンを足す時は、ここで当たり判定をする)。
    if (over && input.pressed)
    {
        result.consumedClick = true;
    }

    return result;
}

void GameUI::DrawSunIcon(float cx, float cy, float radius)
{
    UIColor ray = Color(1.00f, 0.62f, 0.10f);
    float rayInner = radius * 0.72f;
    float rayOuter = radius * 1.00f;
    float rayThickness = radius * 0.14f;
    constexpr float kTwoPi = 6.28318530717959f;
    for (int i = 0; i < 8; ++i)
    {
        float angle = kTwoPi * static_cast<float>(i) / 8.0f;
        float dx = std::cos(angle);
        float dy = std::sin(angle);
        m_ui->DrawLine(cx + dx * rayInner, cy + dy * rayInner, cx + dx * rayOuter, cy + dy * rayOuter, rayThickness, ray);
    }
    m_ui->FillCircle(cx, cy, radius * 0.58f, Color(0.90f, 0.52f, 0.08f));
    m_ui->FillCircle(cx, cy, radius * 0.48f, Color(1.00f, 0.86f, 0.25f));
}

void GameUI::DrawMoonIcon(float cx, float cy, float radius)
{
    m_ui->FillCircle(cx, cy, radius * 0.80f, Color(0.45f, 0.48f, 0.72f));
    m_ui->FillCircle(cx, cy, radius * 0.70f, Color(0.96f, 0.95f, 0.82f));
    // クレーター
    UIColor crater = Color(0.84f, 0.83f, 0.68f);
    m_ui->FillCircle(cx - radius * 0.22f, cy - radius * 0.18f, radius * 0.16f, crater);
    m_ui->FillCircle(cx + radius * 0.20f, cy + radius * 0.10f, radius * 0.20f, crater);
    m_ui->FillCircle(cx - radius * 0.05f, cy + radius * 0.34f, radius * 0.10f, crater);
}

void GameUI::DrawCoinIcon(float cx, float cy, float radius)
{
    // 金貨: 濃い金色の縁、明るい金色の面、左上のハイライト、中央の「¥」。
    m_ui->FillCircle(cx, cy, radius, Color(0.62f, 0.38f, 0.06f));
    m_ui->FillCircle(cx, cy, radius * 0.88f, Color(0.98f, 0.76f, 0.16f));
    m_ui->FillCircle(cx, cy, radius * 0.68f, Color(1.00f, 0.86f, 0.38f));
    m_ui->FillCircle(cx - radius * 0.36f, cy - radius * 0.40f, radius * 0.16f, Color(1.00f, 0.97f, 0.80f));

    float markSize = std::floor(radius * 1.25f + 0.5f);
    if (markSize < 1.0f) { markSize = 1.0f; }
    UITextStyle mark;
    mark.color = Color(0.55f, 0.32f, 0.04f);
    float markWidth = m_ui->MeasureString("\xC2\xA5", markSize); // 「¥」(U+00A5)
    m_ui->DrawString(cx - markWidth * 0.5f, cy - m_ui->GetLineHeight(markSize) * 0.5f, "\xC2\xA5", markSize, mark);
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
    tag.fontSize = RoundSize(kTagFontSize * s);

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

    float s = m_layout.scale;
    const UITheme& T = m_theme;
    bool affordable = tag.warningText.empty();

    // 払えない時は、箱の枠と文字を赤にして目立たせる。
    UIColor warning = Color(0.82f, 0.12f, 0.10f);
    UIColor border = affordable ? T.chipBorder : warning;

    m_ui->FillRoundedRect(UIRect{ tag.rect.x + 1.5f * s, tag.rect.y + 2.5f * s, tag.rect.w, tag.rect.h }, T.chipRadius * s * 0.6f, T.panelShadow, T.panelShadow);
    m_ui->FillRoundedRect(tag.rect, T.chipRadius * s * 0.6f, T.chipFill, T.chipFill);
    m_ui->StrokeRoundedRect(tag.rect, T.chipRadius * s * 0.6f, T.chipBorderThickness * s, border);

    UITextStyle costStyle;
    costStyle.color = affordable ? T.textColor : warning;
    m_ui->DrawString(tag.textX, tag.costTextY, tag.costText, tag.fontSize, costStyle);

    if (!affordable)
    {
        UITextStyle warningStyle;
        warningStyle.color = warning;
        m_ui->DrawString(tag.textX, tag.warningTextY, tag.warningText, tag.fontSize, warningStyle);
    }
}

void GameUI::DrawChipIcon(ChipIcon icon, float cx, float cy, float size)
{
    float half = size * 0.5f;
    UIColor body = m_theme.iconColor;

    switch (icon)
    {
    case ChipIcon::Population:
    {
        // 家: 屋根(三角)と壁(四角)とドア
        m_ui->FillTriangle(cx - half, cy - half * 0.05f, cx + half, cy - half * 0.05f, cx, cy - half, Color(0.85f, 0.35f, 0.25f));
        m_ui->FillRect(UIRect{ cx - half * 0.72f, cy - half * 0.05f, half * 1.44f, half * 1.05f }, Color(1.00f, 0.92f, 0.70f));
        m_ui->StrokeRoundedRect(UIRect{ cx - half * 0.72f, cy - half * 0.05f, half * 1.44f, half * 1.05f }, 0.0f, 1.2f, body);
        m_ui->FillRect(UIRect{ cx - half * 0.18f, cy + half * 0.30f, half * 0.36f, half * 0.70f }, body);
        break;
    }
    case ChipIcon::Jobs:
    {
        // ブリーフケース: 取っ手と本体
        UIColor caseColor = Color(0.62f, 0.40f, 0.22f);
        m_ui->StrokeRoundedRect(UIRect{ cx - half * 0.32f, cy - half * 0.92f, half * 0.64f, half * 0.60f }, 2.0f, 1.6f, body);
        m_ui->FillRoundedRect(UIRect{ cx - half, cy - half * 0.40f, size, half * 1.45f }, 2.5f, caseColor, caseColor);
        m_ui->FillRect(UIRect{ cx - half, cy + half * 0.12f, size, half * 0.14f }, Color(0.40f, 0.24f, 0.12f));
        m_ui->FillRect(UIRect{ cx - half * 0.14f, cy + half * 0.02f, half * 0.28f, half * 0.34f }, Color(1.00f, 0.85f, 0.30f));
        break;
    }
    case ChipIcon::Residents:
    {
        // 人: 頭(円)と体(角丸)
        UIColor skin = Color(1.00f, 0.82f, 0.62f);
        UIColor shirt = Color(0.30f, 0.55f, 0.85f);
        m_ui->FillRoundedRect(UIRect{ cx - half * 0.62f, cy + half * 0.02f, half * 1.24f, half * 0.98f }, half * 0.5f, shirt, shirt);
        m_ui->FillCircle(cx, cy - half * 0.42f, half * 0.46f, body);
        m_ui->FillCircle(cx, cy - half * 0.42f, half * 0.38f, skin);
        break;
    }
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
    const UITheme& T = m_theme;

    // ---- バー本体: 下に影、クリーム色のグラデーション、下端に茶色の太い線 ----
    UIColor shadowTop = T.panelShadow;
    UIColor shadowBottom = T.panelShadow;
    shadowBottom.w = 0.0f;
    m_ui->FillRoundedRect(UIRect{ 0.0f, L.bar.Bottom(), L.bar.w, 7.0f * s }, 0.0f, shadowTop, shadowBottom);

    m_ui->FillRoundedRect(L.bar, 0.0f, T.panelTop, T.panelBottom);
    m_ui->FillRect(UIRect{ 0.0f, L.bar.Bottom() - T.panelBorderThickness * s, L.bar.w, T.panelBorderThickness * s }, T.panelBorder);

    // ---- 左: 昼夜アイコン、時刻、日数、タイムライン ----
    bool daytime = m_cycle ? m_cycle->IsDaytime(state.timeOfDayHours) : true;
    if (daytime)
    {
        DrawSunIcon(L.iconCenterX, L.iconCenterY, L.iconRadius);
    }
    else
    {
        DrawMoonIcon(L.iconCenterX, L.iconCenterY, L.iconRadius);
    }

    UITextStyle timeStyle;
    timeStyle.color = T.textColor;
    timeStyle.outline = true;
    timeStyle.outlineColor = T.textOutline;
    m_ui->DrawString(L.timeTextX, L.timeTextY, L.timeText, L.timeTextSize, timeStyle);

    UITextStyle dayStyle;
    dayStyle.color = T.subTextColor;
    dayStyle.outline = true;
    dayStyle.outlineColor = T.textOutline;
    m_ui->DrawString(L.dayTextX, L.dayTextY, L.dayText, L.dayTextSize, dayStyle);

    // タイムライン: 24時間ぶんの空の色を並べる。昼夜のどのあたりかが一目で分かる。
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
        m_ui->StrokeRoundedRect(UIRect{ tl.x - 1.0f, tl.y - 1.0f, tl.w + 2.0f, tl.h + 2.0f }, 3.0f * s, 2.0f * s, T.panelBorder);

        // 今の時刻の目印(白い線を濃い縁で挟む)。
        float markerX = tl.x + tl.w * (state.timeOfDayHours / 24.0f);
        m_ui->DrawLine(markerX, tl.y - 4.0f * s, markerX, tl.Bottom() + 4.0f * s, 4.5f * s, T.panelBorder);
        m_ui->DrawLine(markerX, tl.y - 3.0f * s, markerX, tl.Bottom() + 3.0f * s, 2.0f * s, Color(1.0f, 1.0f, 1.0f));
    }

    // ---- 中央: 所持金(コインのアイコン+金額。他の指標より大きな文字) ----
    {
        m_ui->FillRoundedRect(UIRect{ L.cashBox.x, L.cashBox.y + 2.0f * s, L.cashBox.w, L.cashBox.h }, T.chipRadius * s, T.panelShadow, T.panelShadow);
        m_ui->FillRoundedRect(L.cashBox, T.chipRadius * s, T.chipFill, T.chipFill);
        m_ui->StrokeRoundedRect(L.cashBox, T.chipRadius * s, T.chipBorderThickness * s, T.chipBorder);
        DrawCoinIcon(L.coinCenterX, L.coinCenterY, L.coinRadius);

        UITextStyle cashStyle;
        cashStyle.color = T.textColor;
        m_ui->DrawString(L.cashTextX, L.cashTextY, L.cashText, L.cashTextSize, cashStyle);
    }

    // ---- 右: 指標のチップ ----
    UITextStyle chipStyle;
    chipStyle.color = T.textColor;
    float chipFontSize = RoundSize(kChipFontSize * s);
    for (int i = 0; i < L.chipCount; ++i)
    {
        const Chip& chip = L.chips[i];
        m_ui->FillRoundedRect(chip.rect, T.chipRadius * s, T.chipFill, T.chipFill);
        m_ui->StrokeRoundedRect(chip.rect, T.chipRadius * s, T.chipBorderThickness * s, T.chipBorder);

        float iconSize = kChipIconSize * s;
        float iconCx = chip.rect.x + kChipPadding * s + iconSize * 0.5f;
        float iconCy = chip.rect.y + chip.rect.h * 0.5f;
        DrawChipIcon(chip.icon, iconCx, iconCy, iconSize);

        float textX = chip.rect.x + (kChipPadding + kChipIconSize + kChipIconGap) * s;
        float textY = chip.rect.y + (chip.rect.h - m_ui->GetLineHeight(chipFontSize)) * 0.5f;
        m_ui->DrawString(textX, textY, chip.text, chipFontSize, chipStyle);
    }

    // カーソルの横の建設費タグは、HUDの上にも重なりうるので最後(最前面)に描く。
    DrawCostTag(state);
}
