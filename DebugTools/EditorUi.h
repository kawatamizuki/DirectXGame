#pragma once
#include <string>
#include "EditorSettings.h"

// エディタまわりの画面(ImGui)をまとめた部品。Unityのように、パネルを整理して並べる土台と、上部の「Editor」メニュー、
// ウィンドウにカーソルを合わせた時のヒント、ショートカット一覧を受け持つ。
//
//  ・ドックスペース: HUD(ゲームの上部バー)とメニューバーの下から画面いっぱいに置く。中央は透過していて、
//    ゲームの画面(3D)がそのまま見え、中央の上のマウスはゲームに届く(PassthruCentralNode)。
//    パネルは、左(Hierarchy+ツール)・右(Inspector)・下(Assets/Log/Profiler)に吸着した初期配置で始まり、
//    タイトルバーをドラッグして自由に動かす・タブにまとめる。配置はImGuiが imgui.ini に保存する。
//  ・Editorメニュー: パネルの表示切り替え、レイアウトの初期化、ヒントのON/OFF、エディタ機能のON/OFF一覧、ショートカット一覧。
//  ・ヒント: ウィンドウの上にカーソルがある間だけ、そのウィンドウで使えるキーを小さく表示する(ShortcutTableの内容)。
//  ・設定(パネルの表示・ヒント・スイッチ)は Settings/editor.json に保存する(個人の設定なのでGit管理外)。
//
// 毎フレームの使い方: ImGuiのNewFrameの後に BeginFrame、各パネルを描いた後に EndFrame。
class EditorUi
{
public:
    static constexpr const char* kSettingsPath = "Settings/editor.json";

    // 画面ごとの情報(Gameが毎フレーム渡す)。
    struct FrameInfo
    {
        float hudTopInset = 0.0f;       // ゲームの上部バーの下端(メニューバーはこの下に置く)
        bool inGameScene = false;       // ゲームシーンか(ゲーム画面のヒントを出すか)
        bool mouseOverGameHud = false;  // カーソルが上部バーなどの上にあるか(ゲーム画面のヒントを出さない)
        bool playing = false;           // Playingモードか(falseならEditing)
    };

    // パネルを登録する。設定ファイルを読むのは、全てのパネルとスイッチが登録された後の LoadSettings で行う。
    void Initialize();

    EditorSettings& Settings() { return m_settings; }
    const EditorSettings& Settings() const { return m_settings; }

    // 設定ファイルを読む(無い・壊れている時は既定値のまま)。
    void LoadSettings();

    // パネルの表示フラグ(ImGui::Begin(name, flag) に渡す)。登録されていない名前ならnullptr。
    bool* PanelFlag(const char* name);

    // メニューバーとドックスペースを描く。パネルを描く前に呼ぶ。
    void BeginFrame(const FrameInfo& info);

    // ヒントとショートカット一覧を描く。全てのパネルを描いた後に呼ぶ。
    void EndFrame(const FrameInfo& info);

    // 設定が変わっていれば保存する(定期的に呼ぶ。毎回は書かない)。
    void SaveSettingsIfChanged(bool force = false);

    // メニューバーの高さ(画面の上から、メニューバーの下端まで)。ゲーム側の入力の判定に使える。
    float GetMenuBarBottom() const { return m_menuBarBottom; }

    // ゲームビュー(中央の枠。ゲームの絵はここだけに描く)の位置と大きさ(ウィンドウのクライアント座標のピクセル)。
    // BeginFrame の後に呼ぶと、今フレームの値。まだドックスペースが無い時はfalse(引数は変えない)。
    bool GetGameViewRect(float& x, float& y, float& width, float& height) const;

    // ゲームビューの中心のピクセル位置(ウィンドウのクライアント座標)。取れない時はfalse。
    bool GetGameViewCenter(float& x, float& y) const;

    // 次のフレームで、パネルを初期配置(Unity風)に戻す。
    void RequestLayoutReset() { m_layoutResetRequested = true; }

private:
    void DrawMenuBar(const FrameInfo& info);
    void DrawDockSpace(const FrameInfo& info);
    void BuildDefaultLayout(unsigned int dockId, float width, float height);
    void DrawHoverHint(const FrameInfo& info);
    void DrawShortcutsWindow();

    EditorSettings m_settings;
    bool* m_shortcutsPanel = nullptr;
    unsigned int m_dockId = 0;

    float m_menuBarBottom = 0.0f;
    bool m_layoutResetRequested = false;

    // ヒント: カーソルが同じ場所に0.3秒以上いた時だけ出す(通り過ぎる時にちらつかないように)。
    std::string m_hoverContext;
    double m_hoverSince = 0.0;

    // 保存の間引き。
    std::string m_lastSavedText;
    int m_framesSinceSaveCheck = 0;
};
