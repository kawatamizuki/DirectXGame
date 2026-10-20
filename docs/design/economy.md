# お金(所持金)と建設費の設計

## 方針
- お金の出し入れは **`GameEconomy` の1か所**を通す(`Spend`/`Earn`/`CanAfford`)。足りない時・負の額・上限の扱いが全体で揃う。
- 値段は**データの表**に集める。バランス調整は表の数字だけを変えればよい。
  - 建物: `Object/BuildingType.cpp` の `BuildingDefinition::cost`(円)
  - 道路: `Field/RoadType.cpp` の `RoadTypeDefinition::costPerCell`(1マスあたりの円。道幅の種類ごと)
  - 初期所持金: `Simulation/GameEconomy.h` の `EconomyConfig::startingCash`
- **収入は未実装**(今は減るだけ)。住民の税・オフィスの売上などは、`GameEconomy::Earn` を呼ぶだけで足せる。

## 値段(仮)
| 項目 | 金額 |
|---|---|
| 初期所持金 | 1000円 |
| 道路 | 1マスにつき20円(種類によらず。後半で道幅によって変える予定) |
| House | 100円 |
| Office | 300円 |
| Shop | 200円 |

## 道路の値段の決め方
`CalculateRoadCost(全長, マスの大きさ, 種類)` = **ceil(全長 ÷ マスの大きさ − 0.001) × 1マスの単価**(`Field/RoadType.cpp`)。
- 直線モード: 始点→終点の長さ(6マスなら120円。斜めで長さ1.41なら2マス=40円)。
- フリーハンド: 区間ごとではなく、**手ぶれ補正後の道1本の全長**で決める(`RoadSystem::GetPendingCurveLength`)。
  フリーハンドで直線に引くと1本の直線に均されるので直線モードと同額。うねらせる・細かく曲げるほど全長が伸びて高くなる
  (区間ごとに切り上げて足すと過剰に取られるので、全長でまとめて数える)。
- 既存の道路の途中に繋ぐ(T字の分割)場合も、新しく引いた道の長さだけが対象。

## 払う場所(課金はプレイヤーが置く操作だけ)
| 操作 | 処理 | 足りない時 |
|---|---|---|
| 建物 | `BuildController::PlaceBuilding`(クリックも`TryPlaceAt`もここを通る) | ゴースト(置く予定の見た目)が赤、置けない |
| 直線の道路 | `RoadSystem::CommitStraightSegment`(払えるか確認 → 置く → 置けた時だけ払う) | ホバーが赤、クリックしても置けない(始点は残る) |
| フリーハンドの道路 | `RoadSystem::ConfirmPendingCurve`(全区間を置き終えてから、全長ぶんを1回だけ払う) | ゴーストが赤、「確定」ボタンが無効 |

- **途中が置けない場所を通っているフリーハンドは、全部置かない**(全か無か。課金もしない)。キャンセルでも課金しない。
- セーブデータの復元(`SpawnBuilding`/`RestoreSegments`)は課金しない。
- 払い戻しは無い(削除機能が無いため)。

## 表示
- 上部HUDの中央に所持金(コインのアイコン+「1,000円」)。独自描画のHUD(`UI/GameUI`)が `HudState::cash` を描く。
- カーソルの横の小さなタグに、置こうとしている建物・道路の費用を出す(`GameContext::costPreview` → `HudState::costTag`)。
  道路は「6マス  -120円」のようにマス数も出す。払えない時は赤い枠と「お金が足りません」。
- 倍速・停止・再生ボタンはHUDから外した(今後「メニューを開いたら時間が止まる」方式にする予定)。`GameClock::SetTimeScale` とデバッグの「Time」パネルは残している。
- デバッグ用ImGuiパネル「Economy」で、所持金・初期所持金の変更ができる。

## セーブ
`economy` 区画(`cash`)。詳細は `docs/design/save_load.md`。
