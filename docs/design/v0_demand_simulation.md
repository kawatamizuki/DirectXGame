# v0: 人流・需要シミュレーションの「歩く骨格」

実装日: 2026-09-10

## 目的

企画検討の結果、このゲームの核は「道路の自由な形そのもの」ではなく「道路網を使って人の流れをデザインし、需要と供給の相互作用で街が発展すること」だと整理された。この方向性が実際に面白いかを検証するため、見た目を作り込む前に、建物種別・需要生成・道路グラフ・フロー割り当てを最小限だけ繋いだ「歩く骨格(v0)」を実装した。

詳細な設計判断の経緯は `C:\Users\mizuk\.claude\plans\memoized-stargazing-bird.md`(このセッションで承認したプラン)を参照。

## 実装内容

### 安定したid参照(土台)
- `GameObject`に一意な`id`を追加(`GameObjectFactory::Spawn`が発行)。
- `GameObjectFactory::FindById`を追加。複数フレームにまたがってオブジェクトを参照する箇所(代表住民の自宅/職場/自分自身)は、`std::vector`の添字ではなく`id`で持ち、使う直前に`FindById`で解決する。将来の建物削除機能でインデックスがずれても壊れない設計。

### 建物種別
- `Object/BuildingType.h/.cpp`: House(住宅)/Office(職場)/Shopの3種別と、それぞれの性能値(人口換算・代表住民数・雇用容量・モデルパス)を定義。
- `BuildController`にImGuiの仮選択UI("Build"ウィンドウ)を追加。本格的なin-game UIに置き換える前提。

### 道路網のグラフ化 + 経路探索
- `Simulation/RoadGraph.h/.cpp`: 配置済み道路区間から、交差点/端点をノードとするグラフを構築するデータ構造。
- `Simulation/IPathfinder.h` + `Simulation/DijkstraPathfinder.h/.cpp`: 経路探索をインターフェースで分離し、ダイクストラ法で実装。将来、観光施設の周辺が儲かるような経路依存のゲーム性や、都市規模による最適化(A*等)に備えて差し替え可能にしてある。

### 需要生成・代表住民の割り当てと移動
- `Simulation/ResidentAgent.h`: 代表住民1人分の状態(自宅/職場/移動経路/現在の状態)。
- `Simulation/DemandSystem.h/.cpp`: 3秒おきに、House規模に応じた代表住民の生成、空きのある最寄りOfficeへの割り当て(経路探索の結果でソート)、道路混雑度(`RoadSegment::debugLoad`)への反映を行う。移動自体(代表住民が道に沿って歩くアニメーション)は毎フレーム更新する。

### フィードバック
- `Field/RoadSegment.h`に`debugLoad`(表示専用の混雑度)を追加、`DebugLineRoadMeshGenerator`で混雑度に応じて道路の色を変える。
- `DemandSystem::DrawDebugUI`で「City Stats」パネル(House/Office数、人口換算、代表住民数、就業状況、未充足需要)を表示。
- `DebugEditor`のHierarchy/Inspectorを拡張し、オブジェクトの種類(Building/Road/Agent/Environment)の表示と、選択した建物・代表住民の詳細情報の表示に対応。

### 簡易プロファイラ
- `DebugTools/Profiler.h/.cpp`: `PROFILE_SCOPE("名前")`で処理時間を計測できるようにし、「Profiler」パネルで処理時間・オブジェクト種別ごとの数・プロセスのメモリ使用量(Working Set)を表示する。道路グラフの再構築や需要計算が実際どれくらいの負荷か、感覚ではなく実測で判断できるようにするためのもの。

## 動作確認

- Debug|x64、Release|x64ともビルド成功を確認済み。
- 実行時にプロジェクトルートを作業ディレクトリとして起動し、クラッシュせず起動することを確認済み。
- **需要シミュレーションの実際の動作(建物配置→代表住民の出現→通勤→統計反映)は、画面を直接見られない環境のため未確認です。プランの検証手順に沿って、Visual Studio上で確認してください。**

## 既知の制約・今後の課題

- **Releaseビルドでは今回追加したImGuiパネル(Build/City Stats/Profiler)がおそらく正しく動作しません。** ImGuiの`NewFrame`/`Render`の呼び出しが`DebugEditor`経由で`ENABLE_EDITOR`(Releaseでは未定義)の中に閉じているため、Release単独でこれらのUIを表示しようとすると、対応する`ImGui::NewFrame()`が呼ばれないままウィンドウを開こうとして不正な状態になる可能性があります。今回追加したUI群はいずれも「後で本格的なUIに置き換える前提の仮ボタン」であり、Milestone 1で分かっている「Releaseではカメラも動かせない」制約と合わせて、配布可能な版を作る際にImGuiの初期化をENABLE_EDITORから切り離す対応が必要です。
- 需要フローは通勤(House→Office)のみ。買い物・観光等はスコープ外。
- 代表住民は一度職場に着いたら帰宅・再割当をしない。
- 道路グラフは毎ティック作り直し、Office選定も毎回総当たり。Profilerパネルで実測し、重くなったら最適化する。
- `GameObjectFactory::FindById`は線形探索。数が増えてボトルネックになったら、ハンドル+世代番号方式(id→indexの対応表)へのアップグレードを検討する。

## 変更ファイル一覧

**新規**:
`Object/BuildingType.h/.cpp`, `Simulation/RoadGraph.h/.cpp`, `Simulation/IPathfinder.h`,
`Simulation/DijkstraPathfinder.h/.cpp`, `Simulation/ResidentAgent.h`, `Simulation/DemandSystem.h/.cpp`,
`DebugTools/Profiler.h/.cpp`

**変更**:
`Object/GameObject.h`, `Object/GameObjectFactory.h/.cpp`, `Field/BuildController.h/.cpp`,
`Field/RoadSegment.h`, `Field/RoadSystem.h`, `Field/DebugLineRoadMeshGenerator.cpp`,
`Core/GameContext.h`, `Core/Game.h/.cpp`, `DebugTools/DebugEditor.cpp`,
`GAME.vcxproj`, `GAME.vcxproj.filters`
