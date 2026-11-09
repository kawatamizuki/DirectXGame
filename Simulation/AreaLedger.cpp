#include "AreaLedger.h"

AreaRecord& AreaLedger::GetOrCreate(uint32_t areaId)
{
    for (AreaRecord& record : m_records)
    {
        if (record.areaId == areaId) { return record; }
    }
    AreaRecord record;
    record.areaId = areaId;
    m_records.push_back(record);
    return m_records.back();
}

void AreaLedger::RecordAreaOuting(uint32_t areaId)
{
    if (areaId == 0) { return; }
    AreaRecord& record = GetOrCreate(areaId);
    ++record.thisWeek.areaOutings;
    ++record.thisMonth.areaOutings;
    ++record.total.areaOutings;
}

void AreaLedger::RecordVisit(uint32_t areaId, int price, bool firstInOuting, bool areaWasPurpose)
{
    if (areaId == 0) { return; }
    AreaRecord& record = GetOrCreate(areaId);
    for (AreaTally* tally : { &record.thisWeek, &record.thisMonth, &record.total })
    {
        ++tally->visits;
        tally->sales += price;
        if (firstInOuting)
        {
            ++tally->visitors;
            if (areaWasPurpose) { ++tally->purposeVisitors; }
        }
    }
}

void AreaLedger::OnWeekEnd(bool monthEnds)
{
    for (AreaRecord& record : m_records)
    {
        record.lastWeek = record.thisWeek;
        record.thisWeek.Clear();
        if (monthEnds) { record.thisMonth.Clear(); }
    }
}

const AreaRecord* AreaLedger::Find(uint32_t areaId) const
{
    for (const AreaRecord& record : m_records)
    {
        if (record.areaId == areaId) { return &record; }
    }
    return nullptr;
}

void AreaLedger::SetRecord(const AreaRecord& record)
{
    for (AreaRecord& existing : m_records)
    {
        if (existing.areaId == record.areaId) { existing = record; return; }
    }
    m_records.push_back(record);
}
