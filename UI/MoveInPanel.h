#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "UISkin.h"

// ---- 入居者を選ぶパネル ----
// 住宅を建てた時に、住む家が無い(住居なし)住民がいたら開く。「どの住民を、新しい住宅に入れるか」を、一覧から選ぶ。
// 誰も選ばない時は、「新しい住民を迎える」(今までどおり、新しい住民が生まれる)。
// 開いている間は、ゲーム内の時間が止まる(止める処理はGame側。このクラスは時間を知らない)。
// このクラスは「配置(Layout)」と「入力(選ぶ・ページ送り)」だけを持ち、住民・ゲームの中身は知らない。
// 毎フレーム渡される MoveInModel(行の一覧)だけを見て並べる。描くのはIUISkin(見た目は、スキンで差し替えられる)。
// Esc・右クリックで「新しい住民を迎える」と同じになるのは、Game側が処理する(このクラスは、パネルの外のクリックでは閉じない。誤操作を避けるため)。

// 一覧の1行(住居なしの住民1人)。
struct MoveInRow
{
    uint32_t residentId = 0;        // 住民の識別(選ばれた時に、そのまま返す)
    std::string title;              // 「住民 #22」
    std::string detail;             // 「満足度 85 / 体力 60 / 生活 70・活動 55」など、判断の材料
    bool warning = false;           // trueなら、detailを警告の色にする(不満が大きいなど)
};

struct MoveInModel
{
    std::string title;              // 「入居する住民を選ぶ」
    std::string subtitle;           // 「新しい住宅 #5 に住む人を選んでください」
    std::vector<MoveInRow> rows;    // 住居なしの住民の一覧
    std::string declineLabel;       // 「新しい住民を迎える」
};

struct MoveInInput
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

struct MoveInResult
{
    bool mouseOverUI = false;       // 開いている間は、画面全体(後ろのゲームへ何も届かない)
    bool consumedClick = false;     // このフレームのクリックを受け取ったか
    uint32_t chosenResidentId = 0;  // 選ばれた住民(0=選ばれていない)
    bool declined = false;          // 「新しい住民を迎える」が押された
};

class MoveInPanel
{
public:
    // 1フレームぶんの配置結果。テストで「はみ出し・重なりが無いか」を確かめられるよう公開している。
    struct RowLayout
    {
        UIRect rect;                    // 行(ボタン)の全体
        float titleX = 0.0f, titleY = 0.0f;
        float detailX = 0.0f, detailY = 0.0f;
        size_t modelIndex = 0;          // MoveInModel::rows の何番目か
    };

    struct Layout
    {
        float scale = 1.0f;
        bool open = false;
        UIRect overlay;                 // 開いている間、後ろを暗くする幕
        UIRect panel;                   // パネルの枠
        float titleX = 0.0f, titleY = 0.0f, titleSize = 0.0f;
        float subtitleX = 0.0f, subtitleY = 0.0f, subtitleSize = 0.0f;
        float textSize = 0.0f;          // 行の見出しの文字の大きさ
        float detailSize = 0.0f;        // 行の補足の文字の大きさ
        std::vector<RowLayout> rows;    // 今のページに出す行(画面に入る数まで)
        UIRect declineButton;           // 「新しい住民を迎える」
        UIRect prevButton;              // 前のページ(ページが1つだけなら、幅0)
        UIRect nextButton;              // 次のページ
        float pageTextX = 0.0f, pageTextY = 0.0f;
        std::string pageText;           // 「1 / 2」(ページが1つだけなら空)
        int page = 0;                   // 今のページ(0から)
        int pageCount = 1;
        int rowsPerPage = 1;
    };

    // 配置を計算し、押された物を処理する。ワールドへの入力処理より先に、毎フレーム呼ぶ。
    MoveInResult Update(const MoveInInput& input, const MoveInModel& model);

    // Updateで計算した配置で描く。UIRendererのBeginFrame〜EndFrameの間に呼ぶ。
    void Draw(UIRenderer& ui, IUISkin& skin, const MoveInModel& model) const;

    // 配置だけを計算する(Updateの内部と、テスト用)。ページは、今のページ(範囲に収める)を使う。
    Layout ComputeLayout(const MoveInInput& input, const MoveInModel& model) const;

    bool IsOpen() const { return m_open; }
    void Open() { m_open = true; m_page = 0; m_hoverRow = -1; }
    void Close() { m_open = false; }

    const Layout& GetLayout() const { return m_layout; }

private:
    bool m_open = false;
    int m_page = 0;
    int m_hoverRow = -1;            // カーソルが乗っている行(Layout::rowsの添字。無ければ-1)
    bool m_hoverDecline = false;
    bool m_hoverPrev = false;
    bool m_hoverNext = false;
    Layout m_layout;
};
