#pragma once
#include <string>
#include <vector>
#include "UISkin.h"

// ---- ゲーム内メニュー(カイロソフト風) ----
// 右下の「メニュー」ボタンを押すと、ゲーム内の時間が止まり、左に縦の項目リスト(建築・情報・研究・システム)と、
// 選んだ項目の中身が出る。「戻る」(同じ場所のボタン)で閉じて、時間が動き出す。
// このクラスは「配置(Layout)」と「入力(どのボタンを押したか)」だけを持つ。ゲームの中身(建物の定義・所持金・
// 配置ツール)は知らず、毎フレーム渡される MenuModel だけを見て並べ、押された結果は MenuCommand で返す。
// 描くのはIUISkin。ゲーム側(Game)が MenuModel を作り、MenuCommand を実際の処理につなぐ。

// メニューが「押された」ことをゲーム側へ伝える命令。
enum class MenuCommandType
{
    None,
    SelectBuilding,     // arg = BuildingType(を整数にした値)。建物の配置を始める
    SelectRoad,         // arg = RoadType。道路の配置を始める
    SetRoadDrawMode,    // arg = RoadDrawMode。道路の引き方(直線/自由)を切り替える
    CancelPlacement,    // 配置を、やめる
    Save,
    Load
};

struct MenuCommand
{
    MenuCommandType type = MenuCommandType::None;
    int arg = 0;
};

// 建築タブの1つの項目(建物・道路の種類・引き方)。
struct MenuItem
{
    UIIcon icon = UIIcon::House;
    std::string name;           // 「住宅」
    std::string detail;         // 右に出す補足(「100円」「20円/マス」)。空なら出さない
    bool affordable = true;     // 払えるか(払えない時は、補足を警告の色にする。押せないわけではない)
    bool selected = false;      // 今、選ばれているか(今の配置ツール・今の引き方)
    MenuCommand command;
};

// 情報タブの1行。
struct MenuInfoLine
{
    std::string label;          // 「人口」
    std::string value;          // 「120」
};

// メニューに出す内容。毎フレーム、ゲーム側が作って渡す。
struct MenuModel
{
    std::vector<MenuItem> buildings;
    std::vector<MenuItem> roads;
    std::vector<MenuItem> drawModes;
    std::vector<MenuInfoLine> info;
    std::string activeToolLabel;    // 配置中のものの名前(「住宅」)。空なら、配置中ではない
    std::string statusText;         // システムタブに出す、直前のセーブ/ロードの結果
};

// マウスの状態と画面の大きさ。座標は、描画先(ゲームビュー)の左上からのピクセル。
struct MenuInput
{
    float screenW = 1920.0f;
    float screenH = 1080.0f;
    float topInset = 0.0f;          // 上部HUDの下端(メニューは、これより下に出す)
    float mouseX = -1.0f;
    float mouseY = -1.0f;
    bool pressed = false;           // 左ボタンを押したフレーム
    bool down = false;              // 左ボタンを押している間
    bool enabled = true;            // メニューを使える状態か(Playingの時だけtrue。falseなら、閉じて何も出さない)
    bool blockedByOtherUI = false;  // デバッグGUI(ImGui)の上にカーソルがある(反応しない)
};

struct MenuResult
{
    bool mouseOverUI = false;       // カーソルがメニューの上にあるか(開いている間は、画面全体)
    bool consumedClick = false;     // このフレームのクリックをメニューが受け取ったか
    MenuCommand command;            // 押された命令(無ければNone)
};

class GameMenu
{
public:
    enum class Tab
    {
        Build,
        Info,
        Research,
        System,
        Count
    };

    enum class ButtonKind
    {
        Toggle,     // 右下の「メニュー」/「戻る」
        Tab,        // 左のリストの項目
        Item,       // 中身の項目(建物・道路・セーブなど)
        Cancel      // 「やめる」(配置中のしるしの横)
    };

    // 1つのボタンの配置結果。
    struct Button
    {
        UIRect rect;
        ButtonKind kind = ButtonKind::Item;
        Tab tab = Tab::Build;           // kind==Tabの時、どのタブか
        std::string label;
        std::string detail;             // 右寄せで出す補足
        UIIcon icon = UIIcon::House;
        bool selected = false;
        bool affordable = true;
        bool enabled = true;
        bool centerLabel = false;       // trueなら、文字を中央に(「メニュー」「戻る」)
        MenuCommand command;
        bool closesMenu = false;        // 押したら、メニューを閉じるか
    };

    // 文字1つの配置結果(見出し・情報の行・準備中の案内)。
    struct Text
    {
        float x = 0.0f;                 // alignRightなら、右端のx
        float y = 0.0f;
        float size = 0.0f;
        std::string text;
        UITextRole role = UITextRole::Normal;
        bool alignRight = false;
    };

    // 1フレームぶんの配置結果。テストで「はみ出し・重なりが無いか」を確かめられるよう公開している。
    // 文字の幅は測らずに決められる配置だけにしてあるので、描画の準備(UIRenderer)が無くても計算できる。
    struct Layout
    {
        float scale = 1.0f;
        float screenW = 0.0f;
        float screenH = 0.0f;
        bool enabled = false;
        bool open = false;
        UIRect overlay;                 // 開いている間、後ろを暗くする幕(HUDの下から画面の下端まで)
        UIRect list;                    // 左の項目リストの枠
        UIRect content;                 // 選んだ項目の中身の枠
        UIRect activePill;              // 閉じている時、配置中のしるし(幅0なら出ない)
        std::vector<Button> buttons;
        std::vector<Text> texts;
        float labelSize = 0.0f;         // ボタンの文字の大きさ
        float detailSize = 0.0f;
    };

    // 配置を計算し、押された物を処理する。ワールドへの入力処理より先に、毎フレーム呼ぶ。
    MenuResult Update(const MenuInput& input, const MenuModel& model);

    // Updateで計算した配置で描く。UIRendererのBeginFrame〜EndFrameの間に呼ぶ。
    void Draw(UIRenderer& ui, IUISkin& skin) const;

    // 配置だけを計算する(Updateの内部と、テスト用)。open/tabは今の状態を使う。
    Layout ComputeLayout(const MenuInput& input, const MenuModel& model) const;

    bool IsOpen() const { return m_open; }
    void Open() { m_open = true; }
    void Close() { m_open = false; }
    Tab GetTab() const { return m_tab; }
    void SetTab(Tab tab) { m_tab = tab; }

    const Layout& GetLayout() const { return m_layout; }

private:
    // カーソルの下のボタンの番号(無ければ-1)。
    int FindButtonAt(float x, float y) const;
    UIButtonState StateOf(const Button& button, int index) const;

    bool m_open = false;
    Tab m_tab = Tab::Build;
    Layout m_layout;
    int m_hover = -1;       // 前回のUpdateで、カーソルの下にあったボタン
    bool m_down = false;    // 前回のUpdateで、左ボタンを押していたか
};
