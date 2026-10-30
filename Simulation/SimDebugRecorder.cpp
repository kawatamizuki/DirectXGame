#include "SimDebugRecorder.h"
#include <algorithm>
#include <cmath>

namespace
{
    // ペナルティの段階の「極端に大きい」境界(Editorの設定の上限10より、ずっと大きい)。実行中に最大の段階を下げても、誤検出しないための余裕。
    constexpr int kPenaltyHardLimit = 100;

    // 体力が、住民の基準の上限を、これだけ超えてもよい(浮動小数点の丸め)。
    constexpr float kStaminaEpsilon = 0.01f;

    bool IsMovingState(ResidentAgentState state)
    {
        return state == ResidentAgentState::GoingOut || state == ResidentAgentState::ReturningHome;
    }
}

// ---------------------------------------------------------------------------
// 不変条件
// ---------------------------------------------------------------------------
uint32_t CheckResidentInvariants(const ResidentAgent& agent, const SimInvariantInput& input)
{
    static const ResidentSimSettings kDefaultSettings;
    const ResidentSimSettings& settings = input.settings ? *input.settings : kDefaultSettings;
    uint32_t mask = 0;

    // NaN/Inf(他の判定が、NaNで意味を失うので、先に調べる)。
    const bool finite = std::isfinite(agent.stamina) && std::isfinite(agent.lifeNeed) && std::isfinite(agent.activityNeed)
        && std::isfinite(agent.satisfaction) && std::isfinite(agent.visitHoursLeft)
        && std::isfinite(agent.unmetLifeHours) && std::isfinite(agent.unmetActivityHours) && std::isfinite(agent.satisfiedHours)
        && std::isfinite(agent.traits.staminaCapBase) && input.markerPositionFinite;
    if (!finite)
    {
        mask |= SimAnomalyBit(SimAnomaly::NonFiniteValue);
    }
    else
    {
        // 体力: 負はありえない。上限は、住民の基準(ペナルティで実効の上限が下がっても、今の体力は自動では下がらないので、基準と比べる)。
        if (agent.stamina < 0.0f || agent.stamina > agent.traits.staminaCapBase + kStaminaEpsilon)
        {
            mask |= SimAnomalyBit(SimAnomaly::StaminaOutOfRange);
        }
        if (agent.lifeNeed < 0.0f || agent.lifeNeed > 100.0f || agent.activityNeed < 0.0f || agent.activityNeed > 100.0f)
        {
            mask |= SimAnomalyBit(SimAnomaly::NeedOutOfRange);
        }
        if (agent.satisfaction < 0.0f || agent.satisfaction > settings.satisfactionMax + kStaminaEpsilon)
        {
            mask |= SimAnomalyBit(SimAnomaly::SatisfactionOutOfRange);
        }
    }

    if (agent.penaltyLevel < 0 || agent.penaltyLevel > kPenaltyHardLimit)
    {
        mask |= SimAnomalyBit(SimAnomaly::PenaltyOutOfRange);
    }
    if (agent.outingStopCount < 0)
    {
        mask |= SimAnomalyBit(SimAnomaly::StopCountNegative);
    }

    // 移動中の経路。経路が2点未満の移動は、到着として扱われる(設計上ありえる)ので、違反にしない。添字が経路の外にある場合だけ違反。
    if (IsMovingState(agent.state) && agent.waypoints.size() >= 2 && agent.currentWaypointIndex >= agent.waypoints.size())
    {
        mask |= SimAnomalyBit(SimAnomaly::WaypointIndexOutOfRange);
    }

    // 行き先の施設: 外出中(向かっている・利用中)は、必ずある。帰宅中・家・住居なし・立ち往生は、0でよい(解体時などに0へ戻す設計)。
    if (agent.state == ResidentAgentState::GoingOut || agent.state == ResidentAgentState::Visiting)
    {
        if (agent.targetFacilityId == 0 || !input.targetObjectExists)
        {
            mask |= SimAnomalyBit(SimAnomaly::MissingTargetFacility);
        }
    }

    // 家: idが0でないなら、オブジェクトがある。家にいる住民は、idが0ではない。住居なしの住民は、idが0。
    if (agent.homeObjectId != 0 && !input.homeObjectExists)
    {
        mask |= SimAnomalyBit(SimAnomaly::MissingHome);
    }
    if (agent.state == ResidentAgentState::AtHome && agent.homeObjectId == 0)
    {
        mask |= SimAnomalyBit(SimAnomaly::MissingHome);
    }
    if (agent.state == ResidentAgentState::Homeless && agent.homeObjectId != 0)
    {
        mask |= SimAnomalyBit(SimAnomaly::HomelessWithHome);
    }
    return mask;
}

// ---------------------------------------------------------------------------
// 一時停止の条件
// ---------------------------------------------------------------------------
bool SimBreakpoints::Matches(const SimEvent& event) const
{
    if (residentId != 0 && event.residentId != residentId)
    {
        return false;
    }
    if (onViolation && event.result == SimEventResult::Violation)
    {
        return true;
    }
    if (onWarning && (event.result == SimEventResult::Warning || event.result == SimEventResult::Violation))
    {
        return true;
    }
    if (kindMask != 0 && (kindMask & (1u << static_cast<uint32_t>(event.kind))) != 0)
    {
        return resultMask == 0 || (resultMask & (1u << static_cast<uint32_t>(event.result))) != 0;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 記録
// ---------------------------------------------------------------------------
void SimDebugRecorder::SetCapacity(size_t eventCapacity, size_t detailCapacity)
{
    m_eventCapacity = (std::max)(static_cast<size_t>(1), eventCapacity);
    m_detailCapacity = (std::max)(static_cast<size_t>(1), detailCapacity);
    TrimEvents();
    TrimDetails();
}

void SimDebugRecorder::StartNewRun()
{
    ++m_runId;
    m_events.clear();
    m_details.clear();
    m_residents.clear();
    m_tickGate = SimTickGate{};
    ClearPause(); // 別の世界になるので、止める印は外す(止める条件は、残す)
    m_eventsRecordedTotal = 0;
    m_eventsDropped = 0;
    m_detailsRecordedTotal = 0;
    m_detailsDropped = 0;
    m_anomaliesRaised = 0;
    for (ISimRecordObserver* observer : m_observers)
    {
        observer->OnWorldReset();
    }
    // 通し番号(m_nextEventId・m_nextDetailId・m_nextOutingId)は戻さない(古い番号と混ざらないように)。
}

void SimDebugRecorder::TrimEvents()
{
    while (m_events.size() > m_eventCapacity)
    {
        m_events.pop_front();
        ++m_eventsDropped;
    }
}

void SimDebugRecorder::TrimDetails()
{
    while (m_details.size() > m_detailCapacity)
    {
        m_details.pop_front();
        ++m_detailsDropped;
    }
}

uint64_t SimDebugRecorder::AddEvent(SimEvent event)
{
    if (!m_enabled)
    {
        return 0;
    }
    SimResidentRecord& resident = m_residents[event.residentId];

    // 外出の始まり: 新しい外出の番号を付ける。
    if (event.kind == SimEventKind::OutingRoll || event.kind == SimEventKind::StrandedRecovered)
    {
        resident.currentOutingId = m_nextOutingId++;
    }
    event.outingId = resident.currentOutingId;
    if (event.testId == 0)
    {
        event.testId = resident.activeTestId; // 行動テストで強制している間のイベントには、テストの番号を付ける
    }
    event.eventId = m_nextEventId++;
    event.runId = m_runId;
    const uint64_t id = event.eventId;

    // 外出の終わり(このイベントまでが、その外出)。
    const bool mainSelectFailed = event.kind == SimEventKind::FacilitySelect && event.stopIndex == 0 && event.result == SimEventResult::NotMet;
    const bool ends = event.kind == SimEventKind::OutingBlocked || event.kind == SimEventKind::ReturnHome
        || event.kind == SimEventKind::BecameStranded || event.kind == SimEventKind::BecameHomeless || mainSelectFailed;

    if (event.kind == SimEventKind::Anomaly)
    {
        ++m_anomaliesRaised;
    }
    // 止める条件に当てはまれば、止める印を立てる(最初の1件だけ。実際に止めるのは、呼び出し側)。
    if (!m_pauseActive && m_breakpoints.Matches(event))
    {
        m_pauseActive = true;
        m_pausedEventId = id;
    }
    for (ISimRecordObserver* observer : m_observers)
    {
        observer->OnEvent(event);
    }
    m_events.push_back(std::move(event));
    ++m_eventsRecordedTotal;
    TrimEvents();

    if (ends)
    {
        resident.currentOutingId = 0;
    }
    return id;
}

uint64_t SimDebugRecorder::AddDetail(SimDecisionDetail detail)
{
    if (!m_enabled)
    {
        return 0;
    }
    if (detail.testId == 0)
    {
        const auto resident = m_residents.find(detail.residentId);
        if (resident != m_residents.end())
        {
            detail.testId = resident->second.activeTestId;
        }
    }
    detail.detailId = m_nextDetailId++;
    const uint64_t id = detail.detailId;
    for (ISimRecordObserver* observer : m_observers)
    {
        observer->OnDetail(detail);
    }
    m_details.push_back(std::move(detail));
    ++m_detailsRecordedTotal;
    TrimDetails();
    return id;
}

void SimDebugRecorder::NoteOutingRoll(uint32_t residentId, OutingChanceBranch branch, float chance, float roll, bool hit, int week, float hour)
{
    if (!m_enabled)
    {
        return;
    }
    SimOutingRollSummary& summary = m_residents[residentId].roll;
    ++summary.checks;
    if (hit)
    {
        ++summary.hits;
        summary.consecutiveMisses = 0;
    }
    else
    {
        ++summary.consecutiveMisses;
    }
    summary.lastBranch = branch;
    summary.lastChance = chance;
    summary.lastRoll = roll;
    summary.lastHit = hit;
    summary.lastWeek = week;
    summary.lastHour = hour;
    for (ISimRecordObserver* observer : m_observers)
    {
        observer->OnOutingRoll(week, branch, chance, hit);
    }
}

void SimDebugRecorder::SetActiveTest(uint32_t residentId, uint32_t testId)
{
    if (!m_enabled)
    {
        return;
    }
    if (testId == 0)
    {
        // 外す時は、住民の記録を、新しく作らない(既にある時だけ)。
        const auto it = m_residents.find(residentId);
        if (it != m_residents.end())
        {
            it->second.activeTestId = 0;
        }
        return;
    }
    m_residents[residentId].activeTestId = testId;
}

uint32_t SimDebugRecorder::ActiveTestId(uint32_t residentId) const
{
    const SimResidentRecord* record = FindResident(residentId);
    return record ? record->activeTestId : 0;
}

void SimDebugRecorder::NoteTickGate(const SimTickGate& gate)
{
    if (!m_enabled)
    {
        return;
    }
    m_tickGate = gate;
    m_tickGate.valid = true;
}

uint32_t SimDebugRecorder::UpdateAnomalyState(uint32_t residentId, uint32_t currentMask)
{
    if (!m_enabled)
    {
        return 0;
    }
    // 異常が無く、これまでも記録が無い住民は、住民の記録を作らない(全住民ぶんの空の記録を増やさないため)。
    auto it = m_residents.find(residentId);
    if (it == m_residents.end())
    {
        if (currentMask == 0)
        {
            return 0;
        }
        it = m_residents.emplace(residentId, SimResidentRecord{}).first;
    }
    const uint32_t newly = currentMask & ~it->second.activeAnomalies;
    it->second.activeAnomalies = currentMask;
    return newly;
}

const SimDecisionDetail* SimDebugRecorder::FindDetail(uint64_t detailId) const
{
    if (detailId == 0 || m_details.empty())
    {
        return nullptr;
    }
    // detailIdは追加した順に増えるので、二分探索で探せる。
    auto it = std::lower_bound(m_details.begin(), m_details.end(), detailId,
        [](const SimDecisionDetail& detail, uint64_t id) { return detail.detailId < id; });
    if (it == m_details.end() || it->detailId != detailId)
    {
        return nullptr;
    }
    return &*it;
}

const SimEvent* SimDebugRecorder::FindEvent(uint64_t eventId) const
{
    if (eventId == 0 || m_events.empty())
    {
        return nullptr;
    }
    auto it = std::lower_bound(m_events.begin(), m_events.end(), eventId,
        [](const SimEvent& event, uint64_t id) { return event.eventId < id; });
    if (it == m_events.end() || it->eventId != eventId)
    {
        return nullptr;
    }
    return &*it;
}

const SimResidentRecord* SimDebugRecorder::FindResident(uint32_t residentId) const
{
    const auto it = m_residents.find(residentId);
    return it == m_residents.end() ? nullptr : &it->second;
}

uint32_t SimDebugRecorder::CurrentOutingId(uint32_t residentId) const
{
    const SimResidentRecord* record = FindResident(residentId);
    return record ? record->currentOutingId : 0;
}

std::vector<const SimEvent*> SimDebugRecorder::RecentEventsOf(uint32_t residentId, size_t count) const
{
    std::vector<const SimEvent*> result;
    for (auto it = m_events.rbegin(); it != m_events.rend() && result.size() < count; ++it)
    {
        if (it->residentId == residentId)
        {
            result.push_back(&*it);
        }
    }
    return result;
}

SimRecorderStats SimDebugRecorder::GetStats() const
{
    SimRecorderStats stats;
    stats.runId = m_runId;
    stats.eventsRecordedTotal = m_eventsRecordedTotal;
    stats.eventsDropped = m_eventsDropped;
    stats.detailsRecordedTotal = m_detailsRecordedTotal;
    stats.detailsDropped = m_detailsDropped;
    stats.anomaliesRaised = m_anomaliesRaised;
    stats.eventCount = m_events.size();
    stats.detailCount = m_details.size();
    stats.eventCapacity = m_eventCapacity;
    stats.detailCapacity = m_detailCapacity;
    stats.oldestEventId = m_events.empty() ? 0 : m_events.front().eventId;

    size_t bytes = m_events.size() * sizeof(SimEvent) + m_residents.size() * (sizeof(SimResidentRecord) + 32);
    for (const SimDecisionDetail& detail : m_details)
    {
        bytes += sizeof(SimDecisionDetail) + detail.evaluations.capacity() * sizeof(CandidateEvaluation) + detail.history.capacity() * sizeof(VisitRecord);
    }
    stats.estimatedBytes = bytes;
    return stats;
}
