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
            { "Global", "Esc / 右クリック", "キャンセル(メニューを閉じる・配置の取り消し)" },

            // ---- Playing(街づくり。実際にexeで遊ぶ状態)。カメラは、平行移動とズームの2つ ----
            { "Playing", "左クリック",    "メニューのボタンを押す / 建物を置く / 道路を引く" },
            { "Playing", "R",            "建物の向きを回す(90度ずつ)" },
            { "Playing", "Esc",          "メニューを開く・閉じる(配置中は、配置をやめる)" },
            { "Playing", "右クリック",    "メニューを閉じる / 配置をやめる(置きかけの道路が先)" },
            { "Playing", "W A S D / 矢印", "カメラを平行移動" },
            { "Playing", "Shift",        "平行移動を速く" },
            { "Playing", "ホイール",      "ズーム(カーソルの下の地面を中心に)" },
            { "Playing", "中ボタンのドラッグ", "地面をつかんで動かす" },

            // ---- Editing(オブジェクトの編集)。カメラは、自由に飛び回れるフリーカメラ ----
            { "Editing", "左クリック",    "オブジェクトを選ぶ / ギズモをドラッグ" },
            { "Editing", "W",            "移動ギズモ" },
            { "Editing", "E",            "拡大縮小ギズモ" },
            { "Editing", "R",            "回転ギズモ" },
            { "Editing", "Ctrl+Z",       "元に戻す(Undo)" },
            { "Editing", "Ctrl+Y",       "やり直す(Redo)" },
            { "Editing", "右ドラッグ",    "カメラ: 視点を回す" },
            { "Editing", "右ボタン+W A S D", "カメラ: 前後左右へ" },
            { "Editing", "Q / E",        "カメラ: 下 / 上へ" },
            { "Editing", "Shift",        "カメラ: 3倍速に" },
            { "Editing", "F",            "選択中のオブジェクトへカメラを寄せる" },

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

            // ---- Audio(音の一覧と音量) ----
            { "Audio", "Play / Stop",         "試し聴き(音量は全体・種類の設定が効く)" },
            { "Audio", "音量の欄",           "+/-ボタンか数字の入力、スライダー(Ctrl+クリックで入力)" },
            { "Audio", "音の表に保存",       "音ごとの音量を Sounds/sounds.json へ書き戻す" },
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
            { "Playing",   "ゲーム画面: Playing(街づくり)とカメラ" },
            { "Editing",   "ゲーム画面: Editing(オブジェクト編集)とフリーカメラ" },
            { "Hierarchy", "Hierarchy パネル" },
            { "Inspector", "Inspector パネル" },
            { "Build",     "Build パネル" },
            { "Assets",    "Assets パネル(素材の一覧)" },
            { "Asset Viewer", "Asset Viewer ウィンドウ(モデルを並べて見る)" },
            { "Audio", "Audio パネル(音の一覧と音量)" },
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
