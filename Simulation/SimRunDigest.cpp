#include "SimRunDigest.h"

namespace
{
    constexpr uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;
}

int SimFirstDivergence(const std::vector<SimDigestTraceEntry>& a, const std::vector<SimDigestTraceEntry>& b)
{
    const size_t common = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < common; ++i)
    {
        if (a[i].hashAfter != b[i].hashAfter)
        {
            return static_cast<int>(i);
        }
    }
    return a.size() == b.size() ? -1 : static_cast<int>(common);
}

void SimRunDigest::PushTrace(uint8_t kind, uint32_t residentId, int week, float hour, float a)
{
    if (m_trace.size() >= kMaxTrace)
    {
        return;
    }
    SimDigestTraceEntry entry;
    entry.hashAfter = m_result.hash;
    entry.kind = kind;
    entry.residentId = residentId;
    entry.week = week;
    entry.hour = hour;
    entry.a = a;
    m_trace.push_back(entry);
}

void SimRunDigest::Reset()
{
    m_trace.clear();
    m_result = SimRunDigestResult{};
    m_result.hash = kFnvOffset;
}

void SimRunDigest::MixBytes(const void* data, size_t size)
{
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    uint64_t hash = m_result.hash;
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
    m_result.hash = hash;
}

void SimRunDigest::OnEvent(const SimEvent& event)
{
    ++m_result.events;
    Mix(static_cast<uint8_t>(0xE1));    // イベントの印(外出判定の抽選と、混ざらないように)
    Mix(event.residentId);
    Mix(event.week);
    Mix(event.hour);
    Mix(static_cast<uint8_t>(event.kind));
    Mix(static_cast<uint8_t>(event.result));
    Mix(static_cast<uint8_t>(event.reason));
    Mix(static_cast<uint8_t>(event.anomaly));
    Mix(event.stopIndex);
    Mix(event.facilityId);
    Mix(static_cast<uint8_t>(event.testId != 0 ? 1 : 0));
    // 行動テストの指示の a は、テストの番号(世界を作り直しても、通し番号が進む)なので、混ぜない。
    // 行動テストの指示の a は、テストの番号(世界を作り直しても、通し番号が進む)なので、混ぜない。
    Mix(event.kind == SimEventKind::TestCommand ? 0.0f : event.a);
    Mix(event.b);
    Mix(event.c);
    const SimResidentSnapshot& s = event.snapshot;
    Mix(static_cast<uint8_t>(s.state));
    Mix(s.stamina);
    Mix(s.staminaCap);
    Mix(s.lifeNeed);
    Mix(s.activityNeed);
    Mix(s.satisfaction);
    Mix(s.penaltyLevel);
    Mix(s.outingStopCount);
    Mix(s.homeObjectId);
    Mix(s.targetFacilityId);
    PushTrace(static_cast<uint8_t>(event.kind), event.residentId, event.week, event.hour, event.a);
}

void SimRunDigest::OnOutingRoll(int week, OutingChanceBranch branch, float chance, bool naturalHit)
{
    ++m_result.rolls;
    Mix(static_cast<uint8_t>(0xA7));
    Mix(week);
    Mix(static_cast<uint8_t>(branch));
    Mix(chance);
    Mix(static_cast<uint8_t>(naturalHit ? 1 : 0));
    PushTrace(255, 0, week, 0.0f, chance);
}
