#pragma once
#include <optional>
#include <string>
#include "UIRenderer.h"
#include "UITheme.h"
#include "DemandSystem.h"

class DayNightCycle;

// 上部HUDに表示する内容(毎フレーム、GameUIの外から渡す)。
// GameUIはDemandSystemやGameClockを直接は知らず、この値だけを見て描く。
// これで、ゲームの他の部分が無くてもHUDだけを描いて確認できる。
struct HudState
{
    int dayCount = 1;               // 1から数える日数
    float timeOfDayHours = 8.0f;    // 0〜24の時刻
    float timeScale = 1.0f;         // 時間の倍率(0=停止)
    DemandStats stats;              // 人口・雇用などの街の指標
};

// マウスの状態と画面サイズ(毎フレーム、GameUIの外から渡す)。
struct HudInput
{
    float screenW = 1920.0f;
    float screenH = 1080.0f;
    float mouseX = -1.0f;           // 描画先(ウィンドウのクライアント領域)の左上からのピクセル座標
    float mouseY = -1.0f;
    bool pressed = false;           // 左ボタンを押したフレーム
    bool down = false;              // 左ボタンを押している間
    bool released = false;          // 左ボタンを離したフレーム
    bool blockedByOtherUI = false;  // デバッグGUI(ImGui)のパネルの上にカーソルがある時はtrue(HUDは反応しない)
};

// GameUI::Updateの結果。
struct HudResult
{
    bool mouseOverUI = false;                   // カーソルがHUDの上にあるか(ワールドへの操作を止めるのに使う)
    bool consumedClick = false;                 // このフレームのクリックをHUDが受け取ったか
    std::optional<float> requestedTimeScale;    // 速度ボタンが押された時の、新しい時間の倍率
};

// ゲームプレイ用の上部HUDバー。配置・入力・描画をまとめて担当する(ImGuiは使わない)。
// 長さはすべて「画面の高さ1080を基準にしたピクセル」で決めておき、画面サイズに応じた倍率をかけて描く。
class GameUI
{
public:
    // 速度ボタン1つぶん。
    struct SpeedButton
    {
        UIRect rect;
        float timeScale = 1.0f;
    };

    // 指標のチップ(アイコン+文字の小さな角丸の箱)の種類。
    enum class ChipIcon
    {
        Population,
        Jobs,
        Residents
    };

    struct Chip
    {
        UIRect rect;
        std::string text;
        ChipIcon icon = ChipIcon::Population;
    };

    // 1フレームぶんの配置結果。テストで「はみ出し・重なりが無いか」を確かめられるよう公開している。
    struct Layout
    {
        float scale = 1.0f;
        UIRect bar;                 // 画面上端の全幅のバー
        float iconCenterX = 0.0f;   // 昼夜アイコンの中心
        float iconCenterY = 0.0f;
        float iconRadius = 0.0f;
        std::string timeText;       // "HH:MM"
        float timeTextX = 0.0f;
        float timeTextY = 0.0f;
        float timeTextSize = 0.0f;
        std::string dayText;        // "Day N"
        float dayTextX = 0.0f;
        float dayTextY = 0.0f;
        float dayTextSize = 0.0f;
        UIRect timeline;            // 1日のタイムライン
        SpeedButton buttons[4];     // 停止/1x/2x/4x
        Chip chips[3];
        int chipCount = 0;          // 画面が狭くて入りきらない時は、右から数えて収まる数だけ
        float leftBlockRight = 0.0f; // 左のブロック(アイコン〜タイムライン)の右端
    };

    // uiRendererとdayNightCycleは呼び出し側が所有する(このクラスは借りるだけ)。
    void Initialize(UIRenderer* uiRenderer, const DayNightCycle* dayNightCycle);

    // 配置を計算し、マウスの当たり判定とクリック処理を行う。毎フレーム、ワールドへの入力処理より先に呼ぶ。
    HudResult Update(const HudInput& input, const HudState& state);

    // Updateで計算した配置で描く。UIRendererのBeginFrame〜EndFrameの間に呼ぶ。
    void Draw(const HudState& state);

    const Layout& GetLayout() const { return m_layout; }
    UITheme& GetTheme() { return m_theme; }

private:
    void ComputeLayout(float screenW, float screenH, const HudState& state);
    int SelectedSpeedIndex(float timeScale) const;

    // アイコン(すべて図形で描く。素材は使わない)。
    void DrawSunIcon(float cx, float cy, float radius);
    void DrawMoonIcon(float cx, float cy, float radius);
    void DrawSpeedIcon(int index, const UIRect& rect, const UIColor& color);
    void DrawChipIcon(ChipIcon icon, float cx, float cy, float size);

    UIRenderer* m_ui = nullptr;
    const DayNightCycle* m_cycle = nullptr;
    UITheme m_theme;
    Layout m_layout;

    int m_hoveredButton = -1;   // カーソルが乗っている速度ボタン(なければ-1)
    int m_pressedButton = -1;   // 押し始めた速度ボタン(離した時に同じボタンの上なら「押された」)
    bool m_mouseDown = false;
};
