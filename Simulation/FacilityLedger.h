#pragma once
#include <cstdint>
#include <vector>
#include "BuildingType.h"

// 施設ごとの、来訪者数と売上の集計(住民が実際に利用した結果の記録)。
// 固定の売上バフではなく、利用が起きるたびにRecordVisitで積み上げる。到達できなかった外出は、ここに入らない。
// 週ごと・月ごと・通算で持つ(週末・月末にOnWeekEnd / OnMonthEndで区切る)。
struct FacilityTally
{
    int visits = 0;     // 来訪者数
    int sales = 0;      // 売上(円。住民1人が1回使うごとに、施設の値段が入る)

    void Add(int price) { visits += 1; sales += price; }
    void Clear() { visits = 0; sales = 0; }
};

struct FacilityRecord
{
    uint32_t objectId = 0;          // 施設のGameObject::id
    BuildingType type = BuildingType::Shop;
    FacilityTally thisWeek;         // 今の週
    FacilityTally lastWeek;         // 直前の週(週末で、今の週がここへ移る)
    FacilityTally thisMonth;        // 今の月(月末で0に戻る)
    FacilityTally total;            // 通算

    // 解体された施設の記録か。解体後も、過去の売上・来訪者数は、記録として残す(消さない。合計にも入る)。
    // 施設の種類(type)も残るので、後から「種類別の収益」などを分析できる。解体済みの記録には、もう利用は足されない。
    bool demolished = false;
};

class FacilityLedger
{
public:
    // 施設で1回の利用があった(来訪者+1、売上+price)。初めての施設は、記録を作る。
    void RecordVisit(uint32_t objectId, BuildingType type, int price);

    // 週末: 今の週を、直前の週へ移す。月末(monthEnds==true)なら、月の集計も0に戻す。
    void OnWeekEnd(bool monthEnds);

    // 全ての施設の合計(今の週・直前の週・今の月・通算)。
    FacilityTally SumThisWeek() const;
    FacilityTally SumLastWeek() const;
    FacilityTally SumThisMonth() const;
    FacilityTally SumTotal() const;

    // objectIdの施設を、解体済みとして印を付ける(記録は残す)。記録が無い(一度も使われていない)施設は、何もしない。
    void MarkDemolished(uint32_t objectId);

    const FacilityRecord* Find(uint32_t objectId) const;
    const std::vector<FacilityRecord>& GetRecords() const { return m_records; }

    void Clear() { m_records.clear(); }

    // セーブデータからの復元用(同じ施設の記録が既にあれば、置き換える)。
    void SetRecord(const FacilityRecord& record);

private:
    FacilityRecord& GetOrCreate(uint32_t objectId, BuildingType type);

    std::vector<FacilityRecord> m_records;
};
