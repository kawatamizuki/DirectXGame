#pragma once
#include <optional>
#include <string>
#include "UIRenderer.h"
#include "UISkin.h"
#include "ProceduralUISkin.h"
#include "GameMenu.h"
#include "ResidentInfoPanel.h"
#include "MoveInPanel.h"
#include "DemandSystem.h"
#include "CostPreview.h"

class DayNightCycle;

// 上部HUDに表示する内容(毎フレーム、GameUIの外から渡す)。
// GameUIはDemandSystemやGameClockを直接は知らず、この値だけを見て描く。
// これで、ゲームの他の部分が無くてもHUDだけを描いて確認できる。
// 世界の建物に付ける小さな警告(「道路がない！」など)。位置は、画面のピクセル(ゲームビューの左上が原点)。
struct HudWorldLabel
{
    float x = 0.0f;
    float y = 0.0f;
    std::string text;
};

struct HudState
{
    int dayCount = 1;               // 1から数える日数
    std::string calendarText;       // 「1年目 1月 第1週」のような日付の文字。空なら、dayCountから「Day N」を出す
    float timeOfDayHours = 8.0f;    // 0〜24の時刻
    int cash = 0;                   // 所持金(円)
    DemandStats stats;              // 人口・雇用などの街の指標

    // カーソルの横に出す建設費のタグ(建物・道路を置こうとしている間だけvisible)。
    // 位置はcursorX/Y(描画先の左上からのピクセル座標)の近くに出す。
    CostPreview costTag;
    float cursorX = -1.0f;
    float cursorY = -1.0f;

    // ゲーム内メニュー(右下のボタンと、開いた時の項目)に出す内容。
    MenuModel menu;

    // 「住民の情報」パネルに出す内容(開いている間だけ使う)。
    ResidentInfoModel residentInfo;

    // 「入居者を選ぶ」パネルに出す内容(開いている間だけ使う)。
    MoveInModel moveIn;

    // 世界の建物に付ける警告(建物の少し上に出す。ゲーム側が、画面の位置に直して渡す)。
    std::vector<HudWorldLabel> worldLabels;

    // 画面の左下に出す、操作のヒント(住民を追跡している間など)。空なら出さない。メニュー・パネルが開いている間は出さない。
    std::string hintText;
};

// マウスの状態と画面サイズ(毎フレーム、GameUIの外から渡す)。
struct HudInput
{
    float screenW = 1920.0f;
    float screenH = 1080.0f;
    float mouseX = -1.0f;           // 描画先(ウィンドウのクライアント領域)の左上からのピクセル座標
    float mouseY = -1.0f;
    bool pressed = false;           // 左ボタンを押したフレーム
    bool down = false;              // 左ボタンを押している間(ボタンの「押している」見た目に使う)
    bool menuEnabled = true;        // ゲーム内メニューを使える状態か(Playingの時だけtrue)
    bool blockedByOtherUI = false;  // デバッグGUI(ImGui)のパネルの上にカーソルがある時はtrue(HUDは反応しない)

    // メニューのオプションの数字欄への入力(MenuInputと同じ意味)。
    std::string typedDigits;
    bool keyBackspace = false;
    bool keyEnter = false;
    bool keyEscape = false;
};

// GameUI::Updateの結果。
struct HudResult
{
    bool mouseOverUI = false;                   // カーソルがHUDの上にあるか(ワールドへの操作を止めるのに使う)
    bool consumedClick = false;                 // このフレームのクリックをHUDが受け取ったか
    MenuCommand menuCommand;                    // ゲーム内メニューで押された命令(無ければNone)
    bool menuOpen = false;                      // ゲーム内メニューが開いているか(開いている間は、ゲーム内の時間を止める)
    bool menuTabChanged = false;                // このフレームに、メニューのタブが切り替わったか(効果音を鳴らすのに使う)
    bool residentPanelOpen = false;             // 「住民の情報」パネルが開いているか(開いている間は、ゲーム内の時間を止める)
    bool moveInOpen = false;                    // 「入居者を選ぶ」パネルが開いているか(開いている間は、ゲーム内の時間を止める)
    uint32_t moveInChosenResidentId = 0;        // このフレームに、入居する住民が選ばれた(0=選ばれていない)
    bool moveInDeclined = false;                // このフレームに、「新しい住民を迎える」が押された
};

// ゲームプレイ用の上部HUDバーと、ゲーム内メニュー。配置・入力をまとめて担当する(ImGuiは使わない)。
// 長さはすべて「画面の高さ1080を基準にしたピクセル」で決めておき、画面サイズに応じた倍率をかけて描く。
// 箱・ボタン・アイコン・文字の色の「描き方」は、全てIUISkin(UISkin.h)を通す。見た目の差し替えは、SetSkinで行う。
class GameUI
{
public:
    struct Chip
    {
        UIRect rect;
        std::string text;
        UIIcon icon = UIIcon::Population;
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

    // 見た目(スキン)を差し替える。skinは呼び出し側が所有する(借りるだけ)。nullptrなら、標準の図形スキンに戻す。
    void SetSkin(IUISkin* skin) { m_skin = skin ? skin : &m_defaultSkin; }
    IUISkin& GetSkin() { return *m_skin; }

    // ゲーム内メニューの開閉(Escキー・ゲーム側の都合で閉じる時に使う)。
    bool IsMenuOpen() const { return m_menu.IsOpen(); }
    void OpenMenu() { m_menu.Open(); }
    void CloseMenu() { m_menu.Close(); }
    bool IsMenuEditingNumber() const { return m_menu.IsEditingNumber(); } // オプションの数字欄に入力中か(入力中は、Esc・右クリックで閉じない)
    const GameMenu& GetMenu() const { return m_menu; }

    // 「住民の情報」パネル(住宅を選んで、もう一度クリックすると開く)。開いている間は、ゲーム内の時間が止まる(止める処理はGame側)。
    bool IsResidentPanelOpen() const { return m_residentPanel.IsOpen(); }
    void OpenResidentPanel() { m_residentPanel.Open(); }
    void CloseResidentPanel() { m_residentPanel.Close(); }
    const ResidentInfoPanel& GetResidentPanel() const { return m_residentPanel; }

    // 「入居者を選ぶ」パネル(住宅を建てた時に、住居なしの住民がいたら開く)。開いている間は、ゲーム内の時間が止まる(止める処理はGame側)。
    // 選ばれた・「新しい住民を迎える」が押された結果は、Updateの結果(HudResult)で受け取る。
    bool IsMoveInOpen() const { return m_moveInPanel.IsOpen(); }
    void OpenMoveIn() { m_moveInPanel.Open(); }
    void CloseMoveIn() { m_moveInPanel.Close(); }
    const MoveInPanel& GetMoveInPanel() const { return m_moveInPanel; }

    // 配置を計算し、マウスの当たり判定とクリック処理を行う。毎フレーム、ワールドへの入力処理より先に呼ぶ。
    HudResult Update(const HudInput& input, const HudState& state);

    // Updateで計算した配置で描く。UIRendererのBeginFrame〜EndFrameの間に呼ぶ。
    void Draw(const HudState& state);

    // stateのcostTagとカーソル位置から、費用タグの配置を求める(Drawの内部でも使う)。
    // 画面の端に近い時は、はみ出さないようカーソルの反対側に出す。画面サイズはUpdateで計算した配置のものを使う。
    CostTagLayout ComputeCostTagLayout(const HudState& state) const;

    const Layout& GetLayout() const { return m_layout; }

    // 標準スキンの色・太さ(調整用)。別のスキンを使っている間は、見た目に影響しない。
    UITheme& GetTheme() { return m_defaultSkin.GetTheme(); }

private:
    void ComputeLayout(float screenW, float screenH, const HudState& state);
    void DrawCostTag(const HudState& state);

    UIRenderer* m_ui = nullptr;
    const DayNightCycle* m_cycle = nullptr;
    ProceduralUISkin m_defaultSkin;     // 標準の見た目(図形)
    IUISkin* m_skin = &m_defaultSkin;   // 今使っている見た目。差し替えはSetSkin
    GameMenu m_menu;
    ResidentInfoPanel m_residentPanel;
    MoveInPanel m_moveInPanel;
    Layout m_layout;
};
