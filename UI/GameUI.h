#pragma once
#include <optional>
#include <string>
#include "UIRenderer.h"
#include "UITheme.h"
#include "DemandSystem.h"
#include "CostPreview.h"

class DayNightCycle;

// 上部HUDに表示する内容(毎フレーム、GameUIの外から渡す)。
// GameUIはDemandSystemやGameClockを直接は知らず、この値だけを見て描く。
// これで、ゲームの他の部分が無くてもHUDだけを描いて確認できる。
struct HudState
{
    int dayCount = 1;               // 1から数える日数
    float timeOfDayHours = 8.0f;    // 0〜24の時刻
    int cash = 0;                   // 所持金(円)
    DemandStats stats;              // 人口・雇用などの街の指標

    // カーソルの横に出す建設費のタグ(建物・道路を置こうとしている間だけvisible)。
    // 位置はcursorX/Y(描画先の左上からのピクセル座標)の近くに出す。
    CostPreview costTag;
    float cursorX = -1.0f;
    float cursorY = -1.0f;
};

// マウスの状態と画面サイズ(毎フレーム、GameUIの外から渡す)。
struct HudInput
{
    float screenW = 1920.0f;
    float screenH = 1080.0f;
    float mouseX = -1.0f;           // 描画先(ウィンドウのクライアント領域)の左上からのピクセル座標
    float mouseY = -1.0f;
    bool pressed = false;           // 左ボタンを押したフレーム
    bool blockedByOtherUI = false;  // デバッグGUI(ImGui)のパネルの上にカーソルがある時はtrue(HUDは反応しない)
};

// GameUI::Updateの結果。
struct HudResult
{
    bool mouseOverUI = false;                   // カーソルがHUDの上にあるか(ワールドへの操作を止めるのに使う)
    bool consumedClick = false;                 // このフレームのクリックをHUDが受け取ったか
};

// ゲームプレイ用の上部HUDバー。配置・入力・描画をまとめて担当する(ImGuiは使わない)。
// 長さはすべて「画面の高さ1080を基準にしたピクセル」で決めておき、画面サイズに応じた倍率をかけて描く。
class GameUI
{
public:
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
        float screenW = 0.0f;
        float screenH = 0.0f;
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
        UIRect cashBox;             // 中央の所持金の箱(コインのアイコン+金額)
        std::string cashText;       // "1,000円"
        float cashTextX = 0.0f;
        float cashTextY = 0.0f;
        float cashTextSize = 0.0f;
        float coinCenterX = 0.0f;   // コインのアイコンの中心と半径
        float coinCenterY = 0.0f;
        float coinRadius = 0.0f;
        Chip chips[3];
        int chipCount = 0;          // 画面が狭くて入りきらない時は、右から数えて収まる数だけ
        float leftBlockRight = 0.0f; // 左のブロック(アイコン〜タイムライン)の右端
    };

    // カーソルの横の建設費タグの配置結果(テストで画面からはみ出していないかを確かめられるよう公開している)。
    struct CostTagLayout
    {
        bool visible = false;
        UIRect rect;                // タグの箱
        std::string costText;       // "6マス  -120円" / "-100円"
        std::string warningText;    // 払えない時だけ「お金が足りません」(払える時は空)
        float textX = 0.0f;
        float costTextY = 0.0f;
        float warningTextY = 0.0f;
        float fontSize = 0.0f;
    };

    // uiRendererとdayNightCycleは呼び出し側が所有する(このクラスは借りるだけ)。
    void Initialize(UIRenderer* uiRenderer, const DayNightCycle* dayNightCycle);

    // 配置を計算し、マウスの当たり判定とクリック処理を行う。毎フレーム、ワールドへの入力処理より先に呼ぶ。
    HudResult Update(const HudInput& input, const HudState& state);

    // Updateで計算した配置で描く。UIRendererのBeginFrame〜EndFrameの間に呼ぶ。
    void Draw(const HudState& state);

    // stateのcostTagとカーソル位置から、費用タグの配置を求める(Drawの内部でも使う)。
    // 画面の端に近い時は、はみ出さないようカーソルの反対側に出す。画面サイズはUpdateで計算した配置のものを使う。
    CostTagLayout ComputeCostTagLayout(const HudState& state) const;

    const Layout& GetLayout() const { return m_layout; }
    UITheme& GetTheme() { return m_theme; }

private:
    void ComputeLayout(float screenW, float screenH, const HudState& state);

    // アイコン(すべて図形で描く。素材は使わない)。
    void DrawSunIcon(float cx, float cy, float radius);
    void DrawMoonIcon(float cx, float cy, float radius);
    void DrawCoinIcon(float cx, float cy, float radius);
    void DrawChipIcon(ChipIcon icon, float cx, float cy, float size);
    void DrawCostTag(const HudState& state);

    UIRenderer* m_ui = nullptr;
    const DayNightCycle* m_cycle = nullptr;
    UITheme m_theme;
    Layout m_layout;
};
