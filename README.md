# DirectXGame

DirectX11を使用して作成している C++ のゲームプロジェクトです。

## 概要
DirectX11を用いた描画処理の学習と、就職作品としての開発を目的に制作しています。  
現在はOBJモデルの読み込み機能に加え、MTLファイルとテクスチャの読み込みまで対応し、  
3Dモデルの描画処理の流れを一通り実装しています。

---

## 現在の実装内容

  ├ 描画
  ├ モデル / Material
  ├ Scene
  ├ Input
  ├ Time
  ├ Debug / Editor
  ├ GameObject / Transform
  └ Window Resize

## 使用技術
- C++
- DirectX11
- HLSL
- Dear ImGui
- Visual Studio
- Git / GitHub

## 実行方法
1. Visual Studio で `GAME.sln` を開く
2. ビルド構成を `Debug` または `Release` に設定
3. 実行する
## 実行結果

### Debug Editor

![DebugEditor](docs/debug_editor/debug_editor.png)

### Resizable
![ResizableBefore](docs/debug_editor/resizable_before.png)

![ResizableAfter](docs/debug_editor/resizable_after.png)

## 理解したこと

### ImGuiによるデバッグエディタ

Dear ImGuiを導入し、ゲーム実行中に内部状態を確認・編集できるデバッグエディタを実装しました。

現在は以下の情報を表示・編集できます。

- FPS / DeltaTime
- VSync状態
- GameObject一覧
- 選択中ObjectのTransform
- Position / Rotation / Scaleの編集

また、Inspector表示では内部処理用のラジアン値をGUI上では度数として扱うことで、編集しやすい形にしました。

DebugEditorはGameContext経由でRenderer、TimeManager、GameObject一覧へアクセスする構造にし、引数が増え続ける問題を避けました。

### ウィンドウリサイズ対応

ウィンドウサイズ変更時にSwapChain、RenderTargetView、DepthStencilView、Viewportを再生成する処理を実装しました。

また、サイズ変更時にCameraのProjection行列も更新することで、最大化やウィンドウサイズ変更後も描画比率やGUIのクリック位置がずれないようにしました。

WindowはGameを直接保持せず、リサイズ通知をコールバックで渡す構造にすることで、WindowとGameの依存を減らしました。

## 現在の課題


- 複数マテリアル対応
- カメラ制御（追従 / 自由視点）
- ライティング（法線の活用）
- デバッグエディタの拡張
  - Object生成
  - 画面上のObject選択
  - Gizmoによる移動・回転・拡縮
- ステージエディタ作成
- ステージデータの保存 / 読み込み
- サウンドシステム
- リソース管理の整理



## 今後の実装予定

- デバッグエディタ拡張
  - Gizmo操作
  - Object生成
  - Scene上でのObject選択

- 初期マップ作成
- カメラ制御
- フィールド移動実装


## 制作意図
DirectX を用いたゲーム開発の基礎理解と、設計・描画・管理の流れを段階的に学ぶために制作しています。  
機能を一つずつ実装しながら、ゲームとして完成度を高めていく予定です。
