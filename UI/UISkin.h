#pragma once
#include "UIRenderer.h"

// ---- UIの「見た目」を差し替えるための土台(スキン) ----
// HUD(GameUI)とメニュー(GameMenu)は、「どこに何を置くか(配置)」「何を押したか(入力)」だけを自分で持ち、
// 箱・ボタン・アイコン・文字の色の「描き方」は、全てこのIUISkinを通して描く。
// 今の見た目は、図形で描く ProceduralUISkin。後で、テクスチャ(画像)で描く別のスキンを作って
// GameUI::SetSkin に渡せば、配置・入力の処理には1行も手を入れずに、見た目を丸ごと置き換えられる。
// (テクスチャ版を作る時の手順は docs/design/ui_skin.md に書いてある)

// ボタンの状態。スキンは、状態ごとに別の絵(色・画像)を使う。
enum class UIButtonState
{
    Normal,
    Hover,
    Pressed,
    Selected,   // 選ばれている(今のタブ・今の建設ツールなど)
    Disabled
};

// 箱の種類。種類ごとに、スキンが違う絵を使う(9スライスのテクスチャを、種類ごとに用意する想定)。
enum class UIPanelStyle
{
    Bar,         // 画面の端の全幅のバー(上部HUD)
    Window,      // メニューの大きな枠
    Chip,        // 数字を入れる小さな角丸の箱
    Tag,         // カーソルの横の小さなタグ
    TagWarning,  // 同上(払えない時。赤い枠)
    Frame,       // 中身(タイムラインなど)を囲む細い枠。中は塗らない
    Overlay      // メニューを開いた時に、後ろのゲーム画面を暗くする幕
};

// アイコンの種類。スキンは、IDごとに自分で絵を決める(図形で描く・画像を貼る)。
// 呼び出し側は、IDと「中心の位置・大きさ」だけを渡す。
enum class UIIcon
{
    // HUD
    Sun,
    Moon,
    Coin,
    Population,
    Jobs,
    Residents,
    TimeMarker,   // タイムラインの、今の時刻の目印。sizeはタイムラインの高さ(縦に伸びる線)

    // メニュー
    Menu,
    Back,
    Build,
    Info,
    Research,
    System,
    Save,
    Load,
    Option,       // オプション(スピーカー)

    // 建設の項目
    House,
    Office,
    Shop,
    RoadNarrow,
    RoadNormal,
    RoadLarge,
    RoadStraight,
    RoadFreehand
};

// 文字の役割。色・縁取りなどの書式は、スキンが役割ごとに決める(呼び出し側は、色の数値を持たない)。
enum class UITextRole
{
    Normal,         // ふつうの文字
    Sub,            // 補足の小さい文字
    Heading,        // 見出し
    Warning,        // 警告(お金が足りない等)
    Disabled,       // 押せない・準備中
    Outlined,       // 縁取り付き(背景が変わっても読める。時刻など)
    OutlinedSub     // 同上の、補足の文字
};

class IUISkin
{
public:
    virtual ~IUISkin() = default;

    // 箱を描く。scaleは、画面の大きさに応じた倍率(枠の太さ・角丸・影の大きさにかける)。
    virtual void DrawPanel(UIRenderer& ui, const UIRect& rect, UIPanelStyle style, float scale) = 0;

    // ボタンの背景を描く(文字・アイコンは描かない。呼び出し側が、DrawIcon/DrawStringで重ねる)。
    virtual void DrawButton(UIRenderer& ui, const UIRect& rect, UIButtonState state, float scale) = 0;

    // スライダー(音量など)を描く。trackは、つかめる範囲の四角。value01は、つまみの位置(0〜1)。
    // つまみの中心は、trackの左右からtrack.h/2だけ内側を両端として動く(入力側の値の計算と合わせるための決まり)。
    // stateは、Hover=カーソルが上、Pressed=つかんでいる。
    virtual void DrawSlider(UIRenderer& ui, const UIRect& track, float value01, UIButtonState state, float scale) = 0;

    // アイコンを、中心(cx, cy)に、size四方の大きさで描く。stateは、選択中・押せない時に色を変えるため。
    virtual void DrawIcon(UIRenderer& ui, UIIcon icon, float cx, float cy, float size, UIButtonState state) = 0;

    // 文字の書式(色・縁取り・影)。
    virtual UITextStyle GetTextStyle(UITextRole role) const = 0;

    // 文字を描く時の色など以外に、タイムラインなど「データから決まる色」は、呼び出し側が描く。
    // それらの外枠や目印は、DrawPanel(Frame)/DrawIcon(TimeMarker)を通す。
};
