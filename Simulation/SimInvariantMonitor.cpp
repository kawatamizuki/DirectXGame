#include "SimInvariantMonitor.h"
#include <algorithm>
#include <cmath>

void SimInvariantMonitor::Clear()
{
    m_flows.clear();
    m_violations.clear();
    m_settled.clear();
}

void SimInvariantMonitor::Raise(SimAnomaly kind, const SimEvent& event, float value)
{
    SimFlowViolation violation;
    violation.kind = kind;
    violation.residentId = event.residentId;
    violation.facilityId = event.facilityId;
    violation.value = value;
    m_violations.push_back(violation);
    ++m_totalViolations;
}

void SimInvariantMonitor::Drain(std::vector<SimFlowViolation>& violations, std::vector<std::pair<uint32_t, uint32_t>>& settled)
{
    violations.swap(m_violations);
    settled.swap(m_settled);
    m_violations.clear();
    m_settled.clear();
}

void SimInvariantMonitor::OnEvent(const SimEvent& event)
{
    // 外出に属さないイベント(ロード直後に続いていた外出など)は、途中から見ても分からないので、追わない。
    if (event.outingId == 0)
    {
        return;
    }
    Flow& flow = m_flows[event.residentId];
    if (flow.outingId != event.outingId)
    {
        flow = Flow{};              // 新しい外出が始まった(外出判定・立ち往生からの復帰)
        flow.outingId = event.outingId;
    }
    const bool forcedByTest = event.testId != 0;    // 行動テストの強制は、通常の流れを、飛ばすことがある

    switch (event.kind)
    {
    case SimEventKind::MoveStart:
        if (flow.visitOpen)
        {
            if (!forcedByTest)
            {
                Raise(SimAnomaly::MoveWhileVisiting, event, static_cast<float>(event.stopIndex));
            }
            flow.visitOpen = false;
        }
        flow.moveStarted = true;
        break;

    case SimEventKind::ReturnStart:
        if (flow.visitOpen)
        {
            if (!forcedByTest)
            {
                Raise(SimAnomaly::ReturnWhileVisiting, event, static_cast<float>(event.stopIndex));
            }
            flow.visitOpen = false;
        }
        flow.moveStarted = false;
        break;

    case SimEventKind::Arrived:
        if (event.result == SimEventResult::Info)   // 施設が無くなっていた到着(Warning)は、利用を始めない
        {
            if (!flow.moveStarted)
            {
                Raise(SimAnomaly::ArrivedWithoutMove, event, static_cast<float>(event.stopIndex));
            }
            flow.moveStarted = false;
            flow.visitOpen = true;
        }
        break;

    case SimEventKind::VisitFinished:
        if (event.result == SimEventResult::Success)
        {
            if (!flow.visitOpen)
            {
                Raise(SimAnomaly::SettleWithoutArrival, event, event.a);    // 到着していない(または、1回の到着で、2回目の売上)
            }
            if (!std::isfinite(event.a) || event.a < 0.0f)
            {
                Raise(SimAnomaly::NegativeRevenue, event, event.a);
            }
            if (std::find(flow.visited.begin(), flow.visited.end(), event.facilityId) != flow.visited.end())
            {
                Raise(SimAnomaly::DuplicateFacilityInOuting, event, static_cast<float>(event.facilityId));
            }
            else
            {
                flow.visited.push_back(event.facilityId);
            }
            m_settled.emplace_back(event.residentId, event.facilityId);
        }
        flow.visitOpen = false;     // 解体されて、売上なしで終わった(Warning)時も、利用は終わり
        break;

    case SimEventKind::FacilitySelect:
        if (event.result == SimEventResult::Success && event.reason == SimReason::StopStarted && event.stopIndex > m_maxStopovers)
        {
            Raise(SimAnomaly::StopCountExceeded, event, static_cast<float>(event.stopIndex));
        }
        break;

    case SimEventKind::ReturnHome:
        if (flow.visitOpen && !forcedByTest)
        {
            Raise(SimAnomaly::ReturnWhileVisiting, event, static_cast<float>(event.stopIndex));
        }
        flow.moveStarted = false;
        flow.visitOpen = false;
        break;

    case SimEventKind::OutingAborted:
    case SimEventKind::BecameStranded:
    case SimEventKind::BecameHomeless:
        flow.moveStarted = false;
        flow.visitOpen = false;
        break;

    default:
        break;
    }
}

size_t SimInvariantMonitor::EstimateBytes() const
{
    size_t bytes = sizeof(*this) + m_violations.capacity() * sizeof(SimFlowViolation) + m_settled.capacity() * sizeof(std::pair<uint32_t, uint32_t>);
    for (const auto& entry : m_flows)
    {
        bytes += sizeof(uint32_t) + sizeof(Flow) + 48 + entry.second.visited.capacity() * sizeof(uint32_t);
    }
    return bytes;
}
