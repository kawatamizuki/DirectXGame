#include "SimStats.h"
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// 確率の判定の集計
// ---------------------------------------------------------------------------
void SimRollTally::Add(float chance, bool hit)
{
    const double p = (std::max)(0.0, (std::min)(1.0, static_cast<double>(chance)));
    ++trials;
    if (hit)
    {
        ++hits;
    }
    sumChance += p;
    sumVariance += p * (1.0 - p);
}

double SimRollTally::StdDev() const
{
    return std::sqrt((std::max)(0.0, sumVariance));
}

void SimWilsonInterval(uint64_t successes, uint64_t trials, double& low, double& high)
{
    if (trials == 0)
    {
        low = 0.0;
        high = 1.0;
        return;
    }
    const double z = 1.96;
    const double n = static_cast<double>(trials);
    const double p = static_cast<double>((std::min)(successes, trials)) / n;
    const double denominator = 1.0 + z * z / n;
    const double center = (p + z * z / (2.0 * n)) / denominator;
    const double margin = z * std::sqrt(p * (1.0 - p) / n + z * z / (4.0 * n * n)) / denominator;
    low = (std::max)(0.0, center - margin);
    high = (std::min)(1.0, center + margin);
}

// ---------------------------------------------------------------------------
// 書き込み
// ---------------------------------------------------------------------------
namespace
{
    // 立ち寄りを考えた「最初の結果」になる理由か(1回の判定が、ちょうど1つに数えられる)。
    bool IsStopConsiderationReason(SimReason reason)
    {
        switch (reason)
        {
        case SimReason::StopNoHome:
        case SimReason::StopMaxReached:
        case SimReason::StopTooLate:
        case SimReason::StopLowStamina:
        case SimReason::StopRollMiss:
        case SimReason::StopRollHit:
            return true;
        default:
            return false;
        }
    }

    size_t ReasonIndex(SimReason reason)
    {
        const size_t index = static_cast<size_t>(reason);
        return index < kSimReasonCount ? index : 0;
    }

    void Touch(SimResidentStat& stat, int week)
    {
        if (stat.firstWeek < 0)
        {
            stat.firstWeek = week;
        }
        stat.lastWeek = week;
    }
}

void SimStats::AdvanceWeek(int week)
{
    if (m_startWeek < 0)
    {
        m_startWeek = week;
        m_currentWeek = week;
        return;
    }
    if (week <= m_currentWeek)
    {
        return; // 週は戻らない(同じ週・古い週のイベントは、今週に数える)
    }
    for (SimStatsScopeData& scope : m_scopes)
    {
        SimWeekRow row;
        row.week = m_currentWeek;
        row.bucket = scope.thisWeek;
        scope.weekly.push_back(row);
        if (scope.weekly.size() > kSimStatsMaxWeeks)
        {
            scope.weekly.erase(scope.weekly.begin());
        }
        scope.lastWeek = scope.thisWeek;
        scope.hasLastWeek = true;
        scope.thisWeek = SimStatBucket{};
    }
    m_currentWeek = week;
}

void SimStats::Reset()
{
    for (SimStatsScopeData& scope : m_scopes)
    {
        scope = SimStatsScopeData{};
    }
    m_startWeek = -1;
    m_currentWeek = -1;
    m_counted = 0;
}

void SimStats::OnOutingRoll(int week, OutingChanceBranch branch, float chance, bool naturalHit)
{
    AdvanceWeek(week);
    ++m_counted;
    SimStatsScopeData& scope = Data(SimStatScope::Normal);
    for (SimStatBucket* bucket : { &scope.total, &scope.thisWeek })
    {
        switch (branch)
        {
        case OutingChanceBranch::NeedDriven:            bucket->outingRollNeed.Add(chance, naturalHit); break;
        case OutingChanceBranch::Spontaneous:           bucket->outingRollSpontaneous.Add(chance, naturalHit); break;
        case OutingChanceBranch::SpontaneousLowStamina: bucket->outingRollLowStamina.Add(chance, naturalHit); break;
        }
    }
}

void SimStats::OnEvent(const SimEvent& event)
{
    AdvanceWeek(event.week);
    ++m_counted;
    SimStatsScopeData& scope = Data(event.testId != 0 ? SimStatScope::Test : SimStatScope::Normal);
    SimResidentStat& resident = scope.residents[event.residentId];
    Touch(resident, event.week);

    const bool facilityKnown = event.facilityId != 0;

    // 通算と今週の、両方に同じ数を足す。
    auto add = [&](auto&& apply)
    {
        apply(scope.total);
        apply(scope.thisWeek);
    };

    switch (event.kind)
    {
    case SimEventKind::OutingRoll:
        add([&](SimStatBucket& b) { ++b.outingStarted; });
        ++resident.outings;
        break;

    case SimEventKind::PurposeChoice:
        if (event.stopIndex == 0)
        {
            add([&](SimStatBucket& b) { ++b.purposeChosen; });
        }
        break;

    case SimEventKind::FacilitySelect:
        if (event.stopIndex == 0)
        {
            if (event.result == SimEventResult::Success)
            {
                add([&](SimStatBucket& b) { ++b.mainSelectOk; });
                if (facilityKnown)
                {
                    SimFacilityStat& facility = scope.facilities[event.facilityId];
                    ++facility.chosen;
                    ++facility.chosenMain;
                }
            }
            else
            {
                add([&](SimStatBucket& b) { ++b.outingFailed; ++b.outingFailReason[ReasonIndex(event.reason)]; });
            }
        }
        else if (event.result == SimEventResult::Success && event.reason == SimReason::StopStarted)
        {
            add([&](SimStatBucket& b) { ++b.stopStarted; ++b.stopReason[ReasonIndex(SimReason::StopStarted)]; });
            ++resident.stopovers;
            if (facilityKnown)
            {
                SimFacilityStat& facility = scope.facilities[event.facilityId];
                ++facility.chosen;
                ++facility.chosenStop;
            }
        }
        else if (event.result == SimEventResult::NotMet && event.reason == SimReason::StopNoPickable)
        {
            add([&](SimStatBucket& b) { ++b.stopReason[ReasonIndex(SimReason::StopNoPickable)]; });
        }
        break;

    case SimEventKind::OutingBlocked:
        add([&](SimStatBucket& b) { ++b.outingFailed; ++b.outingFailReason[ReasonIndex(event.reason)]; });
        break;

    case SimEventKind::MoveStart:
        if (event.stopIndex == 0 && event.reason != SimReason::TestApplied) // 行き先の変更(テスト)で、新しく歩き始めた物は、出発に数えない
        {
            add([&](SimStatBucket& b) { ++b.departed; });
        }
        break;

    case SimEventKind::Arrived:
        if (event.result == SimEventResult::Info)
        {
            if (event.stopIndex == 0)
            {
                add([&](SimStatBucket& b) { ++b.arrived; });
            }
            if (facilityKnown)
            {
                ++scope.facilities[event.facilityId].arrivals;
            }
        }
        break;

    case SimEventKind::VisitFinished:
        if (event.result == SimEventResult::Success)
        {
            const bool main = event.stopIndex == 0;
            add([&](SimStatBucket& b)
            {
                ++b.visits;
                b.revenue += event.a;
                b.lifeRecovered += event.b;
                b.staminaUsed += event.c;
                if (main) { ++b.mainVisits; } else { ++b.stopVisits; }
            });
            ++resident.visits;
            resident.revenue += event.a;
            if (facilityKnown)
            {
                SimFacilityStat& facility = scope.facilities[event.facilityId];
                ++facility.visits;
                facility.revenue += event.a;
                facility.lifeRecovered += event.b;
            }
        }
        break;

    case SimEventKind::StopoverCheck:
        if (IsStopConsiderationReason(event.reason))
        {
            const bool rolled = event.reason == SimReason::StopRollHit || event.reason == SimReason::StopRollMiss;
            add([&](SimStatBucket& b)
            {
                ++b.stopConsidered;
                ++b.stopReason[ReasonIndex(event.reason)];
                if (rolled)
                {
                    b.stopRoll.Add(event.a, event.reason == SimReason::StopRollHit);
                }
            });
        }
        else if (event.result == SimEventResult::NotMet)
        {
            // 抽選に当選した後の失敗(家が無い・道が無い・同じカテゴリの施設が無い・食事系が続く)。判定の回数には足さず、結果の別だけ数える。
            add([&](SimStatBucket& b) { ++b.stopReason[ReasonIndex(event.reason)]; });
        }
        break;

    case SimEventKind::ReturnHome:
        add([&](SimStatBucket& b) { ++b.returnedHome; });
        break;
    case SimEventKind::ReturnRerouted:
        add([&](SimStatBucket& b) { ++b.rerouted; });
        break;
    case SimEventKind::OutingAborted:
        add([&](SimStatBucket& b) { ++b.aborted; });
        ++resident.aborted;
        break;
    case SimEventKind::BecameStranded:
        add([&](SimStatBucket& b) { ++b.stranded; });
        ++resident.stranded;
        break;
    case SimEventKind::StrandedRecovered:
        add([&](SimStatBucket& b) { ++b.strandedRecovered; });
        break;
    case SimEventKind::BecameHomeless:
        add([&](SimStatBucket& b) { ++b.homeless; });
        ++resident.homeless;
        break;

    case SimEventKind::TestCommand:
        if (event.reason == SimReason::TestRejected)
        {
            add([&](SimStatBucket& b) { ++b.testRejected; });
        }
        else
        {
            add([&](SimStatBucket& b) { ++b.testApplied; });
        }
        break;

    default:
        break;
    }
}

void SimStats::OnDetail(const SimDecisionDetail& detail)
{
    if (detail.evaluations.empty())
    {
        return;
    }
    AdvanceWeek(detail.week);
    ++m_counted;
    SimStatsScopeData& scope = Data(detail.testId != 0 ? SimStatScope::Test : SimStatScope::Normal);
    for (const CandidateEvaluation& evaluation : detail.evaluations)
    {
        SimFacilityStat& facility = scope.facilities[evaluation.candidate.objectId];
        ++facility.candidateIn;
        const size_t reject = static_cast<size_t>(evaluation.reject);
        if (evaluation.reject != CandidateReject::None && reject < kSimCandidateRejectCount)
        {
            ++facility.rejected[reject];
        }
    }
}
