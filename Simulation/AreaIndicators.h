#pragma once
#include "AreaLedger.h"
#include "CommercialArea.h"
#include "ResidentSimSettings.h"

// 商業エリアの4指標(P2-C2): 集客力・回遊性・消費効率・評判。住民の行動には影響しない(見るだけ)。
// 「予測」(構成と設定から求めた、行先としての選ばれやすさ)と「実績」(台帳に積み上がった、実際の来訪・利用・売上)を、分けて持つ。
// 実績の割り算は、分母(来訪者)が0なら「未計測」(0とは別)。値が0なのか、まだ測れていないのかを、区別できる。
struct IndicatorValue
{
    bool measured = false;      // 測れたか(分母が0・記録が無い時は false)
    float value = 0.0f;         // measured が true の時だけ意味を持つ
};

struct AreaIndicators
{
    // ---- 集客力 ----
    // 予測 = 適合 × 充実度の魅力 × 周知の倍率 × 評判。区画の抽選(ChooseArea)の重みから、距離(住民ごと)と、今の目的(1つの用途)を除いた物。
    //   適合 = 用途ごとの 供給量 ÷ 頭打ち(下限あり、上限1) の平均(用途の数で割る)。
    float attractionPredicted = 0.0f;
    float predictedFit = 0.0f;
    float predictedCoverage = 1.0f;
    float predictedPublish = 0.0f;
    float predictedReputation = 1.0f;
    IndicatorValue visitorsLastWeek;    // 実績: 先週の来訪者数(記録がある時だけ測れた)
    IndicatorValue visitorsTotal;       // 実績: 通算の来訪者数

    // ---- 回遊性 ----
    int storeCount = 0;                 // 予測の材料: 立ち寄り先の豊富さ
    int coveredPurposes = 0;
    float richness = 0.0f;              // 豊富さ(0〜1) = 0.5 × (店の数−1)÷3 + 0.5 × 充実度÷用途の数
    IndicatorValue visitsPerVisitorLastWeek;  // 実績: 来訪者1人あたりの延べ利用(施設の数)
    IndicatorValue visitsPerVisitorTotal;
    int stopoversTotal = 0;             // 実績: 区画内の立ち寄りの回数(通算)

    // ---- 消費効率 ----
    IndicatorValue salesPerVisitorLastWeek;   // 実績: 来訪者1人あたりの実売上(入店していない施設への配分は無い)
    IndicatorValue salesPerVisitorTotal;

    // ---- 評判 ----
    float reputation = 1.0f;            // 区画の評判(P2-D。週末に更新される)
    bool reputationFixed = false;       // true=固定値(区画の評判を渡さなかった時)

    // 集計の整合(来訪者 <= 延べ利用 など)。falseなら、集計が重複している疑い。
    bool consistent = true;
};

// record: 区画の台帳の記録(まだ無ければ nullptr)。
// reputation: 区画の評判(CommercialArea::reputation)。渡さなければ、中立の1.0(固定)。
AreaIndicators ComputeAreaIndicators(const AreaComposition& composition, bool published, const AreaRecord* record, const ResidentSimSettings& settings, float reputation = -1.0f);
