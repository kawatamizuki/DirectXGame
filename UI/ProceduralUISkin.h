#pragma once
#include "UISkin.h"
#include "UITheme.h"

// 図形(角丸・円・線・三角)だけで描くスキン。画像素材は使わない。
// 色・太さ・角丸はUIThemeに集約してあるので、値を変えるだけで見た目を調整できる。
// テクスチャ版のスキンを作るまでの、標準のスキン。
class ProceduralUISkin : public IUISkin
{
public:
    void DrawPanel(UIRenderer& ui, const UIRect& rect, UIPanelStyle style, float scale) override;
    void DrawButton(UIRenderer& ui, const UIRect& rect, UIButtonState state, float scale) override;
    void DrawSlider(UIRenderer& ui, const UIRect& track, float value01, UIButtonState state, float scale) override;
    void DrawGauge(UIRenderer& ui, const UIRect& rect, float value01, float marker01, bool warning, float scale) override;
    void DrawIcon(UIRenderer& ui, UIIcon icon, float cx, float cy, float size, UIButtonState state) override;
    UITextStyle GetTextStyle(UITextRole role) const override;

    UITheme& GetTheme() { return m_theme; }
    const UITheme& GetTheme() const { return m_theme; }

private:
    // 1色のアイコン(線・図形)の色。選択中は白、押せない時はうすい色。
    UIColor MonoColor(UIButtonState state) const;

    // アイコンごとの描画。
    void DrawSun(UIRenderer& ui, float cx, float cy, float radius);
    void DrawMoon(UIRenderer& ui, float cx, float cy, float radius);
    void DrawCoin(UIRenderer& ui, float cx, float cy, float radius);
    void DrawHouse(UIRenderer& ui, float cx, float cy, float size);
    void DrawOffice(UIRenderer& ui, float cx, float cy, float size);
    void DrawShop(UIRenderer& ui, float cx, float cy, float size);
    void DrawJobs(UIRenderer& ui, float cx, float cy, float size);
    void DrawResidents(UIRenderer& ui, float cx, float cy, float size);
    void DrawRoad(UIRenderer& ui, float cx, float cy, float size, float widthRatio);
    void DrawMonoIcon(UIRenderer& ui, UIIcon icon, float cx, float cy, float size, const UIColor& color);

    UITheme m_theme;
};
