#pragma once
#include <string>
#include <vector>

// キー操作の一覧(1か所に集めたデータ)。「ウィンドウにカーソルを合わせた時のヒント」と「ショートカット一覧ウィンドウ」の
// 両方がこの表を使うので、キーを変えた時は、ここだけ直せば両方に反映される。
// 画面・ImGui・入力には依存しない(単体テストできる)。
struct ShortcutEntry
{
    const char* context;     // どの場面の操作か(ウィンドウ名、または "Global" / "Game" など)
    const char* keys;        // 表示するキー(例: "F2"、"Ctrl+Z"、"右ドラッグ")
    const char* description; // 短い説明(1行)
};

namespace ShortcutTable
{
    // 全ての項目(表示順)。
    const std::vector<ShortcutEntry>& All();

    // contextが一致する項目だけ(表示順)。
    std::vector<const ShortcutEntry*> ForContext(const std::string& context);

    // 項目に出てくる文脈の一覧(重複なし、初めて出てきた順)。ショートカット一覧ウィンドウの見出し用。
    std::vector<std::string> Contexts();

    // 文脈の見出し(日本語の説明つき)。未知の文脈ならその名前をそのまま返す。
    std::string ContextTitle(const std::string& context);

    // ImGuiのウィンドウ名から、ヒントを出す文脈を決める。ヒントのある文脈ならその名前、無ければ空文字。
    // ウィンドウ名は、ドッキング中のウィンドウの子ウィンドウ名("Assets/##xxx" など)が来ても、先頭の名前で判定する。
    std::string ContextForWindowName(const std::string& windowName);
}
