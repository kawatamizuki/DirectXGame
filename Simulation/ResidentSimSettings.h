#pragma once
#include <cstdint>

// 住民の外出・施設選び・体力・需要・週のペナルティの、調整用の数値をまとめた設定。
// 仮の値をコードの中に散らさず、全てここに集める。Editorの「Facilities」パネルで、実行中に変えて試せる。
// 長さは「マス基準」(マスの大きさが変わっても、同じ比率を保つ。WorldScale::FromLegacy を通して使う)。
// 時間は「ゲーム内の時間」(1週間 = 朝6時〜夜10時の活動時間 + 就寝)。数値は全て仮で、最終仕様ではない。
struct ResidentSimSettings
{
    // ---- 乱数 ----
    uint32_t seed = 12345;                      // 乱数の種(同じ種・同じ配置なら、同じ選択が再現される)

    // ---- 需要(生活・アクティビティ。0〜100) ----
    float needSatisfiedThreshold = 80.0f;       // この値以上なら「充足」とみなす(これ未満は「不足」の段階以下)
    float needSevereThreshold = 50.0f;          // (P2-0)これ未満は「深刻」
    float needCriticalThreshold = 25.0f;        // (P2-0)これ未満は「危機的」
    float initialNeedMin = 55.0f;               // 住民が生まれた時の需要の範囲(住民ごとに変える)
    float initialNeedMax = 100.0f;
    float weeklyDecayLife = 35.0f;              // 週末に自然に減る量(生活)
    float weeklyDecayActivity = 30.0f;          // 同(アクティビティ)

    // ---- 外出を決める(RunDemandTickごと。約3秒おきに判定) ----
    float needDrivenChancePerTick = 0.25f;      // 需要が不足している時に、外出しようとする確率(1判定あたり)
    float spontaneousChancePerTick = 0.03f;     // 需要が足りていても、自発的に外出する確率(1判定あたり)
    float spontaneousMinStamina = 50.0f;        // 自発的な外出をするのに必要な体力
    float lastDepartureHour = 20.0f;            // この時刻より後は、新しく外出しない(帰宅できなくなるのを避ける)

    // ---- 外出の理由(P2-0)。外出が決まった後に、「手軽 / がっつり / エリア」のどれで出かけるかを、重み付きの抽選で決める ----
    // 理由は、(1)行き先の距離の効き方(下の reasonDistance*)と、(2)立ち寄りのしやすさ(stopoverReasonMultiplier)を変える。
    // 理由を決める重みには、今の状態(需要の切迫・体力)を、1回だけ反映する(理由の後の、カテゴリ・施設選びには、二重に掛けない)。
    float reasonQuickWeight = 1.0f;             // 「手軽に需要を満たしたい」の基本の重み
    float reasonSubstantialWeight = 0.8f;       // 「がっつり外出したい」の基本の重み(体力・余裕に応じて変わる)
    float reasonAreaWeight = 0.0f;              // 「エリアに外出したい」の基本の重み。エリアの候補が実装されるまで(P2-A)は、候補が無いので、必ず外れる
    float needPressureByStage[4] = { 0.0f, 0.35f, 0.7f, 1.0f }; // 需要の段階(充足/不足/深刻/危機的)ごとの、切迫度(0〜1)
    float substantialPressurePenalty = 0.85f;   // 切迫度が1の時、「がっつり」の重みから引く割合(切迫していると、手軽な外出になりやすい)
    float substantialStaminaExponent = 1.0f;    // 「がっつり」の重み = 体力の割合 ^ この値(体力に余裕があるほど、がっつり)
    // 理由ごとの距離の効き方: 距離の重み = exp( -(道のり ÷ 距離の目安)^形 )。距離の目安 = distanceScaleLegacyCells × 住民の距離許容度 × 下の倍率。
    // 並びは、{手軽, がっつり, エリア}。手軽は、遠いほど、強く下がる(従来と同じ)。がっつりは、減り方が弱い(遠くの店も候補に残る)。
    float reasonDistanceScale[3] = { 1.0f, 2.5f, 1.0f };    // 距離の目安の倍率
    float reasonDistanceShape[3] = { 1.0f, 1.0f, 1.0f };    // 減り方の形(1=指数。大きいほど、近場は変わらず、ある距離から急に下がる)

    // ---- 需要のカテゴリの重み(P2-0)。外出の目的(生活=日用品 / 余暇)を、重み付きの抽選で決める ----
    // 重み = カテゴリの重要度 × そのカテゴリの需要の段階の補正。余暇の需要が深刻でも、重要度の低い余暇は、生活より選ばれにくい(例: 生活50・余暇10 → 生活が約8割)。
    // 必ず生活になるわけではない(余暇が選ばれる例外が残る)。需要が両方足りている時は、余暇が多め(従来の「自発的な外出」と同じ傾向)。
    float dailyImportance = 1.0f;               // 生活(日用品)の重要度(暫定)
    float leisureImportance = 0.5f;             // 余暇の重要度(暫定)
    float dailyStageMultiplier[4] = { 0.06f, 1.0f, 1.6f, 2.5f };    // 生活の段階の補正(充足/不足/深刻/危機的)
    float leisureStageMultiplier[4] = { 0.30f, 0.40f, 0.45f, 0.50f }; // 余暇の段階の補正(余暇は、段階が上がっても、あまり増えない)

    // ---- 体力(P2-0: 移動では、体力を消費しない。施設を使う時だけ、消費する) ----
    float staminaMax = 100.0f;                  // 体力の上限の基準(住民ごとに、この値の周りでばらつく)
    float staminaHomeRecoveryPerHour = 2.0f;    // 家にいる間の、1ゲーム時間あたりの回復
    float staminaSleepRecovery = 60.0f;         // 就寝(週の始まり)での回復量(上限まで)

    // ---- 週末のペナルティ(生活・アクティビティ不足は、初期版では同じ体力ペナルティ) ----
    float unmetGraceHours = 8.0f;               // 不足したまま、この時間(ゲーム内)を超えて改善できなかったら、週末にペナルティ
    float recoverSatisfiedHours = 10.0f;        // 両方が充足した状態が、この時間以上続いた週は、ペナルティが1段階軽くなる
    int maxPenaltyLevel = 3;                    // ペナルティの最大の段階
    float penaltyStaminaCapStep = 10.0f;        // 1段階あたり、体力の上限を下げる量

    // ---- 施設の選び方(重み付き抽選) ----
    float qualityExponent = 1.0f;               // 品質の効き方(重み = 品質^この値)。住民ごとの係数がかかる
    float priceReferenceYen = 300.0f;           // 値段の基準(円)。これより高いと重みが下がる
    float priceStrength = 0.6f;                 // 値段の効き方(大きいほど、高い店を避ける)。住民ごとの係数がかかる
    float distanceScaleLegacyCells = 14.0f;     // 距離で重みが約1/e(37%)になる道のり(マス基準)。住民ごとの係数と、理由ごとの倍率(reasonDistanceScale)がかかる
    float minimumWeight = 0.001f;               // 重みの下限(到達できる候補を、無条件に0にしない)

    // ---- 利用履歴(P1。同じ施設への連続利用の重みを下げる。完全には禁止しない) ----
    int repeatVisitWindow = 3;                  // 直近の何回の利用まで見るか(履歴の新しい方から数える)
    float repeatVisitWeightFactor = 0.5f;       // 直前の利用と同じ施設の重みの倍率(1回前=この値。古い履歴ほど1に近づく)
    float needDrivenDailyRepeatStrength = 0.25f;// 不足による日用品の外出では、下げ幅をこの割合に弱める(必要な買い物を妨げないため。0=下げない、1=同じ)

    // ---- 立ち寄り(P1 → P2-0で連続的に。主目的の施設を使った後、確率で、近くの施設に寄る) ----
    // 立ち寄る確率 = 基本 × 回数の減衰 ^ 立ち寄り済みの回数 × 体力の割合 ^ 体力の指数 × 需要の補正 × 理由の倍率。
    int maxStopovers = 2;                       // 1回の外出での立ち寄りの最大回数(暫定の安全弁。3回以上にできる作りにしてある)
    float stopoverBaseChance = 0.4f;            // 1回目の立ち寄りの基本の確率(P1は0.25。理由の倍率(手軽 0.5)が掛かるので、全体の平均は、P1に近い)
    float stopoverChanceDecay = 0.4f;           // 立ち寄りが1回増えるごとに、確率にかける倍率(2回目= 基本 × この値)
    float stopoverStaminaExponent = 1.0f;       // 体力の割合 ^ この値 を、確率に掛ける(体力が少ないほど、寄りにくい。固定のしきい値は無い)
    float stopoverNeedFloor = 0.7f;             // 需要が全て充足している時の、確率の倍率(切迫度が上がるほど、1に近づく)
    float stopoverReasonMultiplier[3] = { 0.5f, 1.2f, 1.0f }; // 理由ごとの確率の倍率 {手軽, がっつり, エリア}(暫定)
    float stopoverDistanceScaleFactor = 0.6f;   // 立ち寄り先の距離の目安は、(理由ごとの目安)× この倍率(今いる施設から近い所に寄りやすい)
    float stopoverLatestHour = 19.0f;           // この時刻以降は、立ち寄らない(帰宅できなくなるのを避ける。時間の上限)
    float stopoverMaxDistanceLegacyCells = 0.0f;// 今いる施設から、立ち寄り先までの道のりの上限(マス基準)。0=上限なし(距離は、確率の減衰で扱う)
    float additionalUseStaminaCostStep = 0.15f; // 2軒目以降の利用の、体力の消費の増え方(利用の何軒目か × この割合。0=増えない)。2軒目=1.15倍、3軒目=1.3倍
    float foodChainAcceptChance = 0.2f;         // 同じ外出で、食事系の施設を使った後、別の食事系の施設に寄る時に、実際に寄る確率(通常は1)

    // ---- 満足度・道路が無くなった時の扱い(暫定。後で、救済の方法・ペナルティを見直す) ----
    float satisfactionMax = 100.0f;                     // 満足度の上限(初期値でもある)
    float roadLossRerouteSatisfactionPenalty = 5.0f;    // 道路が無くなって、迂回して帰ることになった時に、下がる満足度
    float roadLossStrandedSatisfactionPenalty = 15.0f;  // 道路が無くなって、立ち往生した時に、下がる満足度
    float strandedRoadSnapRadiusLegacyCells = 3.0f;     // 立ち往生した時、今の位置が道路の上でなければ、この範囲(マス基準)の、一番近い道路の上へ寄せる

    // ---- お金 ----
    float salesShareToFunds = 1.0f;             // 売上のうち、プレイヤーの建設資金に入る割合(今は100%。後で変える。税収ではなく店舗売上からの開発資金分配)

    // ---- 試作の補助 ----
    int decisionLogCapacity = 40;               // デバッグ用に残す、施設選びの記録の数
};
