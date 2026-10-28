#pragma once
#include <string>
#include <vector>
#include "UISkin.h"

// ---- 住民の情報パネル ----
// ゲーム内で、住宅を1回クリック(選択)し、同じ住宅をもう1回クリックすると開く、その住宅に住む住民の内部パラメータの表示。
// 開いている間は、ゲーム内の時間が止まる(止める処理はGame側。このクラスは時間を知らない)。
// このクラスは「配置(Layout)」と「入力(閉じる操作)」だけを持ち、住民・ゲームの中身は知らない。
// 毎フレーム渡される ResidentInfoModel(行の一覧)だけを見て並べる。描くのはIUISkin(見た目は、スキンで差し替えられる)。
// 閉じる: 「閉じる」ボタン、パネルの外のクリック(Esc・右クリックは、Game側が閉じる)。

// 表示する1行。
struct ResidentInfoRow
{
    enum class Kind
    {
        Header,     // 見出し(「需要」「体力」など)。label だけ使う
        Value,      // 「名前 ... 値」の行。gauge >= 0 なら、間にゲージを出す
        Note        // 長めの文(直近の選択など)。label の下に value を、小さい文字で出す
    };

    Kind kind = Kind::Value;
    std::string label;
    std::string value;
    float gauge = -1.0f;        // 0〜1。負ならゲージを出さない
    float marker = -1.0f;       // 0〜1。ゲージの上に出す目印(充足のしきい値など)。負なら出さない
    bool warning = false;       // trueなら、値・ゲージを警告の色にする(需要が不足など)
};

// パネルに出す内容。毎フレーム、ゲーム側が作って渡す。
struct ResidentInfoModel
{
    std::string title;                      // 「住民の情報」
    std::string subtitle;                   // 「住宅 #5 / 今の状態: 家にいる」
    std::vector<ResidentInfoRow> rows;
    std::string emptyMessage;               // 空でなければ、rowsの代わりにこの文を出す(住民がまだいない時など)
};

struct ResidentPanelInput
{
    float screenW = 1920.0f;
    float screenH = 1080.0f;
    float topInset = 0.0f;          // 上部HUDの下端(パネルは、これより下に出す)
    float mouseX = -1.0f;
    float mouseY = -1.0f;
    bool pressed = false;           // 左ボタンを押したフレーム
    bool down = false;              // 左ボタンを押している間
    bool blockedByOtherUI = false;  // デバッグGUI(ImGui)の上にカーソルがある(反応しない)
};

struct ResidentPanelResult
{
    bool mouseOverUI = false;       // 開いている間は、画面全体(後ろのゲームへ何も届かない)
    bool consumedClick = false;     // このフレームのクリックを受け取ったか
    bool closed = false;            // このフレームに、パネルが閉じられたか
};

class ResidentInfoPanel
{
public:
    // 1フレームぶんの配置結果。テストで「はみ出し・重なりが無いか」を確かめられるよう公開している。
    struct RowLayout
    {
        UIRect rowRect;                 // 行の全体
        float labelX = 0.0f;            // 文字の左端
        float labelY = 0.0f;
        float valueRight = 0.0f;        // 値の右端(右寄せ)
        UIRect gaugeRect;               // ゲージ(幅0なら出ない)
        float noteY = 0.0f;             // Noteの、2行目(value)のY
    };

    struct Layout
    {
        float scale = 1.0f;
        bool open = false;
        UIRect overlay;                 // 開いている間、後ろを暗くする幕
        UIRect panel;                   // パネルの枠
        UIRect closeButton;             // 「閉じる」ボタン
        float titleX = 0.0f, titleY = 0.0f, titleSize = 0.0f;
        float subtitleX = 0.0f, subtitleY = 0.0f, subtitleSize = 0.0f;
        float textSize = 0.0f;          // 行の文字の大きさ
        float noteSize = 0.0f;
        std::vector<RowLayout> rows;    // modelのrowsと同じ並び(emptyMessageの時は空)。画面が極端に小さい時は、入る分だけ(先頭から)
        float emptyX = 0.0f, emptyY = 0.0f;
    };

    // 配置を計算し、押された物を処理する。ワールドへの入力処理より先に、毎フレーム呼ぶ。
    ResidentPanelResult Update(const ResidentPanelInput& input, const ResidentInfoModel& model);

    // Updateで計算した配置で描く。UIRendererのBeginFrame〜EndFrameの間に呼ぶ。
    void Draw(UIRenderer& ui, IUISkin& skin, const ResidentInfoModel& model) const;

    // 配置だけを計算する(Updateの内部と、テスト用)。
    Layout ComputeLayout(const ResidentPanelInput& input, const ResidentInfoModel& model) const;

    bool IsOpen() const { return m_open; }
    void Open() { m_open = true; }
    void Close() { m_open = false; }

    const Layout& GetLayout() const { return m_layout; }

private:
    bool m_open = false;
    bool m_hoverClose = false;
    Layout m_layout;
};
