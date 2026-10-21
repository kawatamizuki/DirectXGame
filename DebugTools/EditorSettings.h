#pragma once
#include <deque>
#include <string>
#include "Json.h"

// エディタ(デバッグ用パネル群)の設定。パネルの表示/非表示、ヒントの表示、各システムが登録した
// 「エディタ機能のON/OFF」のスイッチを持ち、Settings/editor.json に保存・復元する。
// ImGuiや画面には依存しない(単体テストできる)。メニューやパネルの描画は EditorUi が行う。
//
// 保存する物は「個人の好み」だけ(パネルの配置はImGuiが imgui.ini に保存する)。
// ファイルが無い・壊れている・知らない項目がある・型が違う時は、その項目だけ既定値のまま続ける(落ちない)。
class EditorSettings
{
public:
    // パネル1つ分。nameはImGuiのウィンドウ名と同じ(保存のキーにも使う)。
    struct Panel
    {
        std::string name;
        bool open = true;
        bool defaultOpen = true;
        bool persist = true;      // false なら、設定ファイルに保存せず、いつも既定の開閉で始める(中身が空では意味が無いウィンドウ用)
    };

    // 「エディタ機能のON/OFF」の1項目。flagは持ち主(各システム)が持つ変数を指す(借りるだけ)。
    struct Toggle
    {
        std::string id;          // 保存のキー(英数字。ラベルを変えても保存が壊れない)
        std::string label;       // メニューに出す名前
        std::string description; // 短い説明(メニューの項目にカーソルを合わせると出る)
        bool* flag = nullptr;
        bool defaultValue = true;
    };

    // パネルを登録する(同じ名前が既にあればそれを返す)。返すポインタは、このオブジェクトが生きている間ずっと有効
    // (deque なので、後から登録しても住所が変わらない)。ImGui::Begin(name, ptr) にそのまま渡せる。
    // persist=false のパネルは、開閉を保存・復元しない(いつも defaultOpen で始まる)。
    bool* AddPanel(const std::string& name, bool defaultOpen, bool persist = true);

    // 名前でパネルを探す。無ければnullptr。
    bool* FindPanel(const std::string& name);

    const std::deque<Panel>& Panels() const { return m_panels; }

    // スイッチを登録する。登録した時点の *flag の値を既定値として覚える。flagがnullptrか、同じidが既にあれば何もしない。
    void RegisterToggle(const std::string& id, const std::string& label, bool* flag, const std::string& description);

    const std::deque<Toggle>& Toggles() const { return m_toggles; }

    // 全てのパネルを既定の開閉に戻す(「レイアウトを初期化」で使う)。スイッチとヒントは変えない。
    void ResetPanels();

    // ホバーのヒントを出すか(Editorメニューの「ヒントを表示」)。
    bool showHints = true;

    // ---- 保存 / 復元 ----
    Json ToJson() const;
    // 登録済みのパネル・スイッチにだけ値を反映する(知らない名前は無視、型違いは無視)。
    void ApplyJson(const Json& json);

    // pathへ書く(フォルダが無ければ作る。一時ファイルに書いてから置き換える)。失敗したらfalse。
    bool Save(const std::string& path) const;
    // pathを読んで反映する。ファイルが無い・壊れている時はfalse(設定は変わらない)。
    bool Load(const std::string& path);

    // 保存するJSON文字列(変更の検出に使う)。
    std::string Serialize() const { return ToJson().Dump(true); }

    static constexpr int kFormatVersion = 1;

private:
    std::deque<Panel> m_panels;
    std::deque<Toggle> m_toggles;
};
