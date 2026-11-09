# 単体テスト

画面(DirectX / ImGui)に依存しない、シミュレーションの規則を、自動で確かめるテスト。ゲーム本体のビルド(`GAME.sln`)には含まれない。

## 実行
リポジトリのルートで:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_tests.ps1 -Filter p1   # 名前に p1 を含む物だけ
```

- 共有するゲーム側のソースを1回だけコンパイルして、`*_test.cpp` を1本ずつ、ビルド・実行する。
- 1本でも失敗(ビルド失敗を含む)したら、終了コード1。
- 生成物は、リポジトリの外(`%TEMP%\GAME_tests`)に出る。
- Visual Studio 2026(`vcvars64.bat`)が必要。パスが違う場合は、`run_tests.ps1` の `$vcvars` を直す。

## テストの一覧
| ファイル | 対象 |
|---|---|
| `sim_rules_test.cpp` | 住民の行動規則(ResidentLogic)・時計・施設の台帳・Json |
| `sim_p1_test.cpp` | 利用履歴・立ち寄り・食事系の規則(P1の仕様。体力の部分は P2-0 の規則に更新) |
| `sim_p2_0_test.cpp` | P2-0: 需要の段階・外出の理由・理由ごとの距離・体力(移動で消費しない)・立ち寄りの連続的な確率 |
| `sim_debug_recorder_test.cpp` | 住民デバッグの記録(SimDebugRecorder) |
| `sim_timeline_test.cpp` | 一時停止の条件(SimBreakpoints)・記録の時間軸(SimTimeline) |
| `sim_test_command_test.cpp` | 行動テストの対象選び・テスト番号の付与 |
| `sim_stats_test.cpp` | 行動統計(SimStats) |
| `sim_digest_test.cpp` | 実行結果の指紋(SimRunDigest) |
| `sim_invariant_monitor_test.cpp` | 外出の流れの異常検知(SimInvariantMonitor) |

## 足し方
- `tests/` に `<名前>_test.cpp` を置くだけ(`main` が、成功なら0・失敗なら1を返す)。`run_tests.ps1` が、自動で拾う。
- 新しい `.cpp` のゲーム側のソースが要る時は、`run_tests.ps1` の `$common` に足す。
- `stubs/imgui.h` は、`Core/GameClock.cpp` をリンクするための空の ImGui。画面を描く物は、テストに入れない。
