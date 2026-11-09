#pragma once
#include <string>
#include <vector>
#include "UISkin.h"
#include "ResidentInfoPanel.h"

// ---- 「エリア」パネル(P2-A) ----
// 商業エリアのツールで区画を選んでいる間、画面の右に出る小さなパネル。区画の状態(非公開/周知済み)・店舗数と進捗・周知の猶予・履歴を見せ、
// 「周知する」「削除(周知済みは撤回)」「閉じる(選びをやめる)」を受け付ける。周知・撤回は、確認の手順を踏む(誤って押しても、すぐには確定しない)。
// 後ろのゲーム(区画の移動など)の操作を邪魔しないよう、画面全体は覆わない。カーソルがパネルの上にある時だけ、ゲームへの入力を止める。
// このクラスは「配置(Layout)」と「入力(どのボタンを押したか)」だけを持ち、区画・ゲームの中身は知らない。
// 毎フレーム渡される AreaPanelModel だけを見て並べる。描くのはIUISkin(見た目は、スキンで差し替えられる)。
// 行の型は、住民の情報パネルと同じ(ResidentInfoRow: 見出し・値・ゲージ付きの値・注記)。

struct AreaPanelModel
{
    bool visible = false;               // 出すか(区画を選んでいる間だけtrue)
    std::string title;                  // 「商業エリア #1」
    std::string subtitle;               // 「非公開の仮置き」「周知済み」
    std::vector<ResidentInfoRow> rows;
    std::string publishLabel;           // 「周知する」。空なら、周知のボタンを出さない(周知済み)
    std::string publishConfirm;         // 周知の確認の文(改行は '\n')
    std::string deleteLabel;            // 「削除」/「撤回」
    std::string deleteConfirm;          // 削除の確認の文。空なら、確認なしで削除する(非公開の仮置きは、無料で、いつでも撤回できる)
};

struct AreaPanelInput
{
    float screenW = 1920.0f;
    float screenH = 1080.0f;
    float topInset = 0.0f;
    float mouseX = -1.0f;
    float mouseY = -1.0f;
    bool pressed = false;               // 左ボタンを押したフレーム
    bool down = false;
    bool blockedByOtherUI = false;      // デバッグGUI(ImGui)の上にカーソルがある(反応しない)
};

struct AreaPanelResult
{
    bool mouseOverUI = false;           // カーソルがパネルの上にあるか(ゲームへの入力を止める)
    bool consumedClick = false;         // このフレームのクリックを受け取ったか
    bool publishConfirmed = false;      // 周知が、確認まで済んだ
    bool deleteConfirmed = false;       // 削除(撤回)が、確認まで済んだ
    bool closeRequested = false;        // 「閉じる」が押された(選びをやめる)
};

class AreaPanel
{
public:
    enum class Confirm { None, Publish, Delete };

    struct ButtonLayout
    {
        UIRect rect;
        std::string label;
        bool enabled = true;
    };

    struct Layout
    {
        float scale = 1.0f;
        bool visible = false;
        UIRect panel;
        float titleX = 0.0f, titleY = 0.0f, titleSize = 0.0f;
        float subtitleX = 0.0f, subtitleY = 0.0f, subtitleSize = 0.0f;
        float textSize = 0.0f;
        float noteSize = 0.0f;
        std::vector<ResidentInfoPanel::RowLayout> rows;
        // 確認の文(確認の手順の間だけ)。
        std::vector<std::string> confirmLines;
        float confirmX = 0.0f, confirmY = 0.0f, confirmLineHeight = 0.0f;
        std::vector<ButtonLayout> buttons;  // 通常: 周知する / 削除 / 閉じる。確認中: はい / やめる
    };

    // 配置を計算し、押された物を処理する。ワールドへの入力処理より先に、毎フレーム呼ぶ。
    AreaPanelResult Update(const AreaPanelInput& input, const AreaPanelModel& model);

    // Updateで計算した配置で描く。UIRendererのBeginFrame〜EndFrameの間に呼ぶ。
    void Draw(UIRenderer& ui, IUISkin& skin, const AreaPanelModel& model) const;

    // 配置だけを計算する(Updateの内部と、テスト用)。
    Layout ComputeLayout(const AreaPanelInput& input, const AreaPanelModel& model) const;

    Confirm GetConfirm() const { return m_confirm; }
    void ResetConfirm() { m_confirm = Confirm::None; }
    const Layout& GetLayout() const { return m_layout; }

private:
    Confirm m_confirm = Confirm::None;
    int m_hoverButton = -1;
    Layout m_layout;
};
