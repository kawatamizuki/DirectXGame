#include "FacilityLedger.h"

FacilityRecord& FacilityLedger::GetOrCreate(uint32_t objectId, BuildingType type)
{
    for (FacilityRecord& record : m_records)
    {
        if (record.objectId == objectId)
        {
            return record;
        }
    }
    FacilityRecord record;
    record.objectId = objectId;
    record.type = type;
    m_records.push_back(record);
    return m_records.back();
}

void FacilityLedger::RecordVisit(uint32_t objectId, BuildingType type, int price)
{
    FacilityRecord& record = GetOrCreate(objectId, type);
    record.thisWeek.Add(price);
    record.thisMonth.Add(price);
    record.total.Add(price);
}

void FacilityLedger::OnWeekEnd(bool monthEnds)
{
    for (FacilityRecord& record : m_records)
    {
        record.lastWeek = record.thisWeek;
        record.thisWeek.Clear();
        if (monthEnds)
        {
            record.thisMonth.Clear();
        }
    }
}

namespace
{
    template <typename Pick>
    FacilityTally Sum(const std::vector<FacilityRecord>& records, Pick pick)
    {
        FacilityTally sum;
        for (const FacilityRecord& record : records)
        {
            const FacilityTally& tally = pick(record);
            sum.visits += tally.visits;
            sum.sales += tally.sales;
        }
        return sum;
    }
}

FacilityTally FacilityLedger::SumThisWeek() const { return Sum(m_records, [](const FacilityRecord& r) -> const FacilityTally& { return r.thisWeek; }); }
FacilityTally FacilityLedger::SumLastWeek() const { return Sum(m_records, [](const FacilityRecord& r) -> const FacilityTally& { return r.lastWeek; }); }
FacilityTally FacilityLedger::SumThisMonth() const { return Sum(m_records, [](const FacilityRecord& r) -> const FacilityTally& { return r.thisMonth; }); }
FacilityTally FacilityLedger::SumTotal() const { return Sum(m_records, [](const FacilityRecord& r) -> const FacilityTally& { return r.total; }); }

const FacilityRecord* FacilityLedger::Find(uint32_t objectId) const
{
    for (const FacilityRecord& record : m_records)
    {
        if (record.objectId == objectId)
        {
            return &record;
        }
    }
    return nullptr;
}

void FacilityLedger::SetRecord(const FacilityRecord& record)
{
    for (FacilityRecord& existing : m_records)
    {
        if (existing.objectId == record.objectId)
        {
            existing = record;
            return;
        }
    }
    m_records.push_back(record);
}
