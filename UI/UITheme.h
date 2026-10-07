#pragma once
#include <DirectXMath.h>

// UIの見た目(色・角丸・枠・文字サイズ)を1か所に集約する。
// 見た目の調整や、画風の差し替え(ポップ→ドット絵など)はこの構造体の値を変えるだけで済むようにして、
// 配置・入力・描画の処理側には色の数値を直接書かない。
// 長さの単位は「画面の高さ1080を基準にしたピクセル」で、実際に描く時にGameUIが画面サイズに応じた倍率をかける。
struct UITheme
{
    using Color = DirectX::XMFLOAT4;

    // ---- パネル(上部のバーなど) ----
    Color panelTop{ 1.00f, 0.97f, 0.88f, 1.0f };      // クリーム色(上)
    Color panelBottom{ 0.99f, 0.90f, 0.72f, 1.0f };   // 少し濃いクリーム色(下)
    Color panelBorder{ 0.45f, 0.27f, 0.13f, 1.0f };   // 茶色の太い枠
    Color panelShadow{ 0.20f, 0.10f, 0.05f, 0.35f };  // パネルの下に落とす影
    float panelBorderThickness = 3.0f;

    // ---- チップ(数字を入れる小さな角丸の箱) ----
    Color chipFill{ 1.00f, 1.00f, 0.96f, 1.0f };
    Color chipBorder{ 0.45f, 0.27f, 0.13f, 1.0f };
    float chipBorderThickness = 2.0f;
    float chipRadius = 10.0f;

    // ---- ボタン ----
    Color buttonNormalTop{ 1.00f, 0.98f, 0.90f, 1.0f };
    Color buttonNormalBottom{ 0.96f, 0.86f, 0.66f, 1.0f };
    Color buttonHoverTop{ 1.00f, 1.00f, 0.82f, 1.0f };
    Color buttonHoverBottom{ 1.00f, 0.92f, 0.60f, 1.0f };
    Color buttonPressedTop{ 0.88f, 0.72f, 0.48f, 1.0f };
    Color buttonPressedBottom{ 0.96f, 0.82f, 0.58f, 1.0f };
    Color buttonSelectedTop{ 1.00f, 0.75f, 0.30f, 1.0f };    // 選択中: オレンジ
    Color buttonSelectedBottom{ 0.96f, 0.55f, 0.18f, 1.0f };
    Color buttonBorder{ 0.45f, 0.27f, 0.13f, 1.0f };
    float buttonBorderThickness = 2.0f;
    float buttonRadius = 9.0f;

    // ---- 文字・アイコン ----
    Color textColor{ 0.30f, 0.17f, 0.08f, 1.0f };           // 濃い茶色
    Color textOutline{ 1.00f, 1.00f, 1.00f, 0.85f };        // 白い縁取り(背景が変わっても読める)
    Color subTextColor{ 0.50f, 0.34f, 0.20f, 1.0f };
    Color iconColor{ 0.35f, 0.20f, 0.10f, 1.0f };
    Color iconSelectedColor{ 1.00f, 1.00f, 1.00f, 1.0f };   // 選択中のボタンの上のアイコン
};
