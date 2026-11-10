#include "AreaIndicators.h"
#include <algorithm>

namespace
{
    float Clamp01(float v) { return (std::max)(0.0f, (std::min)(1.0f, v)); }

    IndicatorValue Ratio(int numerator, int denominator)
    {
        IndicatorValue v;
        if (denominator > 0)
        {
            v.measured = true;
            v.value = static_cast<float>(numerator) / static_cast<float>(denominator);
        }
        return v;
    }
}

AreaIndicators ComputeAreaIndicators(const AreaComposition& composition, bool published, const AreaRecord* record, const ResidentSimSettings& settings)
{
    AreaIndicators out;

    // 集客力の予測。
    const float cap = (std::max)(0.01f, settings.areaFitSupplyCap);
    float fit = 0.0f;
    for (int p = 0; p < kUsagePurposeCount; ++p)
    {
        fit += (composition.storeCount > 0) ? Clamp01((std::max)(settings.areaFitFloor, composition.supply[p] / cap)) : 0.0f;
    }
    out.predictedFit = fit / static_cast<float>(kUsagePurposeCount);
    const float coverage = static_cast<float>((std::max)(0, composition.coveredPurposes - 1)) / static_cast<float>((std::max)(1, kUsagePurposeCount - 1));
    out.predictedCoverage = 1.0f + (std::max)(0.0f, settings.areaCoverageGain) * Clamp01(coverage);
    out.predictedPublish = published ? settings.areaPublishedFactor : settings.areaUnpublishedFactor;
    out.predictedReputation = settings.areaReputation;
    out.attractionPredicted = out.predictedFit * out.predictedCoverage * out.predictedPublish * out.predictedReputation;

    // 回遊性の予測(立ち寄り先の豊富さ)。
    out.storeCount = composition.storeCount;
    out.coveredPurposes = composition.coveredPurposes;
    out.richness = (composition.storeCount > 0)
        ? 0.5f * Clamp01(static_cast<float>(composition.storeCount - 1) / 3.0f) + 0.5f * Clamp01(static_cast<float>(composition.coveredPurposes) / static_cast<float>(kUsagePurposeCount))
        : 0.0f;

    // 実績。記録が無い(台帳に、まだ何も積まれていない)なら、全て未計測。
    out.reputation = settings.areaReputation;
    out.reputationFixed = true;
    if (record)
    {
        out.visitorsLastWeek = { true, static_cast<float>(record->lastWeek.visitors) };
        out.visitorsTotal = { true, static_cast<float>(record->total.visitors) };
        out.visitsPerVisitorLastWeek = Ratio(record->lastWeek.visits, record->lastWeek.visitors);
        out.visitsPerVisitorTotal = Ratio(record->total.visits, record->total.visitors);
        out.salesPerVisitorLastWeek = Ratio(record->lastWeek.sales, record->lastWeek.visitors);
        out.salesPerVisitorTotal = Ratio(record->total.sales, record->total.visitors);
        out.stopoversTotal = record->total.stopovers;
        for (const AreaTally* t : { &record->thisWeek, &record->lastWeek, &record->thisMonth, &record->total })
        {
            // 来訪者(1人1回)は、延べ利用(施設ごと)以下。目的の来訪者は、来訪者以下。
            if (t->visitors > t->visits || t->purposeVisitors > t->visitors) { out.consistent = false; }
        }
    }
    return out;
}
