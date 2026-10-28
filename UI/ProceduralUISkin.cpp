#include "ProceduralUISkin.h"
#include <algorithm>
#include <cmath>

namespace
{
    UIColor Color(float r, float g, float b, float a = 1.0f)
    {
        return UIColor(r, g, b, a);
    }

    constexpr float kTwoPi = 6.28318530717959f;
}

// ======================================================================
// 箱・ボタン
// ======================================================================

void ProceduralUISkin::DrawPanel(UIRenderer& ui, const UIRect& rect, UIPanelStyle style, float scale)
{
    const UITheme& T = m_theme;

    switch (style)
    {
    case UIPanelStyle::Bar:
    {
        // 下に影、クリーム色のグラデーション、下端に茶色の太い線。
        UIColor shadowTop = T.panelShadow;
        UIColor shadowBottom = T.panelShadow;
        shadowBottom.w = 0.0f;
        ui.FillRoundedRect(UIRect{ rect.x, rect.Bottom(), rect.w, 7.0f * scale }, 0.0f, shadowTop, shadowBottom);
        ui.FillRoundedRect(rect, 0.0f, T.panelTop, T.panelBottom);
        ui.FillRect(UIRect{ rect.x, rect.Bottom() - T.panelBorderThickness * scale, rect.w, T.panelBorderThickness * scale }, T.panelBorder);
        break;
    }
    case UIPanelStyle::Window:
    {
        float radius = 14.0f * scale;
        ui.FillRoundedRect(UIRect{ rect.x + 2.0f * scale, rect.y + 4.0f * scale, rect.w, rect.h }, radius, T.panelShadow, T.panelShadow);
        ui.FillRoundedRect(rect, radius, T.panelTop, T.panelBottom);
        ui.StrokeRoundedRect(rect, radius, T.panelBorderThickness * scale, T.panelBorder);
        break;
    }
    case UIPanelStyle::Chip:
    {
        float radius = T.chipRadius * scale;
        ui.FillRoundedRect(UIRect{ rect.x, rect.y + 2.0f * scale, rect.w, rect.h }, radius, T.panelShadow, T.panelShadow);
        ui.FillRoundedRect(rect, radius, T.chipFill, T.chipFill);
        ui.StrokeRoundedRect(rect, radius, T.chipBorderThickness * scale, T.chipBorder);
        break;
    }
    case UIPanelStyle::Tag:
    case UIPanelStyle::TagWarning:
    {
        float radius = T.chipRadius * scale * 0.6f;
        UIColor border = (style == UIPanelStyle::TagWarning) ? GetTextStyle(UITextRole::Warning).color : T.chipBorder;
        ui.FillRoundedRect(UIRect{ rect.x + 1.5f * scale, rect.y + 2.5f * scale, rect.w, rect.h }, radius, T.panelShadow, T.panelShadow);
        ui.FillRoundedRect(rect, radius, T.chipFill, T.chipFill);
        ui.StrokeRoundedRect(rect, radius, T.chipBorderThickness * scale, border);
        break;
    }
    case UIPanelStyle::Frame:
    {
        ui.StrokeRoundedRect(rect, 3.0f * scale, 2.0f * scale, T.panelBorder);
        break;
    }
    case UIPanelStyle::Overlay:
    {
        ui.FillRect(rect, Color(0.12f, 0.06f, 0.02f, 0.38f));
        break;
    }
    }
}

void ProceduralUISkin::DrawButton(UIRenderer& ui, const UIRect& rect, UIButtonState state, float scale)
{
    const UITheme& T = m_theme;

    UIColor top = T.buttonNormalTop;
    UIColor bottom = T.buttonNormalBottom;
    switch (state)
    {
    case UIButtonState::Hover:    top = T.buttonHoverTop;    bottom = T.buttonHoverBottom;    break;
    case UIButtonState::Pressed:  top = T.buttonPressedTop;  bottom = T.buttonPressedBottom;  break;
    case UIButtonState::Selected: top = T.buttonSelectedTop; bottom = T.buttonSelectedBottom; break;
    case UIButtonState::Disabled:
        top = Color(0.88f, 0.85f, 0.80f);
        bottom = Color(0.80f, 0.77f, 0.72f);
        break;
    default: break;
    }

    float radius = T.buttonRadius * scale;
    if (state != UIButtonState::Pressed)
    {
        ui.FillRoundedRect(UIRect{ rect.x, rect.y + 2.0f * scale, rect.w, rect.h }, radius, T.panelShadow, T.panelShadow);
    }
    ui.FillRoundedRect(rect, radius, top, bottom);
    ui.StrokeRoundedRect(rect, radius, T.buttonBorderThickness * scale, T.buttonBorder);
}

void ProceduralUISkin::DrawSlider(UIRenderer& ui, const UIRect& track, float value01, UIButtonState state, float scale)
{
    const UITheme& T = m_theme;
    const float t = value01 < 0.0f ? 0.0f : (value01 > 1.0f ? 1.0f : value01);

    // つまみの動く範囲(両端は、track.h/2 だけ内側。GameMenuの値の計算と同じ決まり)。
    const float inset = track.h * 0.5f;
    const float startX = track.x + inset;
    const float endX = track.Right() - inset;
    const float knobX = startX + (endX - startX) * t;
    const float cy = track.y + track.h * 0.5f;

    // みぞ(うすい色)と、つまみまでの塗り(選択色)。
    const float grooveH = (std::max)(6.0f * scale, track.h * 0.28f);
    const UIRect groove{ startX - grooveH * 0.5f, cy - grooveH * 0.5f, (endX - startX) + grooveH, grooveH };
    const float radius = grooveH * 0.5f;
    ui.FillRoundedRect(groove, radius, T.chipFill, T.chipFill);
    ui.StrokeRoundedRect(groove, radius, T.chipBorderThickness * scale, T.chipBorder);
    if (knobX > startX)
    {
        const UIRect filled{ groove.x, groove.y, (knobX - groove.x) + grooveH * 0.5f, grooveH };
        ui.FillRoundedRect(filled, radius, T.buttonSelectedTop, T.buttonSelectedBottom);
        ui.StrokeRoundedRect(filled, radius, T.chipBorderThickness * scale, T.chipBorder);
    }

    // つまみ(円)。つかんでいる間は少し大きく、カーソルが上なら明るく。
    const bool grabbed = (state == UIButtonState::Pressed);
    const float knobRadius = track.h * (grabbed ? 0.46f : 0.40f);
    UIColor fill = (state == UIButtonState::Hover || grabbed) ? T.buttonHoverTop : T.buttonNormalTop;
    ui.FillCircle(knobX + 1.0f * scale, cy + 2.0f * scale, knobRadius, T.panelShadow);
    ui.FillCircle(knobX, cy, knobRadius, T.buttonBorder);
    ui.FillCircle(knobX, cy, knobRadius - T.buttonBorderThickness * scale, fill);
}

UITextStyle ProceduralUISkin::GetTextStyle(UITextRole role) const
{
    const UITheme& T = m_theme;
    UITextStyle style;
    switch (role)
    {
    case UITextRole::Normal:
    case UITextRole::Heading:
        style.color = T.textColor;
        break;
    case UITextRole::Sub:
        style.color = T.subTextColor;
        break;
    case UITextRole::Warning:
        style.color = Color(0.82f, 0.12f, 0.10f);
        break;
    case UITextRole::Disabled:
        style.color = Color(0.60f, 0.52f, 0.42f);
        break;
    case UITextRole::Outlined:
        style.color = T.textColor;
        style.outline = true;
        style.outlineColor = T.textOutline;
        break;
    case UITextRole::OutlinedSub:
        style.color = T.subTextColor;
        style.outline = true;
        style.outlineColor = T.textOutline;
        break;
    }
    return style;
}

// ======================================================================
// アイコン
// ======================================================================

UIColor ProceduralUISkin::MonoColor(UIButtonState state) const
{
    if (state == UIButtonState::Selected) { return m_theme.iconSelectedColor; }
    if (state == UIButtonState::Disabled) { return Color(0.62f, 0.56f, 0.48f); }
    return m_theme.iconColor;
}

void ProceduralUISkin::DrawIcon(UIRenderer& ui, UIIcon icon, float cx, float cy, float size, UIButtonState state)
{
    const float half = size * 0.5f;

    switch (icon)
    {
    case UIIcon::Sun:        DrawSun(ui, cx, cy, half); return;
    case UIIcon::Moon:       DrawMoon(ui, cx, cy, half); return;
    case UIIcon::Coin:       DrawCoin(ui, cx, cy, half); return;
    case UIIcon::Population:
    case UIIcon::House:      DrawHouse(ui, cx, cy, size); return;
    case UIIcon::Jobs:       DrawJobs(ui, cx, cy, size); return;
    case UIIcon::Residents:  DrawResidents(ui, cx, cy, size); return;
    case UIIcon::Office:     DrawOffice(ui, cx, cy, size); return;
    case UIIcon::Shop:       DrawShop(ui, cx, cy, size); return;
    case UIIcon::RoadNarrow: DrawRoad(ui, cx, cy, size, 0.34f); return;
    case UIIcon::RoadNormal: DrawRoad(ui, cx, cy, size, 0.56f); return;
    case UIIcon::RoadLarge:  DrawRoad(ui, cx, cy, size, 0.86f); return;
    case UIIcon::TimeMarker:
    {
        // 今の時刻の目印(白い線を濃い縁で挟む)。sizeは、タイムラインの高さ。
        ui.DrawLine(cx, cy - size * 1.0f, cx, cy + size * 1.0f, size * 0.56f, m_theme.panelBorder);
        ui.DrawLine(cx, cy - size * 0.875f, cx, cy + size * 0.875f, size * 0.25f, Color(1.0f, 1.0f, 1.0f));
        return;
    }
    default:
        break;
    }

    DrawMonoIcon(ui, icon, cx, cy, size, MonoColor(state));
}

void ProceduralUISkin::DrawMonoIcon(UIRenderer& ui, UIIcon icon, float cx, float cy, float size, const UIColor& color)
{
    const float h = size * 0.5f;
    const float lineWidth = size * 0.13f;
    // 図形の「抜き」の色(「i」の文字・歯車の穴・フロッピーのラベルなど)。選択中は、アイコンが白くなって
    // 明るい色の抜きだと見えなくなるので、選択中のボタンの色(オレンジ)で抜く。
    const bool iconIsLight = color.x > 0.9f && color.y > 0.9f && color.z > 0.9f;
    const UIColor paper = iconIsLight ? m_theme.buttonSelectedBottom : m_theme.chipFill;

    switch (icon)
    {
    case UIIcon::Menu:
    {
        for (int i = -1; i <= 1; ++i)
        {
            ui.DrawLine(cx - h * 0.8f, cy + h * 0.55f * static_cast<float>(i), cx + h * 0.8f, cy + h * 0.55f * static_cast<float>(i), lineWidth * 1.2f, color);
        }
        break;
    }
    case UIIcon::Back:
    {
        ui.DrawLine(cx + h * 0.7f, cy, cx - h * 0.6f, cy, lineWidth * 1.2f, color);
        ui.DrawLine(cx - h * 0.6f, cy, cx - h * 0.05f, cy - h * 0.6f, lineWidth * 1.2f, color);
        ui.DrawLine(cx - h * 0.6f, cy, cx - h * 0.05f, cy + h * 0.6f, lineWidth * 1.2f, color);
        break;
    }
    case UIIcon::Build:
    {
        // 金づち: 斜めの柄と、先の頭。
        ui.DrawLine(cx - h * 0.65f, cy + h * 0.75f, cx + h * 0.30f, cy - h * 0.20f, size * 0.17f, Color(0.60f, 0.38f, 0.18f));
        ui.DrawLine(cx - h * 0.05f, cy - h * 0.60f, cx + h * 0.80f, cy + h * 0.10f, size * 0.30f, color);
        break;
    }
    case UIIcon::Info:
    {
        ui.FillCircle(cx, cy, h * 0.92f, color);
        UITextStyle style;
        style.color = paper;
        float fontSize = std::floor(size * 0.95f + 0.5f);
        if (fontSize < 1.0f) { fontSize = 1.0f; }
        float width = ui.MeasureString("i", fontSize);
        ui.DrawString(cx - width * 0.5f, cy - ui.GetLineHeight(fontSize) * 0.5f, "i", fontSize, style);
        break;
    }
    case UIIcon::Research:
    {
        // フラスコ: 細い首と、広がる底。
        ui.FillRect(UIRect{ cx - h * 0.22f, cy - h * 0.85f, h * 0.44f, h * 0.75f }, color);
        ui.FillTriangle(cx - h * 0.22f, cy - h * 0.15f, cx + h * 0.22f, cy - h * 0.15f, cx + h * 0.85f, cy + h * 0.85f, color);
        ui.FillTriangle(cx - h * 0.22f, cy - h * 0.15f, cx + h * 0.85f, cy + h * 0.85f, cx - h * 0.85f, cy + h * 0.85f, color);
        ui.FillTriangle(cx - h * 0.50f, cy + h * 0.35f, cx + h * 0.50f, cy + h * 0.35f, cx + h * 0.62f, cy + h * 0.70f, Color(0.55f, 0.80f, 0.95f));
        ui.FillTriangle(cx - h * 0.50f, cy + h * 0.35f, cx + h * 0.62f, cy + h * 0.70f, cx - h * 0.62f, cy + h * 0.70f, Color(0.55f, 0.80f, 0.95f));
        break;
    }
    case UIIcon::System:
    {
        // 歯車: 8本の歯と、真ん中の穴。
        for (int i = 0; i < 8; ++i)
        {
            float angle = kTwoPi * static_cast<float>(i) / 8.0f;
            ui.DrawLine(cx + std::cos(angle) * h * 0.55f, cy + std::sin(angle) * h * 0.55f,
                        cx + std::cos(angle) * h * 0.92f, cy + std::sin(angle) * h * 0.92f, size * 0.22f, color);
        }
        ui.FillCircle(cx, cy, h * 0.66f, color);
        ui.FillCircle(cx, cy, h * 0.26f, paper);
        break;
    }
    case UIIcon::Option:
    {
        // スピーカー: 四角い本体+ろうと形の口+音の波。
        ui.FillRect(UIRect{ cx - h * 0.85f, cy - h * 0.30f, h * 0.55f, h * 0.60f }, color);
        ui.FillTriangle(cx - h * 0.30f, cy - h * 0.30f, cx + h * 0.20f, cy - h * 0.80f, cx + h * 0.20f, cy + h * 0.80f, color);
        ui.FillTriangle(cx - h * 0.30f, cy - h * 0.30f, cx + h * 0.20f, cy + h * 0.80f, cx - h * 0.30f, cy + h * 0.30f, color);
        ui.DrawLine(cx + h * 0.50f, cy - h * 0.35f, cx + h * 0.62f, cy, lineWidth, color);
        ui.DrawLine(cx + h * 0.62f, cy, cx + h * 0.50f, cy + h * 0.35f, lineWidth, color);
        ui.DrawLine(cx + h * 0.78f, cy - h * 0.60f, cx + h * 0.95f, cy, lineWidth, color);
        ui.DrawLine(cx + h * 0.95f, cy, cx + h * 0.78f, cy + h * 0.60f, lineWidth, color);
        break;
    }
    case UIIcon::Save:
    {
        // フロッピー: 本体・上のラベル・下の金具。
        ui.FillRoundedRect(UIRect{ cx - h * 0.85f, cy - h * 0.85f, h * 1.7f, h * 1.7f }, size * 0.08f, color, color);
        ui.FillRect(UIRect{ cx - h * 0.50f, cy - h * 0.85f, h * 1.0f, h * 0.55f }, paper);
        ui.FillRect(UIRect{ cx - h * 0.55f, cy + h * 0.10f, h * 1.1f, h * 0.75f }, paper);
        ui.FillRect(UIRect{ cx + h * 0.10f, cy + h * 0.22f, h * 0.22f, h * 0.45f }, color);
        break;
    }
    case UIIcon::Load:
    {
        // フォルダ。
        ui.FillRoundedRect(UIRect{ cx - h * 0.9f, cy - h * 0.72f, h * 0.8f, h * 0.4f }, size * 0.05f, color, color);
        ui.FillRoundedRect(UIRect{ cx - h * 0.9f, cy - h * 0.42f, h * 1.8f, h * 1.3f }, size * 0.07f, color, color);
        ui.FillRect(UIRect{ cx - h * 0.72f, cy - h * 0.22f, h * 1.44f, h * 0.9f }, paper);
        break;
    }
    case UIIcon::RoadStraight:
    {
        ui.DrawLine(cx - h * 0.8f, cy + h * 0.6f, cx + h * 0.8f, cy - h * 0.6f, size * 0.16f, color);
        ui.FillCircle(cx - h * 0.8f, cy + h * 0.6f, size * 0.13f, color);
        ui.FillCircle(cx + h * 0.8f, cy - h * 0.6f, size * 0.13f, color);
        break;
    }
    case UIIcon::RoadFreehand:
    {
        // S字の曲線。
        constexpr int kSteps = 14;
        float prevX = 0.0f;
        float prevY = 0.0f;
        for (int i = 0; i <= kSteps; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(kSteps);
            float x = cx - h * 0.85f + h * 1.7f * t;
            float y = cy - std::sin(t * kTwoPi) * h * 0.55f;
            if (i > 0)
            {
                ui.DrawLine(prevX, prevY, x, y, size * 0.16f, color);
            }
            prevX = x;
            prevY = y;
        }
        break;
    }
    default:
        break;
    }
}

void ProceduralUISkin::DrawSun(UIRenderer& ui, float cx, float cy, float radius)
{
    UIColor ray = Color(1.00f, 0.62f, 0.10f);
    float rayInner = radius * 0.72f;
    float rayOuter = radius * 1.00f;
    float rayThickness = radius * 0.14f;
    for (int i = 0; i < 8; ++i)
    {
        float angle = kTwoPi * static_cast<float>(i) / 8.0f;
        float dx = std::cos(angle);
        float dy = std::sin(angle);
        ui.DrawLine(cx + dx * rayInner, cy + dy * rayInner, cx + dx * rayOuter, cy + dy * rayOuter, rayThickness, ray);
    }
    ui.FillCircle(cx, cy, radius * 0.58f, Color(0.90f, 0.52f, 0.08f));
    ui.FillCircle(cx, cy, radius * 0.48f, Color(1.00f, 0.86f, 0.25f));
}

void ProceduralUISkin::DrawMoon(UIRenderer& ui, float cx, float cy, float radius)
{
    ui.FillCircle(cx, cy, radius * 0.80f, Color(0.45f, 0.48f, 0.72f));
    ui.FillCircle(cx, cy, radius * 0.70f, Color(0.96f, 0.95f, 0.82f));
    // クレーター
    UIColor crater = Color(0.84f, 0.83f, 0.68f);
    ui.FillCircle(cx - radius * 0.22f, cy - radius * 0.18f, radius * 0.16f, crater);
    ui.FillCircle(cx + radius * 0.20f, cy + radius * 0.10f, radius * 0.20f, crater);
    ui.FillCircle(cx - radius * 0.05f, cy + radius * 0.34f, radius * 0.10f, crater);
}

void ProceduralUISkin::DrawCoin(UIRenderer& ui, float cx, float cy, float radius)
{
    // 金貨: 濃い金色の縁、明るい金色の面、左上のハイライト、中央の「¥」。
    ui.FillCircle(cx, cy, radius, Color(0.62f, 0.38f, 0.06f));
    ui.FillCircle(cx, cy, radius * 0.88f, Color(0.98f, 0.76f, 0.16f));
    ui.FillCircle(cx, cy, radius * 0.68f, Color(1.00f, 0.86f, 0.38f));
    ui.FillCircle(cx - radius * 0.36f, cy - radius * 0.40f, radius * 0.16f, Color(1.00f, 0.97f, 0.80f));

    float markSize = std::floor(radius * 1.25f + 0.5f);
    if (markSize < 1.0f) { markSize = 1.0f; }
    UITextStyle mark;
    mark.color = Color(0.55f, 0.32f, 0.04f);
    float markWidth = ui.MeasureString("\xC2\xA5", markSize); // 「¥」(U+00A5)
    ui.DrawString(cx - markWidth * 0.5f, cy - ui.GetLineHeight(markSize) * 0.5f, "\xC2\xA5", markSize, mark);
}

void ProceduralUISkin::DrawHouse(UIRenderer& ui, float cx, float cy, float size)
{
    // 家: 屋根(三角)と壁(四角)とドア
    const float half = size * 0.5f;
    UIColor body = m_theme.iconColor;
    ui.FillTriangle(cx - half, cy - half * 0.05f, cx + half, cy - half * 0.05f, cx, cy - half, Color(0.85f, 0.35f, 0.25f));
    ui.FillRect(UIRect{ cx - half * 0.72f, cy - half * 0.05f, half * 1.44f, half * 1.05f }, Color(1.00f, 0.92f, 0.70f));
    ui.StrokeRoundedRect(UIRect{ cx - half * 0.72f, cy - half * 0.05f, half * 1.44f, half * 1.05f }, 0.0f, 1.2f, body);
    ui.FillRect(UIRect{ cx - half * 0.18f, cy + half * 0.30f, half * 0.36f, half * 0.70f }, body);
}

void ProceduralUISkin::DrawOffice(UIRenderer& ui, float cx, float cy, float size)
{
    // 高いビル: 青灰色の壁と、窓の格子、入口。
    const float h = size * 0.5f;
    UIColor wall = Color(0.45f, 0.58f, 0.78f);
    UIColor window = Color(0.88f, 0.95f, 1.00f);
    ui.FillRoundedRect(UIRect{ cx - h * 0.62f, cy - h, h * 1.24f, h * 2.0f }, 2.0f, wall, wall);
    for (int row = 0; row < 3; ++row)
    {
        for (int col = 0; col < 2; ++col)
        {
            ui.FillRect(UIRect{ cx - h * 0.40f + h * 0.46f * static_cast<float>(col), cy - h * 0.80f + h * 0.46f * static_cast<float>(row), h * 0.34f, h * 0.30f }, window);
        }
    }
    ui.FillRect(UIRect{ cx - h * 0.16f, cy + h * 0.55f, h * 0.32f, h * 0.45f }, m_theme.iconColor);
}

void ProceduralUISkin::DrawShop(UIRenderer& ui, float cx, float cy, float size)
{
    // 店: 赤白のひさしと、壁、入口。
    const float h = size * 0.5f;
    ui.FillRect(UIRect{ cx - h * 0.8f, cy - h * 0.05f, h * 1.6f, h * 1.0f }, Color(1.00f, 0.92f, 0.70f));
    for (int i = 0; i < 4; ++i)
    {
        UIColor stripe = (i % 2 == 0) ? Color(0.88f, 0.28f, 0.22f) : Color(1.0f, 1.0f, 1.0f);
        ui.FillRect(UIRect{ cx - h * 0.9f + h * 0.45f * static_cast<float>(i), cy - h * 0.55f, h * 0.45f, h * 0.5f }, stripe);
    }
    ui.FillRect(UIRect{ cx - h * 0.18f, cy + h * 0.30f, h * 0.36f, h * 0.65f }, m_theme.iconColor);
}

void ProceduralUISkin::DrawJobs(UIRenderer& ui, float cx, float cy, float size)
{
    // ブリーフケース: 取っ手と本体
    const float half = size * 0.5f;
    UIColor caseColor = Color(0.62f, 0.40f, 0.22f);
    ui.StrokeRoundedRect(UIRect{ cx - half * 0.32f, cy - half * 0.92f, half * 0.64f, half * 0.60f }, 2.0f, 1.6f, m_theme.iconColor);
    ui.FillRoundedRect(UIRect{ cx - half, cy - half * 0.40f, size, half * 1.45f }, 2.5f, caseColor, caseColor);
    ui.FillRect(UIRect{ cx - half, cy + half * 0.12f, size, half * 0.14f }, Color(0.40f, 0.24f, 0.12f));
    ui.FillRect(UIRect{ cx - half * 0.14f, cy + half * 0.02f, half * 0.28f, half * 0.34f }, Color(1.00f, 0.85f, 0.30f));
}

void ProceduralUISkin::DrawResidents(UIRenderer& ui, float cx, float cy, float size)
{
    // 人: 頭(円)と体(角丸)
    const float half = size * 0.5f;
    UIColor skin = Color(1.00f, 0.82f, 0.62f);
    UIColor shirt = Color(0.30f, 0.55f, 0.85f);
    ui.FillRoundedRect(UIRect{ cx - half * 0.62f, cy + half * 0.02f, half * 1.24f, half * 0.98f }, half * 0.5f, shirt, shirt);
    ui.FillCircle(cx, cy - half * 0.42f, half * 0.46f, m_theme.iconColor);
    ui.FillCircle(cx, cy - half * 0.42f, half * 0.38f, skin);
}

void ProceduralUISkin::DrawRoad(UIRenderer& ui, float cx, float cy, float size, float widthRatio)
{
    // 道路: 縦に伸びる灰色の舗装と、白い縁の線、中央の黄色い破線。道幅の違いが、太さで分かる。
    const float h = size * 0.5f;
    const float width = size * widthRatio;
    ui.FillRect(UIRect{ cx - h, cy - h * 0.9f, size, h * 1.8f }, Color(0.62f, 0.80f, 0.45f)); // 草地
    ui.FillRect(UIRect{ cx - width * 0.5f, cy - h * 0.9f, width, h * 1.8f }, Color(0.40f, 0.40f, 0.43f));
    ui.FillRect(UIRect{ cx - width * 0.5f, cy - h * 0.9f, size * 0.05f, h * 1.8f }, Color(0.92f, 0.92f, 0.92f));
    ui.FillRect(UIRect{ cx + width * 0.5f - size * 0.05f, cy - h * 0.9f, size * 0.05f, h * 1.8f }, Color(0.92f, 0.92f, 0.92f));
    for (int i = 0; i < 3; ++i)
    {
        ui.FillRect(UIRect{ cx - size * 0.025f, cy - h * 0.72f + h * 0.62f * static_cast<float>(i), size * 0.05f, h * 0.36f }, Color(1.00f, 0.86f, 0.25f));
    }
}
