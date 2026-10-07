# Milestone 1: フィールド + 建物配置 + 自由角度の道路配置

実装日: 2026-09-09

## 目的

街づくりシミュレーション(就職作品)の最初の実装マイルストーン。
「道路を直線グリッドに縛られず自由な角度で引ける」ことを差別化ポイントとし、
まずは以下の基礎を実装した。

- フィールド(グリッド地面)の表示
- マウス操作による建物配置
- 任意の2点を直線で結ぶ道路配置
- 建物と道路が同じ占有判定を共有し、重複配置を防ぐ

NPC・建物パラメータ・曲線道路・セーブ/ロード・インスタンシングは今回のスコープ外。

## 実装内容

### 1. 土台の修正

- **`InputManager::SetWindowHandle`未呼び出しのバグを修正** — `Core/Game.cpp::Initialize`で呼び出しを追加。これまで`GetMousePosition()`はスクリーン座標をそのまま返しており、クライアント座標に変換されていなかった。
- **マウスレイ生成を`Camera::ScreenPointToRay`に抽出**(`Graphics/Camera.h/.cpp`) — 従来`DebugEditor::CreateMouseRay()`内に閉じ込められていた処理を共通化。`DebugEditor`側は薄いラッパーに変更。
- **`IntersectRayPlane`を`Math/Collision.h/.cpp`に抽出** — 同様にDebugEditor専用だった処理を自由関数化。`DebugEditor::IntersectRayPlane`は新しい共有関数への転送のみになった。
- **Play/Stop切り替え + 入力消費の優先度チェーン**(`Core/GameMode.h`、`GameContext::mode`/`inputConsumed`) — Unityの「編集中/再生中」に相当する仕組みを、単一ウィンドウ構成向けに軽量な形で実装。DebugEditorのImGuiパネルに「▶ Play / ■ Stop」ボタンを追加した。
  - Playing: `BuildController`/`RoadSystem`の入力が有効。オブジェクト選択・ギズモは無効。
  - Editing: オブジェクト選択・ギズモが有効(従来通り)。建物/道路配置は無効。
  - カメラ移動(Free Camera)はモードに関係なく常時有効。

### 2. GameObjectタグ付け + 生成ヘルパー

- `Object/GameObject.h`に`enum class ObjectKind { Environment, Building, Road }`を追加。
- `Object/GameObjectFactory.h/.cpp`(新規) — `Spawn`/`Despawn`のみを持つ関数群。`BuildController`/`RoadSystem`はここ経由でオブジェクトを生成する。

### 3. グリッド数学 + Field

- `Math/GridCoord.h/.cpp`(新規) — ワールド座標⇔グリッド座標の変換、ハッシュ。
- `Field/Field.h/.cpp`(新規) — グリッド寸法・格子線描画・地面レイキャスト(`RaycastGround`)・地面の高さ取得(`GetHeightAt`)。v1では平面(y=0)固定だが、将来ハイトマップ地形にする際はこのクラス内部だけを差し替えればよい設計にしてある。

### 4. IOccupancyGrid(共有占有判定)

- `Field/IOccupancyGrid.h/.cpp`(新規、インターフェース) — 建物・道路が共有する「このマスは空いているか」の判定。
- `Field/CellOccupancyGrid.h/.cpp`(新規、v1実装) — `unordered_set<GridCoord>`によるセル単位管理。将来より精密な判定に差し替える場合、`Game`側の1メンバ型を変えるだけで済む。

### 5. BuildController(建物配置)

- `Field/BuildController.h/.cpp`(新規) — マウスレイ→地面ワールド座標→グリッドセル変換→空きセル確認→`GameObjectFactory::Spawn`で建物生成。ホバー中セルを緑/赤の枠線でプレビュー表示。

### 6. 道路システム

- `Field/RoadSegment.h`(新規) — 道路1区間の純粋データ(始点・終点・幅)。
- `Field/IRoadPlacementStrategy.h`(新規、インターフェース) + `Field/StraightRoadPlacementStrategy.h/.cpp`(v1実装) — 2クリックで直線区間を確定する方式。将来曲線ストラテジーに差し替えても呼び出し側は無変更。
- `Field/IRoadMeshGenerator.h`(新規、インターフェース) + `Field/DebugLineRoadMeshGenerator.h/.cpp`(v1実装) — `DebugRenderer`のライン描画で幅のある道を簡易表現。将来Kennyの道路モデル(`Models/Roads/Kenney`に既存)を使う実装に差し替え可能。
- `Field/RoadSystem.h/.cpp`(新規) — 配置フロー全体の制御。通過セルを`cellSize`刻みでサンプリングし、`IOccupancyGrid::IsFreeRange`で1つでも埋まっていれば道路全体を拒否する。

### 7. Game/GameContextへの配線

- `Core/GameContext.h`に`GameMode mode`と`bool inputConsumed`を追加。
- `Core/Game.h/.cpp`に上記システムをメンバとして追加し、`Initialize`/`Update`/`Draw`から呼び出す。

## 実装中に見つけて追加で直した点

- **`DebugRenderer`の初期化/`Flush`が`#ifdef ENABLE_EDITOR`の中にあった問題** — 今回`DebugRenderer`をフィールドの格子線・建物/道路の見た目にも使う設計にしたため、Releaseビルド(`ENABLE_EDITOR`未定義)だと何も描画されなくなってしまうことが判明。`DebugRenderer`の初期化と`Flush`呼び出しを`ENABLE_EDITOR`の外に出した(ImGuiパネルやギズモなど、本当にエディタ専用の部分は従来通り`ENABLE_EDITOR`内のまま)。
- **`ENABLE_EDITOR`のRelease除外は元々対応済みだった** — 当初「Debug/Release両方に定義されている」という調査結果があったが、実際に`GAME.vcxproj`を確認したところ誤りで、Releaseには元々含まれていなかった。修正不要と判断。
- **`std::optional`が使えないビルドエラー** — プロジェクトの言語標準が指定されていなかった(既定でC++14相当)。`GAME.vcxproj`の全構成(Debug/Release × Win32/x64)に`<LanguageStandard>stdcpp17</LanguageStandard>`を追加。
- **UTF-8/Shift-JISの文字コード競合によるビルドエラー** — 既存ファイルはShift-JIS、新規作成ファイルはUTF-8(BOM無し)で保存されており、コンパイラが文字化けを起こして構文エラーになった。新規作成した.h/.cppファイルすべてにUTF-8 BOMを付与して解決。
- **`Windows.h`の`max`マクロと`std::max`の衝突** — `RoadSystem.cpp`が`InputManager.h`(`Windows.h`を間接的に含む)を includeしているため、`std::max`がマクロ展開されて構文エラーになった。`std::max`を使わず`if`文で下限を掛ける形に変更。
- **既存ファイル編集時の文字コード破損(追加対応)** — Shift-JIS(BOM無し)の既存ファイルをEditツールで編集すると、ファイル全体がUTF-8として書き戻され、未変更部分も含めて既存の日本語コメントが文字化けすることが判明。影響した8ファイル(`Core/Game.cpp`, `Core/Game.h`, `Core/GameContext.h`, `Graphics/Camera.h/.cpp`, `Math/Collision.h/.cpp`, `Object/GameObject.h`)は、git履歴のコミット済み内容をShift-JISとして正しく読み直し、今回の実装差分を再適用したうえでUTF-8(BOM付き)として保存し直して復元した。プロジェクト全体69ファイルを走査し、非ASCII文字を含むファイルすべてにBOMが付与されていることを確認済み。

## 動作確認

- Debug|x64、Release|x64の両構成でビルド成功を確認済み(MSBuildで実行)。
- 実行時にプロジェクトルートを作業ディレクトリとして起動し、クラッシュせず起動することを確認済み(Visual StudioでF5実行する場合と同じ条件)。
- 実際の操作感(建物配置・道路配置・Play/Stop切り替え・既存のDebugEditor機能)は、Visual Studio上での目視確認が必要(この環境では画面を直接見られないため未実施)。

## 既知の制約・今後の課題

- **Releaseビルドではカメラが動かせない** — Free Camera(視点移動)は`DebugEditor::UpdateFreeCamera()`内にのみ実装されており、`DebugEditor`自体が`ENABLE_EDITOR`(Releaseでは未定義)でしか動かないため、Release版では視点固定になってしまう。今回のマイルストーンのスコープ外だが、実際に配布可能な版を作る際は、カメラ移動をDebugEditorから独立させる必要がある。
- 占有判定・道路のセル判定・道路の見た目・道路配置アルゴリズムは、すべてv1の簡易実装であり、差し替え用のインターフェース(`IOccupancyGrid`/`IRoadPlacementStrategy`/`IRoadMeshGenerator`)越しに使う設計にしてある(詳細はプラン内の「意図的なv1割り切りと、その差し替え口」を参照)。
- Field(グリッド)のサイズは起動時固定。実行中の拡張(土地購入)や地形の起伏には、`Field::RaycastGround`/`GetHeightAt`という差し替え口を用意済みだが、実装は含まれていない。
- 建物/道路の削除UIは未実装(`GameObjectFactory::Despawn`は用意済みだが呼び出し箇所なし)。

## 変更ファイル一覧

**新規**:
`Core/GameMode.h`, `Object/GameObjectFactory.h/.cpp`, `Math/GridCoord.h/.cpp`,
`Field/Field.h/.cpp`, `Field/IOccupancyGrid.h/.cpp`, `Field/CellOccupancyGrid.h/.cpp`,
`Field/BuildController.h/.cpp`, `Field/RoadSegment.h`, `Field/IRoadPlacementStrategy.h`,
`Field/StraightRoadPlacementStrategy.h/.cpp`, `Field/IRoadMeshGenerator.h`,
`Field/DebugLineRoadMeshGenerator.h/.cpp`, `Field/RoadSystem.h/.cpp`

**変更**:
`Core/Game.h/.cpp`, `Core/GameContext.h`, `Graphics/Camera.h/.cpp`, `Math/Collision.h/.cpp`,
`DebugTools/DebugEditor.h/.cpp`, `Object/GameObject.h`, `GAME.vcxproj`, `GAME.vcxproj.filters`
