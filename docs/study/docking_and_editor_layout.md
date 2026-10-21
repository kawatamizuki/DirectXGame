# ドッキング・Editorメニュー・ホバーのヒント(勉強資料)

2026-10-21。Unityのように、パネルを整理して並べる土台と、「Editor」メニュー、ウィンドウにカーソルを合わせた時のキーのヒントを作った時のまとめ。

## 1. 用語

| 用語 | 意味 |
|---|---|
| ドッキング(docking) | パネル(ウィンドウ)を、別のパネルの横・上下・タブに吸着させる機能。Unityのウィンドウの並べ方と同じ。 |
| ドックスペース(DockSpace) | パネルを吸着させる「土台の領域」。ImGuiでは `ImGui::DockSpace(id)` で作る。 |
| ドックノード(DockNode) | ドックスペースを分割してできた1つの区画。区画の中にタブとして複数のパネルが入る。 |
| 中央ノード(CentralNode) | 分割の最後に残る区画。ここにはパネルを吸着させず、**ゲームの絵を見せる**ために空けておく。 |
| PassthruCentralNode | 中央ノードを「透明で、マウスも通す」にする設定。後ろに描いたゲームの画面がそのまま見え、中央の上のクリックはゲームに届く。 |
| DockBuilder | 初期の並べ方をコードで作るAPI(`imgui_internal.h`)。「左を19%、右を24%、下を28%」のように分割していく。 |
| imgui.ini | ImGuiが自動で保存する、ウィンドウの位置・大きさ・ドッキング状態のファイル。 |
| docking ブランチ | Dear ImGui の本家(master)に、ドッキングとマルチビューポートを足した枝。タグ `v1.92.8-docking` で版を固定した。 |
| マルチビューポート | パネルを本物の別OSウィンドウにする機能。複雑になるので使っていない(無効のまま)。 |
| 前景描画リスト(ForegroundDrawList) | 全てのウィンドウより手前に、描くだけ(入力は奪わない)ための描画リスト。ヒントの箱に使った。 |
| ホバー(hover) | カーソルを乗せている状態。 |

## 2. 何をどう作ったか

### 2.1 ImGuiを docking 版へ
- 同じ作者の公式リポジトリの**同じバージョン番号のタグ** `v1.92.8-docking` を取得して `imgui/` を差し替えた。今まで使っていたAPI(ボタン、スライダー、`Image` など)は全く同じなので、コードの修正はほぼ要らない。
- 有効にするのは1行: `io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;`
- あわせて `io.ConfigWindowsMoveFromTitleBarOnly = true;`。ウィンドウの中身(スライダーなど)をドラッグした時に、ウィンドウごと動いてしまうのを防ぐ(動かせるのはタイトルバーだけ)。

### 2.2 ドックスペースの土台(`EditorUi::DrawDockSpace`)
```
上部のHUD(ゲームの独自UI) ← hudTopInset の高さ
Editorメニューバー          ← 約24px
ドックスペース(残り全部)
```
- 土台は「背景なし・動かせない・手前に出てこない」ウィンドウ(`NoBackground | NoMove | NoBringToFrontOnFocus | NoDocking ...`)。この中で `DockSpace(id, 0, PassthruCentralNode | NoDockingOverCentralNode)` を呼ぶ。
- `NoDockingOverCentralNode` は、中央にパネルを吸着できなくする設定(中央をいつも空に保つ)。
- 初期配置は、**配置の情報が無い時だけ**作る: `if (DockBuilderGetNode(id) == nullptr) BuildDefaultLayout(...)`。imgui.ini に配置があれば、ImGuiが起動時に復元しているので、`GetNode` が値を返す=ユーザーが動かした配置をそのまま使う。
- 初期配置の作り方(分割は「切り出した側のID」と「残った側のID」を返す):
  ```cpp
  ImGuiID center = dockId;
  ImGuiID left   = DockBuilderSplitNode(center, ImGuiDir_Left,  0.19f, nullptr, &center);
  ImGuiID right  = DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
  ImGuiID bottom = DockBuilderSplitNode(center, ImGuiDir_Down,  0.28f, nullptr, &center);
  DockBuilderDockWindow("Hierarchy", left);   // ウィンドウ名でノードに入れる
  ```
  ウィンドウは「名前」で指定する。だから**ウィンドウ名を変えると、保存済みの配置が効かなくなる**(名前を定数にして共通にした)。

### 2.2.1 ゲームビュー(ゲームの絵を中央の枠だけに描く)
最初は、ゲームの絵を画面全体に描き、中央を透明にして見せていた(HUDがメニューやパネルの上に重なって見えた)。後で、Unityのように、ゲームの絵を**中央の枠(ゲームビュー)だけ**に描く形にした。
- 描画: `RSSetViewports` で、描く範囲を枠に絞る(`Renderer::SetGameViewRect`)。3D・デバッグ線・HUDは、ビューポートの中に収まる。HUDは、`UIRenderer` のビューポートの原点を枠の左上にして、枠の大きさで並べる。
- カメラ: 縦横比を、ウィンドウでなく枠の縦横比にする(毎フレーム `Camera::SetAspect`)。
- マウス: `InputManager::GetMousePosition` を、枠の左上が原点の座標にする。レイ・選択・ギズモ・HUDの当たり判定は、全てこの座標と、枠の大きさで計算する(`GetWindowWidth` を使っていた所を `GetViewWidth` に置き換えた)。
- ImGuiで3Dの位置に重ねる物(ギズモの軸の文字、道路の確認ボタン)は、ImGuiがウィンドウ全体の座標なので、枠の位置を足す。
- 順番: 枠の位置・大きさは、ドックスペースを出した直後(`Game::Update`)に今フレームの値で決め、その後の描画・入力に使う(Drawで決めると、1フレーム遅れて、サイズ変更中に絵がずれる)。
- テクスチャに描く方式は採らなかった(ビューポートの限定で足りる)。

### 2.3 Editorメニュー(`EditorUi::DrawMenuBar`)
- ImGuiの `BeginMainMenuBar` は画面の最上端に出て、ゲームのHUDを隠してしまう。なので、**HUDの直下に置いた細いウィンドウ**(`ImGuiWindowFlags_MenuBar`)の中で `BeginMenuBar` を使った。
- 項目: パネル(チェック付きの一覧)/ レイアウトを初期化 / ヒントを表示 / エディタ機能のON/OFF(登録されたスイッチの一覧)/ ショートカット一覧。
- パネルの開閉は、`ImGui::Begin(name, &open)` の `open` に、メニューのチェックと**同じ `bool` のアドレス**を渡して共有する。タイトルバーの×でも、メニューのチェックでも、同じ変数が変わる。

### 2.4 設定の保存(`EditorSettings`、`Settings/editor.json`)
- 保存するもの: パネルの表示/非表示、ヒントのON/OFF、各スイッチの状態。パネルの配置は imgui.ini(ImGuiの担当)。
- 「スイッチの登録」(`RegisterToggle(id, label, bool*, description)`)で、各システムが自分の `bool` の住所をメニューに渡す。メニューはその住所を読み書きするだけで、各システムのコードの中身を知らない(依存を逆にしている)。
- パネルの `bool` は `std::deque` に入れた。`std::vector` だと、要素を足した時に配列が引っ越して、渡したポインタが無効になる。`deque` は末尾に足しても既存の要素の住所が変わらない。
- 保存は一時ファイルに書いてから置き換える(書き込み中に落ちても前の設定が壊れない)。壊れたファイル・型違い・知らない項目は、その項目だけ既定値のまま続行する(落ちない)。
- 変更の検出は、120フレームごとに「保存する文字列」を作って、前回と比べる。毎フレーム書き込まない。

### 2.5 ホバーのヒント(`EditorUi::DrawHoverHint`、`ShortcutTable`)
- キー操作を**1か所の表**(`ShortcutTable`)にまとめ、ヒントとショートカット一覧の両方がこの表を見る。キーを変えた時に直す場所が1つになる。
- ホバーしているウィンドウは、ImGuiの内部状態 `context->HoveredWindow` から取る。**中央の透明部分の上では `HoveredWindow` が null** になる(パススルーの穴のため)。→ nullの時は「ゲーム画面のヒント」を出す(ゲームのHUDの上は除く)。
- 0.3秒以上同じ場所にいた時だけ出す(通り過ぎる時にちらつかないように)。
- 描画は前景描画リストに `AddText` / `AddRectFilled` するだけ。マウスもキーボードも奪わない。

## 3. 起きた問題と直し方

| 問題 | 原因 | 直し方 |
|---|---|---|
| メニューバーが、ドックのタブの上端に数px重なって隠した | ImGuiはウィンドウを `WindowMinSize`(既定32px)より低くしない。メニューバーは約24pxのつもりだったが、32pxになっていた | `PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1,1))` で下げた |
| 診断用の画面保存で、右側のパネルが写っていなかった | PowerShell が高DPI(拡大表示125%)に対応しておらず、画面の一部しか撮れていなかった。さらに、**別のウィンドウ(私的なもの)まで写り込んだ** | 画面全体の撮影はやめて、ゲーム自身が**バックバッファ(自分の描画結果)を直接PNGに保存**する診断コードにした(他のウィンドウは写らない) |
| 診断で送った「マウス位置」が効かない時がある | ImGuiのWin32バックエンドが、毎フレーム本物のカーソル位置で上書きしていた | バックエンドの更新の**後**、`ImGui::NewFrame` の**前**で、診断のマウス位置を送るようにした |
| PowerShell で `Remove-Item` が誤ってブロックされる / `getenv` が警告でビルド失敗 | 環境の癖 / MSVCの安全でない関数の警告 | bashの `rm`、`_dupenv_s` を使う |

## 4. 確認した方法
- 単体テスト(スクラッチ): `ShortcutTable`(全項目に文脈・キー・説明がある、同じ文脈でキーが重複しない、ウィンドウ名からの文脈の決定)、`EditorSettings`(登録、deque でポインタが変わらない、保存→読み込み→再保存で一致、無い/空/壊れた/型違い/BOM付き/保存先がフォルダ)。全て通った。
- ゲーム本体: 診断用コード(`GAME_DIAG` 環境変数で、シーン切り替え・マウス移動・クリックを再現)で、初期配置・ホバーのヒント(ゲーム画面/Hierarchy、PlayingとEditingで内容が変わる)・Editorメニュー(パネルの一覧、エディタ機能の一覧)・ヒントOFF後の非表示と `editor.json` への保存・パネルを閉じる→レイアウトの初期化、をバックバッファの画像で確認した。

## 5. 落とし穴チェックリスト
- ドックのウィンドウ名を変えない(imgui.ini の配置が別物になる)。
- ドックスペースの土台は、パネルを描くより**先に**呼ぶ(後だと、吸着したパネルが1フレーム隠れる)。
- `Begin(name, &open)` の `open` が false の時は `Begin` 自体を呼ばない(呼ぶと、閉じたはずのウィンドウが出る)。
- ImGuiに渡すポインタの持ち主は、ポインタより長生きさせる(`deque` や固定の変数)。
- 診断用の画面保存は、他のウィンドウが写り込まない方法(自分のバックバッファ)にする。
