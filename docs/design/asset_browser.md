# エディタと素材管理(アセットブラウザ・ビューワー・キャッシュ・取り込み)の設計

使い方は [docs/guide/asset_browser_guide.md](../guide/asset_browser_guide.md)、用語と技術の解説は [docs/study/](../study/) にあります。ここは「どう作ったか」の設計です。

## 目的
素材(OBJ/FBX)をBlenderで1つずつ開かずに、Unityのように、**フォルダで整理し、サムネイルの一覧で見て、並べて確認し、ゲームの画面へ置く**。
重い処理(モデルを読む)は、**1回だけ**行い、結果をディスクに保存して、次回から軽くする。

## 全体の構成
| 部品 | 場所 | 役割 |
|---|---|---|
| `EditorUi` | `DebugTools/` | ドックスペース(中央は透過)・初期配置・Editorメニュー・ホバーのヒント・ショートカット一覧。パネルの表示フラグを持つ |
| `EditorSettings` | `DebugTools/` | パネルの表示・ヒント・「機能のON/OFF」のスイッチの登録と、`Settings/editor.json` への保存(ImGui非依存でテスト可能) |
| `ShortcutTable` | `DebugTools/` | キー操作の表(ヒントと一覧の唯一の情報源。ImGui非依存) |
| `AssetCatalog` | `DebugTools/` | 素材フォルダを探して、モデルの一覧・フォルダの木・重複の印を作る。フォルダの作成・名前の変更・移動の規則(`static`。テスト可能) |
| `ThumbnailCache` | `DebugTools/` | サムネイルのPNGの生成・保存・読み込み(時間分割・上限512・4MB超は手動) |
| `AssetBrowser` | `DebugTools/` | Assetsパネル(軽い一覧)。木・パンくず・一覧・右クリック・ドラッグ元・フォルダ操作・取り込み結果。ビューワーや配置は、外から渡された関数を呼ぶだけ |
| `AssetViewer` | `DebugTools/` | 別ウィンドウ。一括読み込み(時間分割・上限・大きいファイルの省略)・格子に並べて描画・軌道カメラ・札・情報 |
| `ModelPreviewRenderer` | `DebugTools/` | モデルを絵に描く(1つ=サムネイル、複数=ビューワー)。中心を原点・最大の辺を一定に正規化 |
| `OffscreenTarget` | `Graphics/` | 画面ではなくテクスチャに描く入れ物 |
| `BakedModelCache` | `Graphics/` | モデルのデータを重複のない頂点+インデックスにして保存・読み込み。`ModelLoaderRegistry` の中で使う |
| `ModelCache` | `Object/` | 実行中に必要なモデルをパスで持つ置き場(住所が変わらない)。Propが使う |
| `PropPlacer` | `Field/` | ドラッグ中のゴースト・ドロップでの配置(`PropPlacement.h` に、置き方の純粋な計算) |
| `AssetImporter` | `DebugTools/` | ドロップされたファイル・フォルダを `Assets/Incoming/` へコピーする規則(OS・画面に非依存でテスト可能) |
| `Window` | `Core/` | `WM_DROPFILES` を受けて、パスの一覧をコールバックへ |

### 部品どうしのつなぎ方(依存の向き)
- `AssetBrowser` は、シーン・Prop・ビューワーを**知らない**。`SetOpenViewerHandler` / `SetPlaceAtCenterHandler` / `SetFolderLockCheck` で、Game が関数を渡す。
- 「パネルの開閉」は、`EditorSettings` が持つ `bool` の住所を、各パネルが借りる(`ImGui::Begin(name, &open)`)。
- 「機能のON/OFF」は、各システムが `RegisterToggle(id, ラベル, bool*, 説明)` で自分の `bool` を登録する(メニューは、中身を知らない)。
- ドラッグ中の状態は `GameContext::assetDrag`(AssetBrowser側のImGuiのペイロード → Game が読んで書く → PropPlacer が読む)。`costPreview` と同じ作法(毎フレーム、先頭でリセット)。

## ドッキング(`EditorUi`)
- ImGui を docking ブランチ(タグ `v1.92.8-docking`)へ差し替え。`io.ConfigFlags |= ImGuiConfigFlags_DockingEnable`、`ConfigWindowsMoveFromTitleBarOnly`。
- 土台のウィンドウ(背景なし・動かせない・ドッキング不可)に `DockSpace(PassthruCentralNode | NoDockingOverCentralNode)`。中央は透明で、マウスもゲームへ届く(中央の上では `HoveredWindow` が空)。
- 初期配置は、`imgui.ini` に配置が無い時(`DockBuilderGetNode == nullptr`)と、「レイアウトを初期化」の時だけ作る(`DockBuilder`)。Assets を下のタブの最初に選ぶ。
- メニューバーは、`BeginMainMenuBar`(画面の最上端でHUDを隠す)でなく、HUDの直下の細いウィンドウ(`WindowMinSize` を下げて、24pxに合わせる)。
- ゲームの絵は、中央の枠(ゲームビュー)だけに描く: `Renderer::SetGameViewRect` でビューポートを絞り、3D・デバッグ線・HUD(`UIRenderer::BeginFrame` の原点)は、その枠に収まる。カメラの縦横比(`Camera::SetAspect`)・マウスの座標(`InputManager::SetViewOrigin`: ビューの左上が原点)・HUDの配置は、枠の大きさ・位置を基準にする。メニューバーは、ウィンドウの最上端。ImGuiで3Dの位置に重ねる物(ギズモの文字、道路の確認ボタン)は、枠の位置を足す。

## フォルダの木と整理(`AssetCatalog`)
- 木: 全モデルのフォルダ+親、`Assets/Models`・`Assets/Incoming` の下の全てのフォルダ(空でも)。各節に配下のモデルの数。
- 操作は `Assets/Models` と `Assets/Incoming` の下だけ。個々のモデルファイルの移動・削除はしない(MTL・テクスチャの共有で壊れる)。使用中(シーンのPropが使っている)は、名前の変更・移動を止める(`FolderLockCheck`)。
- 木の描画中に木を作り直さないよう、移動は描画の後でまとめて実行。

## Asset Viewer(`AssetViewer`)
- 開くと、渡された一覧を上限(既定64、最大512)まで、**8msの時間枠**(最低1つ)で順に読む。複数のとき4MB超は省略(「大きいファイルも読む」で読む)。閉じると全て解放。
- 格子の間隔は 2.2(1つの大きさ 1.6)。カメラは軌道カメラ(左=回転、中/右=平行移動、ホイール=ズーム)。全体表示は、絵の縦横比とピッチから、全部が入る距離を求める。
- 描く先は、ウィンドウの大きさ(16px単位で作り直す)の `OffscreenTarget`。名前の札は、`Camera::WorldToScreen` でImGuiの描画リストに。

## モデルキャッシュ(`BakedModelCache`)
- 形式: ヘッダー56バイト(識別子 `GMSH`・版・頂点数・重複なし頂点数・インデックスの大きさ・境界・テクスチャのパスの長さ・チェックサム)+ パス + 重複なし頂点(48バイト) + インデックス(2または4バイト)。
- キー: パス・大きさ・更新日時・同じフォルダの `.mtl` の目印・版 の FNV-1a。ファイルが変われば自然に別のキー。
- 検証: 識別子・版・全ての数値の範囲・ファイルの大きさがヘッダーの計算とぴったり一致・チェックサム(ヘッダー込み)・インデックスの範囲・保存時のテクスチャの存在。通らなければ捨てて、ローダーで読み直す。
- 書き込み: `.tmp` → `rename`。保存の失敗は、読み込みの失敗にしない。
- 統合: `ModelLoaderRegistry::LoadFromFile`(ローダーの前)。統計(ヒット・ミス・破棄・時間)を持ち、Assetsの状況行に出す。
- **`kVersion` を上げる時**: ローダーの出力の意味が変わった時(座標系・色・テクスチャの探し方)。

## Prop(飾り)とセーブ
- `ObjectKind::Prop` と `GameObject::modelPath`。マスを占有しない。種類の表示は、Hierarchy/Profilerに「Prop」。
- 配置: 最大の辺を1マスに収める倍率、XZは中心を地面の点へ、Yは足元を地面の高さへ(`ComputePropPlacement`)。
- ドロップの判定: 毎フレーム `ImGui::GetDragDropPayload()`(種類 `ASSET_MODEL`)と `HoveredWindow == nullptr` と HUDの外 と `IsMouseReleased` から、`assetDrag` を書く(Game::Update。毎フレーム先頭でリセット)。
- セーブ: `WorldSnapshot::props`(パス・Transform)。JSONの `"props"`。版の数字は変えない(古いセーブは飾り0個で読める)。不正な1件は警告して飛ばす(絶対パス・`..`・`\`・`:` は拒否)。モデルが無ければ警告して飛ばす。

## 取り込み(`Window`・`AssetImporter`)
- `DragAcceptFiles` → `WM_DROPFILES` → `DragQueryFileW` → コールバックでパスを溜める → 次のフレームの更新でコピー。
- コピーの規則(使う拡張子だけ・OBJ→MTL→テクスチャの参照・FBXの画像と `Textures/`・同名は `_2`・合計1GB・Incomingの中は対象外・ANSIで表せない名前は飛ばす・ドロップ元の外の参照は取り込まない)は、`AssetImporter::Import` に集約。
- パス→文字列は、表示・キーには UTF-8(`u8string`)、読み込みに使う相対パスは ANSI(`generic_string`)。ANSIで表せない名前は、事前に弾く。

## サムネイルのキャッシュ(前の段階の設計。変更なし)
- 場所 `Cache/Thumbnails/<名前>_<キー>.png`、キーはパス・サイズ・更新日時・版(`ThumbnailCache::kVersion`、今は2)。
- 生成: 256x256で描き、2x2で平均して128x128、PNGに保存。光は昼の標準で固定。時間枠8ms、見えている物が優先。保持数は最大512。4MB超は手動。

## 実測
- モデルキャッシュ(Release、ミス=ローダー / ヒット=キャッシュ。5回/20回の平均): 6600頂点OBJ 6.1ms → 0.44ms、26100頂点 21.3ms → 1.7ms、31476頂点 25.6ms → 1.9ms、FBX 3756頂点 2.5ms → 0.45ms。ディスクは、三角形ごとの形の約1/4(74KB対309KB)。
- ビューワーに41モデル(Debug): キャッシュ無し 約14.9秒 → 有り 約0.39秒。
- 起動(Release、モデル約5個): 約364ms → 約333ms。
- サムネイル(Release、708点): 初回約6〜7秒、2回目約21フレーム(前の段階)。

## 検証した内容
単体テスト(スクラッチ)と、診断コードでの画面確認は、[docs/study/asset_management_and_drag_drop.md](../study/asset_management_and_drag_drop.md) の4章にまとめています。

## 既知の割り切り / 次の段階
- ゲームビューをテクスチャに描く方式(`OffscreenTarget` + `ImGui::Image`)は、採っていない(今のビューポートの限定で、HUDの位置・マウスの座標は足りている)。ビューの絵をImGuiのウィンドウとして扱いたくなった時(ビューのタブ化・フォーカス管理など)に再検討。
- 「採用」ボタン(Incoming → Models のコピーとライセンスのメモの自動生成)。今は、フォルダの移動で代用。
- 大きな素材のコピー中の、画面の停止(別スレッド化)。
- Prop用のUndo・当たり判定・マスの占有。
- SimplePoly の建物は正面が -Z(建物として採用する時に向きの補正が必要)。Quaternius の「Textured」版のOBJは、MTLにテクスチャの参照が無い。
- テクスチャのキャッシュ、素材のexeへの組み込み。
