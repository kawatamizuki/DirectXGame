#pragma once
#include <string>
#include <vector>
#include "UISkin.h"

// ---- タイトル画面のメニュー(スタート・終了) ----
// 背景は、ゲーム本体が描く街(初期マップ)。このクラスは、ロゴの板と、押せるボタンの「配置」「入力」「描画」だけを持つ。
// ゲーム内メニュー(GameMenu)と同じく、描くのはIUISkin。画像素材に差し替える時は、スキンだけを替えればよい。
// 押された結果は、TitleCommandで返す。実際の処理(ゲームを始める・ウィンドウを閉じる)は、ゲーム側(Game)がつなぐ。

enum class TitleCommand
{
    None,
    Start,  // ゲームを始める
    Quit    // ゲームを終わる
};

struct TitleInput
{
    float screenW = 1920.0f;
    float screenH = 1080.0f;
    float mouseX = -1.0f;           // 描画先(ゲームビュー)の左上からのピクセル
    float mouseY = -1.0f;
    bool pressed = false;           // 左ボタンを押したフレーム
    bool down = false;              // 左ボタンを押している間
    bool blockedByOtherUI = false;  // デバッグGUI(ImGui)の上にカーソルがある(反応しない)
};

struct TitleResult
{
    TitleCommand command = TitleCommand::None;  // 押されたボタンの命令(無ければNone)
    bool mouseOverButton = false;               // カーソルがボタンの上にあるか
};

class TitleMenu
{
public:
    // ゲームのタイトル(仮)。決まったら、ここの1行(またはSetTitle)を変える。
    static constexpr const char* kDefaultTitle = "街づくりゲーム(仮)";

    struct Button
    {
        UIRect rect;
        std::string label;
        TitleCommand command = TitleCommand::None;
    };

    // 1フレームぶんの配置結果。文字の幅は測らずに決められる配置だけにしてあるので、描画の準備が無くても計算できる
    // (テストで、はみ出し・重なりを確かめられる)。
    struct Layout
    {
        float scale = 1.0f;
        float screenW = 0.0f;
        float screenH = 0.0f;
        std::string title;
        float titleCenterX = 0.0f;      // タイトルの文字の、横の中心
        float titleTop = 0.0f;          // タイトルの板の、上端
        float titleSize = 0.0f;         // タイトルの文字の大きさ
        float titleBoxHeight = 0.0f;    // タイトルの板の高さ
        std::vector<Button> buttons;
        float labelSize = 0.0f;         // ボタンの文字の大きさ
    };

    void SetTitle(const std::string& title) { m_title = title; }

    // 配置を計算し、ホバー・クリックを処理する。毎フレーム呼ぶ。
    TitleResult Update(const TitleInput& input);

    // Updateで計算した配置で描く。UIRendererのBeginFrame〜EndFrameの間に呼ぶ。
    void Draw(UIRenderer& ui, IUISkin& skin) const;

    // 配置だけを計算する(Updateの内部と、テスト用)。
    Layout ComputeLayout(const TitleInput& input) const;

    const Layout& GetLayout() const { return m_layout; }

private:
    std::string m_title = kDefaultTitle;
    Layout m_layout;
    int m_hover = -1;       // 前回のUpdateで、カーソルの下にあったボタン
    bool m_down = false;    // 前回のUpdateで、左ボタンを押していたか
};
