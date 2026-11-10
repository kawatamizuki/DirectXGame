#include "AreaReputation.h"
#include <algorithm>

namespace
{
    float Clamp(float v, float lo, float hi) { return (std::max)(lo, (std::min)(hi, v)); }
}

const char* AreaPlanStateName(AreaPlanState state)
{
    switch (state)
    {
    case AreaPlanState::NotPublished: return "非公開(評判は変わらない)";
    case AreaPlanState::InitialGrace: return "初回の開発猶予中";
    case AreaPlanState::Met:          return "最低進捗を満たしている";
    case AreaPlanState::Reorganizing: return "再整備の猶予中";
    case AreaPlanState::Shortfall:    return "最低進捗に届いていない";
    }
    return "?";
}

AreaReputationResult EvaluateAreaReputation(const CommercialArea& area, int currentWeek, int storeCount, const AreaUsageWeek& usage, const CommercialAreaSettings& s)
{
    AreaReputationResult r;
    r.storeCount = (std::max)(0, storeCount);
    r.newReputation = area.reputation;
    r.target = area.reputation;
    r.belowMinimumSinceWeek = area.belowMinimumSinceWeek;
    r.metMinimumStreakWeeks = area.metMinimumStreakWeeks;
    if (!area.published)
    {
        return r;   // 非公開の仮置き: 何も変わらない
    }

    const AreaThresholds th = ComputeAreaThresholds(area.plannedCapacity, s);   // 計画規模・設定が変わっても、順序が破綻しない値
    const int minStores = th.minStores;
    r.metNow = r.storeCount >= minStores;
    r.hadMetBefore = static_cast<int>(area.achieved) >= static_cast<int>(AreaMilestone::Minimum);   // 履歴(過去に届いた)。今の達成ではない
    r.initialGraceLeft = GraceWeeksLeft(area, currentWeek, s);

    // 再整備の猶予の計測: 下回り始めた週を覚え、最低進捗を recoveryHoldWeeks 週続けて保つまで、取り消さない。
    if (r.metNow)
    {
        r.metMinimumStreakWeeks = area.metMinimumStreakWeeks + 1;
        if (r.metMinimumStreakWeeks >= (std::max)(1, s.recoveryHoldWeeks)) { r.belowMinimumSinceWeek = 0; }
    }
    else
    {
        r.metMinimumStreakWeeks = 0;
        if (r.belowMinimumSinceWeek == 0) { r.belowMinimumSinceWeek = currentWeek; }
    }

    // 計画の履行(今の状態で評価する)。
    if (r.initialGraceLeft > 0)
    {
        r.planState = AreaPlanState::InitialGrace;
    }
    else if (r.metNow)
    {
        r.planState = AreaPlanState::Met;
        // 完成の加点: 計画の完成(最低進捗より多い店舗数)に届いた時だけ。最低進捗に届いただけでは、加点しない。
        if (th.completeStores > th.minStores && r.storeCount >= th.completeStores) { r.planTerm = s.planCompleteBonus; }
    }
    else
    {
        if (r.hadMetBefore)
        {
            r.reorganizeGraceLeft = r.belowMinimumSinceWeek + (std::max)(0, s.reorganizeGraceWeeks) - currentWeek;
        }
        if (r.hadMetBefore && r.reorganizeGraceLeft > 0)
        {
            r.planState = AreaPlanState::Reorganizing;
        }
        else
        {
            r.planState = AreaPlanState::Shortfall;
            const float shortfall = minStores > 0 ? static_cast<float>(minStores - r.storeCount) / static_cast<float>(minStores) : 0.0f;
            r.planTerm = -(std::max)(0.0f, s.planPenaltyMax) * Clamp(shortfall, 0.0f, 1.0f);
        }
    }

    // 実際の利用(猶予中でも働く。計画の履行とは独立)。サンプルが足りない週は、その項は0(測れていない)。
    const int minSamples = (std::max)(1, s.useMinSamples);
    // 成功率は、同じ集団の 成功数 ÷ 結果が決まった数。切り詰めて隠さない(成功数 > 決まった数 は、集計の不整合なので、測れていない扱い)。
    if (usage.resolvedOutings >= minSamples && usage.succeededOutings >= 0 && usage.succeededOutings <= usage.resolvedOutings)
    {
        r.successMeasured = true;
        r.successRate = static_cast<float>(usage.succeededOutings) / static_cast<float>(usage.resolvedOutings);
        const float n = Clamp(s.useSuccessNeutral, 0.01f, 0.99f);
        const float unit = r.successRate >= n ? (r.successRate - n) / (1.0f - n) : (r.successRate - n) / n;
        r.successTerm = (std::max)(0.0f, s.useSuccessWeight) * Clamp(unit, -1.0f, 1.0f);
    }
    if (usage.visits >= minSamples && usage.needNominal > 0.0f)
    {
        r.needMeasured = true;
        r.needEffect = Clamp(usage.needActual / usage.needNominal, 0.0f, 1.0f);
        const float n = Clamp(s.useNeedNeutral, 0.0f, 0.99f);
        r.needTerm = (std::max)(0.0f, s.useNeedWeight) * Clamp((r.needEffect - n) / (1.0f - n), 0.0f, 1.0f);
    }
    r.useTerm = r.successTerm + r.needTerm;

    // 目標値へ、少しずつ近づける(下がるのは遅く、回復も遅い。急な大幅減点を避ける)。
    const float lo = (std::min)(s.reputationMin, s.reputationMax);
    const float hi = (std::max)(s.reputationMin, s.reputationMax);
    r.target = Clamp(1.0f + r.planTerm + r.useTerm, lo, hi);
    const float current = Clamp(area.reputation, lo, hi);
    if (r.target > current) { r.newReputation = (std::min)(r.target, current + (std::max)(0.0f, s.reputationRecoverPerWeek)); }
    else                    { r.newReputation = (std::max)(r.target, current - (std::max)(0.0f, s.reputationDropPerWeek)); }
    r.delta = r.newReputation - area.reputation;
    return r;
}
