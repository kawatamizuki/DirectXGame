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
    float needSatisfiedThreshold = 80.0f;       // この値以上なら「充足」とみなす
    float initialNeedMin = 55.0f;               // 住民が生まれた時の需要の範囲(住民ごとに変える)
    float initialNeedMax = 100.0f;
    float weeklyDecayLife = 35.0f;              // 週末に自然に減る量(生活)
    float weeklyDecayActivity = 30.0f;          // 同(アクティビティ)

    // ---- 外出を決める(RunDemandTickごと。約3秒おきに判定) ----
    float needDrivenChancePerTick = 0.25f;      // 需要が不足している時に、外出しようとする確率(1判定あたり)
    float spontaneousChancePerTick = 0.03f;     // 需要が足りていても、自発的に外出する確率(1判定あたり)
    float spontaneousMinStamina = 50.0f;        // 自発的な外出をするのに必要な体力
    float lastDepartureHour = 20.0f;            // この時刻より後は、新しく外出しない(帰宅できなくなるのを避ける)

    // ---- 体力 ----
    float staminaMax = 100.0f;                  // 体力の上限の基準(住民ごとに、この値の周りでばらつく)
    float staminaPerLegacyCell = 0.8f;          // 移動(片道)1マスあたりの体力の消費。往復分を、出る前に確かめる
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
    float distanceScaleLegacyCells = 14.0f;     // 距離で重みが約1/e(37%)になる道のり(マス基準)。住民ごとの係数がかかる
    float minimumWeight = 0.001f;               // 重みの下限(到達できる候補を、無条件に0にしない)

    // ---- 利用履歴(P1。同じ施設への連続利用の重みを下げる。完全には禁止しない) ----
    int repeatVisitWindow = 3;                  // 直近の何回の利用まで見るか(履歴の新しい方から数える)
    float repeatVisitWeightFactor = 0.5f;       // 直前の利用と同じ施設の重みの倍率(1回前=この値。古い履歴ほど1に近づく)
    float needDrivenDailyRepeatStrength = 0.25f;// 不足による日用品の外出では、下げ幅をこの割合に弱める(必要な買い物を妨げないため。0=下げない、1=同じ)

    // ---- 立ち寄り(P1。主目的の施設を使った後、低い確率で近くの施設に寄る。回数が増えるほど確率が下がる) ----
    int maxStopovers = 2;                       // 1回の外出での立ち寄りの最大回数
    float stopoverBaseChance = 0.25f;           // 1回目の立ち寄りの確率(主目的を使った後)
    float stopoverChanceDecay = 0.4f;           // 立ち寄りが1回増えるごとに、確率にかける倍率(2回目= 基本 × この値)
    float stopoverMinStamina = 30.0f;           // 立ち寄りを考えるのに必要な、今の体力
    float stopoverLatestHour = 19.0f;           // この時刻以降は、立ち寄らない(帰宅できなくなるのを避ける)
    float stopoverMaxDistanceLegacyCells = 8.0f;// 今いる施設から、立ち寄り先までの道のりの上限(マス基準)
    float foodChainAcceptChance = 0.2f;         // 同じ外出で、食事系の施設を使った後、別の食事系の施設に寄る時に、実際に寄る確率(通常は1)

    // ---- 満足度・道路が無くなった時の扱い(暫定。後で、救済の方法・ペナルティを見直す) ----
    float satisfactionMax = 100.0f;                     // 満足度の上限(初期値でもある)
    float roadLossRerouteSatisfactionPenalty = 5.0f;    // 道路が無くなって、迂回して帰ることになった時に、下がる満足度
    float roadLossStrandedSatisfactionPenalty = 15.0f;  // 道路が無くなって、立ち往生した時に、下がる満足度
    float roadLossStaminaCostScale = 1.0f;              // 迂回して帰る時の、体力の消費の倍率(通常の移動の消費 × この値。道のり全体にかかる)
    float strandedRoadSnapRadiusLegacyCells = 3.0f;     // 立ち往生した時、今の位置が道路の上でなければ、この範囲(マス基準)の、一番近い道路の上へ寄せる

    // ---- お金 ----
    float salesShareToFunds = 1.0f;             // 売上のうち、プレイヤーの建設資金に入る割合(今は100%。後で変える。税収ではなく店舗売上からの開発資金分配)

    // ---- 試作の補助 ----
    int decisionLogCapacity = 40;               // デバッグ用に残す、施設選びの記録の数
};
