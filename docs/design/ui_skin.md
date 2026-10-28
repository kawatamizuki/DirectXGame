# UIのスキン(見た目の差し替え)設計

2026-10-22。ゲーム内UI(上部HUD・ゲーム内メニュー)の「見た目」を、後からテクスチャ(画像)で丸ごと置き換えられるようにするための設計。

## 1. 目的

- 今のUIは、図形(角丸・円・線・三角)と文字だけで描いている(素材なし)。
- 後で、画像素材(ドット絵・手描きの枠・アイコン)に差し替えたい。**その時に、配置・入力・ゲームとの接続の処理を、書き換えなくて済むようにする。**

## 2. 層の分け方

| 層 | 担当 | ファイル | 見た目を変える時 |
|---|---|---|---|
| ゲームとの接続 | 何を表示するか(`MenuModel`・`HudState`)を作り、押された結果(`MenuCommand`)を処理に繋ぐ | `Core/Game.cpp` | 触らない |
| 配置と入力 | どこに何を置くか、どのボタンが押されたか | `UI/GameUI.*`、`UI/GameMenu.*` | 触らない(寸法を変えたい時だけ) |
| **見た目(スキン)** | 箱・ボタン・アイコン・文字の色の「描き方」 | `UI/UISkin.h`(インターフェース)、`UI/ProceduralUISkin.*`(今の図形版) | **ここだけ差し替える** |
| 描画の土台 | 頂点を溜めて1回で描く | `UI/UIRenderer.*`、`UI/FontAtlas.*` | 画像を描く機能を足す(§5) |

配置・入力の側は、色や画像を一切知らない。「ここに、Windowの箱」「ここに、Selected状態のボタン」「ここに、Sunのアイコン」と、**種類(enum)と位置・大きさ**だけをスキンに渡す。

## 3. IUISkin(UI/UISkin.h)

```
DrawPanel(ui, rect, UIPanelStyle, scale)     箱: Bar / Window / Chip / Tag / TagWarning / Frame / Overlay
DrawButton(ui, rect, UIButtonState, scale)   ボタンの背景: Normal / Hover / Pressed / Selected / Disabled
DrawSlider(ui, track, value01, state, scale) スライダー(音量): みぞ・塗り・つまみ。つまみの両端は、trackの左右からtrack.h/2内側(入力側の値の計算と同じ決まり)
DrawGauge(ui, rect, value01, marker01, warning, scale) ゲージ(需要・体力など、量を見せる横棒)。目印(充足のしきい値など)と、警告の色
DrawIcon(ui, UIIcon, cx, cy, size, state)    アイコン: Sun, Moon, Coin, Build, Info, Option(スピーカー), House, RoadNarrow ...
GetTextStyle(UITextRole)                     文字の書式: Normal / Sub / Heading / Warning / Disabled / Outlined ...
```

- `scale`は、画面の高さに応じた倍率(0.75〜2.0)。枠の太さ・角丸・影の大きさにかける。
- ボタンの**文字・アイコンは、スキンがボタンの背景を描いた後に、呼び出し側が重ねる**。文字の位置は呼び出し側(`GameMenu`)が決める。
- データから決まる色(タイムラインの空の色)は、スキンではなく`GameUI`が塗る。その外枠(`Frame`)と、今の時刻の目印(`TimeMarker`アイコン)は、スキンが描く。

## 4. 差し替えの手順(テクスチャ版を作る時)

1. `UI/TextureUISkin.{h,cpp}` を作り、`IUISkin`を継承する。
2. 画像素材の対応表を持つ(JSONなど。`UIPanelStyle`・`UIButtonState`・`UIIcon`のそれぞれが、どの画像のどの範囲か)。
3. `DrawPanel` / `DrawButton`: **9スライス**で描く(四隅は拡大せず、辺と中央だけを引き伸ばす)。`scale`で、四隅の大きさも変える。
4. `DrawIcon`: 該当の画像を、中心(cx,cy)・size四方に描く。無いIDは、`ProceduralUISkin`に任せる(フォールバック)ようにすると、素材を少しずつ増やせる。
5. `GetTextStyle`: 文字の色・縁取りを、素材の雰囲気に合わせて返す。
6. `Game::Initialize`で、`m_gameUI.SetSkin(&m_textureSkin)` を呼ぶ。**これだけで、HUDとメニューの見た目が全て切り替わる。**

## 5. 先に必要になるもの(まだ無い)

- **`UIRenderer`の画像描画**: 今は、図形も文字も、1枚のアトラス(`FontAtlas`。左上に白い領域)だけを使う。画像を描くには、`DrawImage(テクスチャ, 描く四角, 元画像の範囲, 色)`と、素材を詰めたアトラス(または複数テクスチャ)の扱いが要る。`UIRenderer`の説明にもあるとおり、同じアトラスに画像を詰める形で拡張できる想定。ドット絵は、`SetPointSampling(true)`で、くっきり拡大できる。
- **寸法(余白・ボタンの高さ)**: 画像素材によって、枠の内側の余白が変わる。今は`GameMenu`の定数。必要になったら、スキンが「余白の寸法」を返す(`GetMetrics()`)形にする。
- **フォント**: 今は`FontAtlas`が1つのフォントを焼く。素材のフォントに変えるなら、`FontAtlas`の初期化に渡す。

## 6. 今のスキン(ProceduralUISkin)

- 色・太さ・角丸は`UITheme`(UI/UITheme.h)に集約。`GameUI::GetTheme()`で調整できる。
- アイコンは、全て図形で描く(道路は太さの違い、フラスコ、歯車、フロッピーなど)。
- 選択中は、アイコンが白くなる。白だと見えなくなる「抜き」(「i」の文字・歯車の穴)は、選択中のボタンの色で抜く。

## 7. 決めたこと・割り切り

- スキンは、`GameUI::SetSkin`で、**1つだけ**差し替える(部品ごとに別のスキンにはしない)。
- スキンは、描く(出力)だけ。クリックの当たり判定は、配置(`GameMenu`のボタンの四角)で行い、スキンに聞かない。
- ホバー・押している状態の見た目は、スキンが`UIButtonState`で決める。アニメーション(ふわっと出る・拡大)は未対応。
