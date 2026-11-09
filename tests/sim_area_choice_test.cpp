// P2-C1(商業エリアへの外出)のルールの単体テスト: 区画の抽選(ChooseArea)・理由の選び直し・区画ごとの集計(AreaLedger)。
// 受け入れ条件:
//   1. 区画の重みは、道のり(使える所属施設までの距離)で下がる。中心への直線距離ではなく、渡された道のりだけで決まる。
//   2. 所属施設が無い・使える所属施設が無い・候補にならない(未周知で倍率0)区画は、抽選の対象にならない。
//   3. 施設の数・供給量が増えても、過剰には有利にならない(頭打ち)。
//   4. 「エリア」を引いても、今回の目的に合う区画が無ければ、同じ乱数で、他の理由から選び直す(乱数は引き直さない)。
//   5. 区画ごとの集計は、「目的にした人数」「実際に使った人数」「延べ利用」「売上」が別々。同じ外出で同じ区画は、人数を1回だけ数える。
#include <cmath>
#include <cstdio>
#include <vector>
#include "AreaLedger.h"
#include "ResidentLogic.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)

static ResidentAgent MakeAgent(const ResidentSimSettings& s)
{
    ResidentAgent a;
    InitializeResident(a, 7, s);
    a.homeObjectId = 1;
    a.traits.spontaneity = 1.0f;
    a.traits.distanceTolerance = 1.0f;
    a.penaltyLevel = 0;
    a.traits.staminaCapBase = 100.0f;
    a.stamina = 100.0f;
    a.lifeNeed = 100.0f;
    a.activityNeed = 100.0f;
    return a;
}

static AreaCandidate MakeCandidate(uint32_t id, float pathLegacyCells, float supply, int members = 3, int usable = 3, bool published = true, int covered = 1)
{
    AreaCandidate c;
    c.areaId = id;
    c.published = published;
    c.memberCount = members;
    c.usableCount = usable;
    c.nearestPathLength = WorldScale::FromLegacy(pathLegacyCells);
    c.supplyForPurpose = supply;
    c.coveredPurposes = covered;
    return c;
}

// ---- 区画の抽選 ----
static void TestChooseArea()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    OutingPurposeChoice purpose;
    std::vector<AreaCandidateEvaluation> evals;
    AreaChoiceTrace trace;

    // 近い区画は、遠い区画より、重みが大きい(距離の効き方は P2-0 のもの)。
    {
        std::vector<AreaCandidate> candidates = { MakeCandidate(1, 4.0f, 3.0f), MakeCandidate(2, 40.0f, 3.0f) };
        const int picked = ChooseArea(a, candidates, purpose, s, 0.0f, evals, &trace);
        CHECK(picked == 0);
        CHECK(evals.size() == 2);
        CHECK(evals[0].weight > evals[1].weight);
        CHECK(evals[0].chosen && !evals[1].chosen);
        CHECK(trace.pickedIndex == 0);
        CHECK(std::fabs(evals[0].chance + evals[1].chance - 1.0f) < 1e-4f);
        CHECK(std::fabs(trace.totalWeight - (evals[0].weight + evals[1].weight)) < 1e-4f);
    }

    // 乱数が大きいと、後ろの候補が選ばれる(重みの累積で決まる)。
    {
        std::vector<AreaCandidate> candidates = { MakeCandidate(1, 4.0f, 3.0f), MakeCandidate(2, 4.0f, 3.0f) };
        CHECK(ChooseArea(a, candidates, purpose, s, 0.10f, evals) == 0);
        CHECK(ChooseArea(a, candidates, purpose, s, 0.90f, evals) == 1);
    }

    // 除外: 所属施設なし / 使える所属施設なし / 未周知(倍率0)。除外された区画は、重み0・選ばれない。
    {
        std::vector<AreaCandidate> candidates = {
            MakeCandidate(1, 4.0f, 3.0f, 0, 0),                       // 所属なし
            MakeCandidate(2, 4.0f, 3.0f, 3, 0),                       // 使える施設なし
            MakeCandidate(3, 4.0f, 3.0f, 3, 3, false),                // 未周知(既定の倍率0)
            MakeCandidate(4, 30.0f, 3.0f)                             // 遠いが、唯一の対象
        };
        const int picked = ChooseArea(a, candidates, purpose, s, 0.5f, evals, &trace);
        CHECK(picked == 3);
        CHECK(evals[0].reject == AreaReject::NoMember);
        CHECK(evals[1].reject == AreaReject::NoUsableMember);
        CHECK(evals[2].reject == AreaReject::NotAvailable);
        CHECK(evals[3].reject == AreaReject::None);
        CHECK(evals[0].weight == 0.0f && evals[1].weight == 0.0f && evals[2].weight == 0.0f);
        CHECK(evals[3].chance > 0.999f);
    }

    // 全て除外なら、選べない(-1)。
    {
        std::vector<AreaCandidate> candidates = { MakeCandidate(1, 4.0f, 3.0f, 3, 0) };
        CHECK(ChooseArea(a, candidates, purpose, s, 0.5f, evals, &trace) == -1);
        CHECK(trace.pickedIndex == -1);
        CHECK(ChooseArea(a, {}, purpose, s, 0.5f, evals, &trace) == -1);
    }

    // 未周知の倍率を上げると、未周知の区画も候補になる(周知による差は、設定で後から調整できる)。同じ条件なら、周知済みの方が重い(倍率1.0 > 0.4)。
    {
        ResidentSimSettings t = s;
        t.areaUnpublishedFactor = 0.4f;
        std::vector<AreaCandidate> candidates = { MakeCandidate(1, 4.0f, 3.0f, 3, 3, true), MakeCandidate(2, 4.0f, 3.0f, 3, 3, false) };
        ChooseArea(a, candidates, purpose, t, 0.5f, evals);
        CHECK(evals[1].reject == AreaReject::None);
        CHECK(evals[0].weight > evals[1].weight);
    }

    // 頭打ち: 供給量が、頭打ち(既定3.0)を超えて増えても、重みは増えない。少ない供給量は、下がる。
    {
        std::vector<AreaCandidate> candidates = { MakeCandidate(1, 4.0f, 3.0f), MakeCandidate(2, 4.0f, 30.0f), MakeCandidate(3, 4.0f, 0.5f) };
        ChooseArea(a, candidates, purpose, s, 0.5f, evals);
        CHECK(std::fabs(evals[0].weight - evals[1].weight) < 1e-5f);
        CHECK(evals[2].weight < evals[0].weight);
        CHECK(evals[2].fitFactor >= s.areaFitFloor);
    }

    // 充実度の魅力は、上限がある(用途が全部そろっても、増え方の分まで)。
    {
        std::vector<AreaCandidate> candidates = { MakeCandidate(1, 4.0f, 3.0f, 3, 3, true, 1), MakeCandidate(2, 4.0f, 3.0f, 3, 3, true, kUsagePurposeCount), MakeCandidate(3, 4.0f, 3.0f, 3, 3, true, 99) };
        ChooseArea(a, candidates, purpose, s, 0.5f, evals);
        CHECK(std::fabs(evals[0].coverageFactor - 1.0f) < 1e-5f);
        CHECK(std::fabs(evals[1].coverageFactor - (1.0f + s.areaCoverageGain)) < 1e-4f);
        CHECK(std::fabs(evals[2].coverageFactor - evals[1].coverageFactor) < 1e-5f);
    }

    // 評判は、重みに掛かる(P2-Dまでは1.0)。
    {
        ResidentSimSettings t = s;
        t.areaReputation = 0.5f;
        std::vector<AreaCandidate> one = { MakeCandidate(1, 4.0f, 3.0f) };
        ChooseArea(a, one, purpose, s, 0.5f, evals);
        const float base = evals[0].weight;
        ChooseArea(a, one, purpose, t, 0.5f, evals);
        CHECK(std::fabs(evals[0].weight - base * 0.5f) < 1e-4f);
    }

    // 抽選の統計: 近い区画が、遠い区画より、多く選ばれる。重みが全く無い区画は、選ばれない。
    {
        std::vector<AreaCandidate> candidates = { MakeCandidate(1, 4.0f, 3.0f), MakeCandidate(2, 25.0f, 3.0f), MakeCandidate(3, 4.0f, 3.0f, 3, 0) };
        SimRandom r(123);
        int counts[3] = { 0, 0, 0 };
        for (int i = 0; i < 4000; ++i)
        {
            const int picked = ChooseArea(a, candidates, purpose, s, r.Next01(), evals);
            if (picked >= 0) { ++counts[picked]; }
        }
        CHECK(counts[0] > counts[1] * 2);
        CHECK(counts[1] > 0);                  // 遠い区画も、ごくまれに選ばれる(連続的で、しきい値は無い)
        CHECK(counts[2] == 0);
    }

    // 距離は、渡された道のり(使える所属施設までの道のり)だけで決まる。区画の座標・中心は、入力に無い(中心への直線距離を使わない)。
    {
        std::vector<AreaCandidate> candidates = { MakeCandidate(1, 8.0f, 3.0f) };
        ChooseArea(a, candidates, purpose, s, 0.5f, evals);
        const float w8 = evals[0].distanceFactor;
        candidates[0].nearestPathLength = WorldScale::FromLegacy(16.0f);
        ChooseArea(a, candidates, purpose, s, 0.5f, evals);
        CHECK(evals[0].distanceFactor < w8);
        CHECK(std::fabs(evals[0].legacyDistance - 16.0f) < 1e-3f);
    }
}

// ---- 理由の選び直し ----
static void TestFallback()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    SimRandom r(77);
    int fallbacks = 0, checked = 0;
    for (int i = 0; i < 3000; ++i)
    {
        OutingReason reason = OutingReason::Quick;
        OutingReasonTrace trace;
        if (!ChooseOutingReason(a, s, r, true, reason, &trace)) { continue; }
        if (reason != OutingReason::Area) { continue; }
        ++checked;
        OutingReason redo = OutingReason::Area;
        CHECK(ReevaluateReasonWithoutArea(trace, redo));
        CHECK(redo != OutingReason::Area);                 // 「エリア」を除いて、選び直す
        if (redo != OutingReason::Area) { ++fallbacks; }
        // 同じ乱数・同じ重みなら、毎回、同じ結果(乱数は引き直さない)。
        OutingReason again = OutingReason::Area;
        CHECK(ReevaluateReasonWithoutArea(trace, again) && again == redo);
    }
    CHECK(checked > 100);
    CHECK(fallbacks == checked);

    // 「エリア」の重みだけがあって、他が全て0の場合は、選び直せない。
    OutingReasonTrace only;
    for (int i = 0; i < kOutingReasonCount; ++i) { only.weight[i] = 0.0f; }
    only.weight[static_cast<int>(OutingReason::Area)] = 1.0f;
    only.roll = 0.3f;
    OutingReason out = OutingReason::Quick;
    CHECK(!ReevaluateReasonWithoutArea(only, out));
}

// ---- 区画ごとの集計 ----
static void TestAreaLedger()
{
    AreaLedger ledger;
    CHECK(ledger.GetRecords().empty());
    CHECK(ledger.Find(1) == nullptr);

    // エリアを目的に出発した人数と、実際に使った人数は、別。
    ledger.RecordAreaOuting(1);
    ledger.RecordAreaOuting(1);
    ledger.RecordAreaOuting(1);                            // 3人が、区画1を目的に出発
    ledger.RecordVisit(1, 500, true, true);                // 1人目: 最初の利用(目的の来訪者)
    ledger.RecordVisit(1, 300, false, true);               // 1人目: 同じ外出の2軒目(人数は増えない・利用と売上は増える)
    ledger.RecordVisit(1, 400, true, true);                // 2人目
    ledger.RecordVisit(1, 200, true, false);               // 目的が別の人(通りがかり・立ち寄り)も、来訪者に数える。目的の来訪者には数えない
    const AreaRecord* r = ledger.Find(1);
    CHECK(r != nullptr);
    CHECK(r->total.areaOutings == 3);
    CHECK(r->total.visitors == 3);
    CHECK(r->total.purposeVisitors == 2);
    CHECK(r->total.visits == 4);
    CHECK(r->total.sales == 1400);
    CHECK(r->total.areaOutings - r->total.purposeVisitors == 1);   // 向かったが、まだ(または結局)使っていない人
    CHECK(r->thisWeek.sales == 1400 && r->thisMonth.sales == 1400);

    // 利用が無ければ、売上は増えない(目的にしただけでは、売上にならない)。
    ledger.RecordAreaOuting(2);
    const AreaRecord* r2 = ledger.Find(2);
    CHECK(r2 && r2->total.areaOutings == 1 && r2->total.sales == 0 && r2->total.visits == 0 && r2->total.visitors == 0);

    // 区画0(エリアなし)は、数えない。
    ledger.RecordAreaOuting(0);
    ledger.RecordVisit(0, 100, true, true);
    CHECK(ledger.Find(0) == nullptr);

    // 週末: 今の週が、先週へ移る。月は、月末だけ0に戻る。通算は、変わらない。
    ledger.OnWeekEnd(false);
    r = ledger.Find(1);
    CHECK(r->thisWeek.sales == 0 && r->lastWeek.sales == 1400 && r->thisMonth.sales == 1400 && r->total.sales == 1400);
    ledger.RecordVisit(1, 100, true, false);
    ledger.OnWeekEnd(true);
    r = ledger.Find(1);
    CHECK(r->lastWeek.sales == 100 && r->thisMonth.sales == 0 && r->total.sales == 1500);
    // 整合: 通算 = 先週までの合計 + 今の週(月・週の合計は、通算を超えない)。
    CHECK(r->total.visitors >= r->lastWeek.visitors + r->thisWeek.visitors);

    // セーブからの復元: 同じ区画の記録は、置き換える。
    AreaRecord restored;
    restored.areaId = 1;
    restored.total.sales = 999;
    ledger.SetRecord(restored);
    CHECK(ledger.Find(1)->total.sales == 999);
    CHECK(ledger.GetRecords().size() == 2);

    ledger.Clear();
    CHECK(ledger.GetRecords().empty());
}

int main()
{
    TestChooseArea();
    TestFallback();
    TestAreaLedger();
    if (g_fail == 0) { std::printf("ALL PASSED\n"); return 0; }
    std::printf("%d FAILED\n", g_fail);
    return 1;
}
