#pragma once
#include <cstdint>
#include "CommercialArea.h"

// 商業エリアの評判の更新規則(P2-D)。画面・ゲームのオブジェクトに依存しない純粋な関数(単体テストできる)。乱数を使わない。
//
//  評判は、週末に、「目標値」へ少しずつ近づける(1週の変化に上限)。目標値 = 1.0 + 計画の履行の項 + 実際の利用の項。この2つは、独立に評価する。
//
//  ・計画の履行: 今の店舗数・最低進捗・猶予で決める。**過去の最高到達段階(achieved)だけでは、達成済みとしない**(履歴として残すだけ)。
//      初回の開発猶予中(周知してから graceWeeks 週): 減点なし。
//      最低進捗以上(今): 減点なし。計画規模の店舗数がそろっていれば、少し加点。
//      最低進捗未満(今):
//        一度も最低進捗に届いていない → 初回の猶予が終わったら、すぐ、不足の割合に応じて減点。
//        一度届いたことがある → 下回り始めた週から reorganizeGraceWeeks 週の「再整備の猶予」(減点なし)。過ぎても未達なら減点。
//      再整備の猶予の計測は、最低進捗を recoveryHoldWeeks 週続けて保つまで、取り消されない(一時的な店舗の追加で、猶予を引き延ばせない)。
//  ・実際の利用: 先週の、エリアを目的にした外出の成功率(補助)と、区画内の利用で実際に回復した需要(標準の回復量に対する割合)から決める。猶予中でも働く。
//      来訪者数そのものは使わない(評判が下がって客が減り、さらに下がる、という悪循環を避ける)。売上も使わない。
//      成功率はほぼ100%に固定されがちなので、小さな加点で頭打ち。需要への貢献は、満たされていても減点しない(0。加点が無いだけ)。標準の回復量より多く回復しても、上限がある。
enum class AreaPlanState : uint8_t
{
    NotPublished,   // 非公開の仮置き(評判は変化しない)
    InitialGrace,   // 初回の開発猶予中(計画未達の減点なし)
    Met,            // 今、最低進捗以上
    Reorganizing,   // 一度最低進捗に届いた後、下回って、再整備の猶予中(減点なし)
    Shortfall       // 最低進捗未満で、猶予が過ぎた(不足の割合に応じて減点)
};

const char* AreaPlanStateName(AreaPlanState state);

// 先週の、区画の利用の集計(AreaLedger の lastWeek から作る)。
struct AreaUsageWeek
{
    // 利用成功率の、同じ集団(AreaTally の areaResolved / areaSucceeded。結果が決まった週に、分子・分母を一緒に数える。出発の週と利用の週がずれても、ずれない)。
    int resolvedOutings = 0;        // 結果が決まった、エリアを目的にした外出の数
    int succeededOutings = 0;       // うち、その区画の施設を使えた数(resolvedOutings 以下。成功率は、必ず0〜1)
    int visits = 0;                 // 区画内の延べ利用回数
    float needNominal = 0.0f;       // 区画内の利用の、施設の標準の回復量(生活+余暇)の合計
    float needActual = 0.0f;        // 区画内の利用で、実際に回復した需要の合計(住民の需要が満たされていると、標準より少ない)
};

struct AreaReputationResult
{
    AreaPlanState planState = AreaPlanState::NotPublished;
    int storeCount = 0;
    bool metNow = false;            // 今、最低進捗以上か
    bool hadMetBefore = false;      // 過去に最低進捗に届いたことがあるか(履歴。現在の達成の判定には使わない)
    int initialGraceLeft = 0;       // 初回の開発猶予の残り週数(0以下=過ぎた)
    int reorganizeGraceLeft = 0;    // 再整備の猶予の残り週数(計測していない時は0)
    float planTerm = 0.0f;
    bool successMeasured = false;
    float successRate = 0.0f;
    float successTerm = 0.0f;
    bool needMeasured = false;
    float needEffect = 0.0f;        // 実際に回復した需要 ÷ 標準の回復量(0〜1)
    float needTerm = 0.0f;
    float useTerm = 0.0f;           // successTerm + needTerm
    float target = 1.0f;
    float newReputation = 1.0f;
    float delta = 0.0f;
    int belowMinimumSinceWeek = 0;  // 更新後の値
    int metMinimumStreakWeeks = 0;  // 更新後の値
};

// area の今の状態から、週末の更新の結果を求める。area は変えない(結果を、呼び出し側が、area に書き戻す)。
// currentWeek: 通算の週。storeCount: 今、区画に所属する店舗数。
AreaReputationResult EvaluateAreaReputation(const CommercialArea& area, int currentWeek, int storeCount, const AreaUsageWeek& usage, const CommercialAreaSettings& settings);
