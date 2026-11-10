#pragma once
#include <cstdint>
#include <vector>

// 商業エリアの区画ごとの、来訪・利用・売上の集計(P2-C1。住民が実際に起こした行動の結果を、起きた時点の所属で数える)。
// 区画の所属は、施設の建築・解体や区画の移動で変わるので、「今の所属で、後から数え直す」のではなく、利用が起きた時に、その時の所属で積み上げる。
// 数える物は、混ざらないよう、別々の数字にする:
//  ・areaOutings     エリアを目的に外出した人数(外出の理由が「エリア」で、その区画を目的地に選んで、実際に出発した回数)。
//  ・visitors        エリア内の施設を、実際に利用した人数(来訪者)。目的が何であっても数える。**同じ外出で、同じ区画は、1回だけ**(最初の利用で数える)。
//  ・purposeVisitors visitors のうち、その区画を目的に出た人(エリア目的の外出で、実際に、その区画の施設を使えた人)。areaOutings との差=エリアに向かったが、使えなかった人。
//  ・visits          エリア内の延べ施設利用回数(1人が3店舗を使えば3)。
//  ・sales           エリア内の実売上(利用が起きた時の施設の値段の合計)。エリアを目的に選んでも、利用が無ければ、増えない。
// 期間の意味: 週・月・通算の別に持つ。人数は、「その期間に起きた、最初の利用」で数える(外出が週をまたいでも、最初の利用の週に数える)。
// 区画のIDは、再利用しないので、区画を削除した後も、記録は残る(過去の実績として)。
struct AreaTally
{
    int areaOutings = 0;
    int visitors = 0;
    int purposeVisitors = 0;
    int visits = 0;
    int sales = 0;
    int stopovers = 0;          // 区画の中の施設から、同じ区画の別の施設へ、立ち寄りを始めた回数(P2-C2。区画内の回遊)

    void Clear() { *this = AreaTally{}; }
};

struct AreaRecord
{
    uint32_t areaId = 0;
    AreaTally thisWeek;         // 今の週
    AreaTally lastWeek;         // 直前の週(週末で、今の週がここへ移る)
    AreaTally thisMonth;        // 今の月(月末で0に戻る)
    AreaTally total;            // 通算
};

class AreaLedger
{
public:
    // エリア目的の外出が、実際に始まった(目的地の区画が決まって、出発した)。
    void RecordAreaOuting(uint32_t areaId);

    // 区画の中の施設から、同じ区画の別の施設への、立ち寄りが始まった(P2-C2)。
    void RecordAreaStopover(uint32_t areaId);

    // 区画の中の施設の利用が、1回終わった。price: 施設の値段(売上)。
    // firstInOuting: この外出で、この区画の利用が、初めてか(trueなら、来訪者に数える)。
    // areaWasPurpose: この外出が、この区画を目的にしたものか(firstInOuting の時だけ、purposeVisitors に数える)。
    void RecordVisit(uint32_t areaId, int price, bool firstInOuting, bool areaWasPurpose);

    // 週末: 今の週を、直前の週へ移す。月末(monthEnds==true)なら、月の集計も0に戻す。
    void OnWeekEnd(bool monthEnds);

    const AreaRecord* Find(uint32_t areaId) const;
    const std::vector<AreaRecord>& GetRecords() const { return m_records; }
    void Clear() { m_records.clear(); }

    // セーブデータからの復元用(同じ区画の記録が既にあれば、置き換える)。
    void SetRecord(const AreaRecord& record);

    size_t EstimateBytes() const { return sizeof(*this) + m_records.capacity() * sizeof(AreaRecord); }

private:
    AreaRecord& GetOrCreate(uint32_t areaId);

    std::vector<AreaRecord> m_records;
};
