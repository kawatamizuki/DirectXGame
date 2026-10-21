#include "ShortcutTable.h"
#include <cstring>

namespace
{
    // キー割り当ての出どころ:
    //   InputManager のコンストラクタ(Decide/Cancel/Rotate、Editor用のカメラ・ギズモ・Undo/Redo・フォーカス)、
    //   Game::Update のF2、Game::HandleScenarioHotkeys のF5/F9。
    // キーを変えたらここも直す(ヒントとショートカット一覧は、この表だけを見る)。
    const std::vector<ShortcutEntry>& Table()
    {
        static const std::vector<ShortcutEntry> table =
        {
            // ---- どこでも ----
            { "Global", "F2",            "Assets(素材の一覧)を開く/閉じる" },
            { "Global", "F5",            "クイックセーブ(ゲームシーン)" },
            { "Global", "F9",            "クイックロード(ゲームシーン)" },
            { "Global", "Esc / 右クリック", "キャンセル(建物・道路の配置の取り消し)" },

            // ---- ゲーム画面(中央)。カメラは Editing / Playing のどちらでも使える ----
            { "Game", "右ドラッグ",       "視点を回す" },
            { "Game", "右ボタン+W A S D", "カメラを前後左右へ" },
            { "Game", "Q / E",           "カメラを下 / 上へ" },
            { "Game", "Shift",           "カメラ移動を3倍速に" },
            { "Game", "F",               "選択中のオブジェクトへカメラを寄せる" },

            // ---- Playing(街づくり) ----
            { "Playing", "左クリック",    "建物を置く / 道路を引く・確定する" },
            { "Playing", "R",            "建物の向きを回す(90度ずつ)" },
            { "Playing", "右クリック / Esc", "配置の途中をキャンセル" },

            // ---- Editing(オブジェクトの編集) ----
            { "Editing", "左クリック",    "オブジェクトを選ぶ / ギズモをドラッグ" },
            { "Editing", "W",            "移動ギズモ" },
            { "Editing", "E",            "拡大縮小ギズモ" },
            { "Editing", "R",            "回転ギズモ" },
            { "Editing", "Ctrl+Z",       "元に戻す(Undo)" },
            { "Editing", "Ctrl+Y",       "やり直す(Redo)" },

            // ---- パネル ----
            { "Hierarchy", "クリック",    "オブジェクトを選ぶ" },
            { "Inspector", "ドラッグ",    "数値を変える(Ctrl+クリックで入力)" },
            { "Build",     "R",          "建物の向きを回す" },
            { "Build",     "クリック",    "建物・道路のツールを選ぶ" },

            // ---- Assets(軽い一覧)と Asset Viewer(別ウィンドウ) ----
            { "Assets", "クリック",           "モデル・フォルダを選ぶ" },
            { "Assets", "ダブルクリック / V", "ビューワーで開く" },
            { "Assets", "右クリック",         "メニュー(ビューワー・場所を開く・パスをコピー)" },
            { "Assets", "フォルダをドラッグ", "別のフォルダの中へ移動" },
            { "Asset Viewer", "左ドラッグ",    "回転" },
            { "Asset Viewer", "中/右ドラッグ", "平行移動" },
            { "Asset Viewer", "ホイール",      "ズーム" },
            { "Asset Viewer", "札をクリック",  "モデルの情報を見る" },
        };
        return table;
    }
}

namespace ShortcutTable
{
    const std::vector<ShortcutEntry>& All()
    {
        return Table();
    }

    std::vector<const ShortcutEntry*> ForContext(const std::string& context)
    {
        std::vector<const ShortcutEntry*> result;
        for (const ShortcutEntry& entry : Table())
        {
            if (context == entry.context)
            {
                result.push_back(&entry);
            }
        }
        return result;
    }

    std::vector<std::string> Contexts()
    {
        std::vector<std::string> result;
        for (const ShortcutEntry& entry : Table())
        {
            bool found = false;
            for (const std::string& existing : result)
            {
                if (existing == entry.context)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                result.push_back(entry.context);
            }
        }
        return result;
    }

    std::string ContextTitle(const std::string& context)
    {
        struct Title { const char* context; const char* title; };
        static const Title titles[] =
        {
            { "Global",    "どこでも" },
            { "Game",      "ゲーム画面(中央): カメラ" },
            { "Playing",   "ゲーム画面: Playing(街づくり)" },
            { "Editing",   "ゲーム画面: Editing(オブジェクト編集)" },
            { "Hierarchy", "Hierarchy パネル" },
            { "Inspector", "Inspector パネル" },
            { "Build",     "Build パネル" },
            { "Assets",    "Assets パネル(素材の一覧)" },
            { "Asset Viewer", "Asset Viewer ウィンドウ(モデルを並べて見る)" },
        };
        for (const Title& title : titles)
        {
            if (context == title.context)
            {
                return title.title;
            }
        }
        return context;
    }

    std::string ContextForWindowName(const std::string& windowName)
    {
        // 子ウィンドウやポップアップの名前は "親の名前/子の名前" になるので、先頭の名前だけを見る。
        std::string head = windowName.substr(0, windowName.find('/'));
        if (head.empty())
        {
            return std::string();
        }
        for (const ShortcutEntry& entry : Table())
        {
            if (head == entry.context)
            {
                return head;
            }
        }
        return std::string();
    }
}
