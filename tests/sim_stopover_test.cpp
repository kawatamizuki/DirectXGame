// P2-C2(立ち寄りの補完性・引き寄せ・エリア内の補正・4指標)のルールの単体テスト。
// 受け入れ条件:
//   1. 補完性は、区画に依らない(区画の情報を入力に持たない)。今使った用途と違う用途が強みの施設ほど高い。同じ大分類でも、買い足しとまとめ買いのように、用途が違えば高い。
//   2. 需要が満たされていても、補完性・引き寄せは0にならない(自発的な追加利用の余地)。
//   3. 引き寄せは、近くに使える施設が多いほど、発生率が上がる。ただし、控えめで頭打ち。遠い・体力が足りない・使用済みの施設は、数えない。
//   4. 補完性 × エリア内の補正は、上限で頭打ち(二重に強くならない)。
//   5. 乱数の消費は変わらない(ChooseFacility は、補正の有無に関わらず、1回)。既存の連続的な確率・回数の減衰は維持。
//   6. 4指標: 来訪者0人の回遊性・消費効率は「未計測」(0とは別)。来訪人数・延べ利用・売上は重複しない。
#include <cmath>
#include <cstdio>
#include <vector>
#include "AreaIndicators.h"
#include "ResidentLogic.h"
#include "FacilityData.h"

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
    a.lifeNeed = 60.0f;
    a.activityNeed = 60.0f;
    return a;
}

static const FacilityDefinition& Def(BuildingType t) { return *FindFacilityDefinition(t); }

static void TestComplement()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    ComplementBreakdown cafeAfterMovie, cinemaAfterMovie, superAfterTopUp, convAfterTopUp, convAfterBulk;
    const float cafe = ComputeComplementFactor(a, Def(BuildingType::Cafe), UsagePurpose::Movie, s, &cafeAfterMovie);
    const float cinema = ComputeComplementFactor(a, Def(BuildingType::Cinema), UsagePurpose::Movie, s, &cinemaAfterMovie);
    ComputeComplementFactor(a, Def(BuildingType::Supermarket), UsagePurpose::TopUp, s, &superAfterTopUp);
    ComputeComplementFactor(a, Def(BuildingType::Shop), UsagePurpose::TopUp, s, &convAfterTopUp);
    ComputeComplementFactor(a, Def(BuildingType::Shop), UsagePurpose::BulkBuy, s, &convAfterBulk);

    // 映画の後は、映画館より、カフェ(別の用途が強み)の方が、補完性が高い。
    CHECK(cafe > cinema);
    CHECK(cinemaAfterMovie.novelty < 0.01f);                 // 映画館は、映画が一番の強み=同じ用途
    CHECK(cafeAfterMovie.novelty > 0.5f);
    // 同じ大分類(日用品)でも、買い足しの後に、まとめ買いが強みのスーパーは、補完性が高い。コンビニ(同じ買い足し)は、ほぼ補完しない。
    CHECK(superAfterTopUp.novelty > 0.5f);
    CHECK(convAfterTopUp.novelty < 0.01f);
    CHECK(convAfterBulk.novelty > 0.5f);                     // まとめ買いの後は、買い足しが強みのコンビニが補完
    // 倍率は1以上で、補完性の強さを超えない。
    CHECK(cafe >= 1.0f && cafe <= 1.0f + s.complementGain + 1e-4f);
    CHECK(cinema >= 1.0f && cinema < 1.0f + 0.01f);

    // 需要が満たされていても、0にはならない(自発的な追加利用の余地。下限)。
    a.activityNeed = 100.0f; a.lifeNeed = 100.0f;
    ComplementBreakdown satisfied;
    const float satisfiedFactor = ComputeComplementFactor(a, Def(BuildingType::Cafe), UsagePurpose::Movie, s, &satisfied);
    CHECK(satisfied.needTerm > 0.0f && std::fabs(satisfied.needTerm - s.complementNeedFloor) < 1e-4f);
    CHECK(satisfiedFactor > 1.0f);
    a.activityNeed = 0.0f;
    ComplementBreakdown starving;
    const float starvingFactor = ComputeComplementFactor(a, Def(BuildingType::Cafe), UsagePurpose::Movie, s, &starving);
    CHECK(starvingFactor > satisfiedFactor);                 // 不足しているほど、高い
    CHECK(std::fabs(starving.needTerm - 1.0f) < 1e-4f);

    // 強さ0なら、補正なし。
    ResidentSimSettings off = s;
    off.complementGain = 0.0f;
    CHECK(std::fabs(ComputeComplementFactor(a, Def(BuildingType::Cafe), UsagePurpose::Movie, off) - 1.0f) < 1e-6f);
}

static void TestAffinityCap()
{
    ResidentSimSettings s;
    CHECK(std::fabs(ComputeStopoverAffinity(1.3f, 1.2f, s) - 1.56f) < 1e-4f);        // 掛け合わせ
    CHECK(std::fabs(ComputeStopoverAffinity(1.5f, 1.8f, s) - s.stopoverAffinityCap) < 1e-4f);  // 上限で頭打ち(1.5 × 1.8 = 2.7 → 2.0)
    CHECK(std::fabs(ComputeStopoverAffinity(1.0f, 1.0f, s) - 1.0f) < 1e-6f);
}

static std::vector<StopoverPullCandidate> Nearby(std::initializer_list<std::pair<BuildingType, float>> items)
{
    std::vector<StopoverPullCandidate> list;
    uint32_t id = 100;
    for (const auto& item : items) { StopoverPullCandidate c; c.objectId = id++; c.type = item.first; c.distance = item.second; list.push_back(c); }
    return list;
}

static void TestPull()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    StopoverPullBreakdown b0, b1, b2, b3;
    const float none = ComputeStopoverPull(a, UsagePurpose::Movie, {}, 1, s, &b0);
    const float one = ComputeStopoverPull(a, UsagePurpose::Movie, Nearby({ { BuildingType::Cafe, 1.0f } }), 1, s, &b1);
    const float two = ComputeStopoverPull(a, UsagePurpose::Movie, Nearby({ { BuildingType::Cafe, 1.0f }, { BuildingType::Shop, 1.5f } }), 1, s, &b2);
    const float many = ComputeStopoverPull(a, UsagePurpose::Movie, Nearby({ { BuildingType::Cafe, 1.0f }, { BuildingType::Shop, 1.5f }, { BuildingType::Supermarket, 1.5f }, { BuildingType::Cafe, 2.0f }, { BuildingType::Cinema, 2.0f } }), 1, s, &b3);
    CHECK(none == 0.0f && std::fabs(b0.multiplier - 1.0f) < 1e-6f);
    CHECK(one > 0.0f && two > one && many > two);            // 近くに使える施設が多いほど、引き寄せが強い
    CHECK(many < 1.0f);                                      // 頭打ち
    CHECK(b3.multiplier <= 1.0f + s.stopoverPullGain + 1e-4f);  // 控えめ(最大 +強さ)

    // 遠い施設は、ほとんど数えない(距離の重み)。
    const float far = ComputeStopoverPull(a, UsagePurpose::Movie, Nearby({ { BuildingType::Cafe, 60.0f } }), 1, s);
    CHECK(far < one * 0.2f);

    // 体力が足りない施設は、使えないので数えない。
    ResidentAgent tired = a;
    tired.stamina = 0.5f;
    StopoverPullBreakdown bt;
    const float tiredPull = ComputeStopoverPull(tired, UsagePurpose::Movie, Nearby({ { BuildingType::Cafe, 1.0f }, { BuildingType::Cinema, 1.0f } }), 1, s, &bt);
    CHECK(bt.considered == 2 && bt.usable == 0 && tiredPull == 0.0f);

    // 今の外出で使った施設は、数えない。
    ResidentAgent used = a;
    VisitRecord rec; rec.facilityObjectId = 100;
    used.history.insert(used.history.begin(), rec);
    StopoverPullBreakdown bu;
    ComputeStopoverPull(used, UsagePurpose::Movie, Nearby({ { BuildingType::Cafe, 1.0f } }), 1, s, &bu);
    CHECK(bu.considered == 0);

    // 同じ用途が強みの候補でも、引き寄せは0にならない(同じ用途でも寄る余地)。
    CHECK(ComputeStopoverPull(a, UsagePurpose::Movie, Nearby({ { BuildingType::Cinema, 1.0f } }), 1, s) > 0.0f);

    // 需要が満たされていても、0にはならない。
    ResidentAgent full = a;
    full.lifeNeed = 100.0f; full.activityNeed = 100.0f;
    CHECK(ComputeStopoverPull(full, UsagePurpose::Movie, Nearby({ { BuildingType::Cafe, 1.0f } }), 1, s) > 0.0f);
}

static void TestChanceWithPull()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    StopoverChanceBreakdown b0, b1;
    const float base = ComputeStopoverChance(a, s, &b0, 0.0f);
    const float pulled = ComputeStopoverChance(a, s, &b1, 0.6f);
    CHECK(pulled > base);
    CHECK(std::fabs(pulled / base - (1.0f + s.stopoverPullGain * 0.6f)) < 1e-3f);
    CHECK(std::fabs(b1.pull - 0.6f) < 1e-6f);
    // 既定の引数(引き寄せなし)は、これまでと同じ。回数の減衰・体力は維持される。
    CHECK(std::fabs(ComputeStopoverChance(a, s) - base) < 1e-6f);
    ResidentAgent twice = a;
    twice.outingStopCount = 1;
    CHECK(ComputeStopoverChance(twice, s, nullptr, 0.6f) < pulled);
    ResidentAgent tired = a;
    tired.stamina = 20.0f;
    CHECK(ComputeStopoverChance(tired, s, nullptr, 0.6f) < pulled);
    // 引き寄せがあっても、確率は1を超えない。
    ResidentSimSettings big = s;
    big.stopoverBaseChance = 1.0f; big.stopoverPullGain = 5.0f;
    CHECK(ComputeStopoverChance(a, big, nullptr, 1.0f) <= 1.0f);
}

static FacilityCandidate MakeCandidate(uint32_t id, BuildingType t, float complement, float area)
{
    FacilityCandidate c;
    c.objectId = id; c.type = t; c.reachable = true; c.pathLength = 1.0f;
    c.complementFactor = complement; c.areaBoost = area;
    return c;
}

static void TestChooseFacilityAffinity()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    a.activityNeed = 30.0f;
    OutingPurposeChoice purpose;
    purpose.category = FacilityCategory::Leisure;
    purpose.purpose = UsagePurpose::Light;
    purpose.reason = OutingReason::Quick;

    std::vector<FacilityCandidate> candidates = {
        MakeCandidate(1, BuildingType::Cafe, 1.0f, 1.0f), MakeCandidate(2, BuildingType::Cafe, 1.5f, 1.0f),
        MakeCandidate(3, BuildingType::Cafe, 1.0f, 1.3f), MakeCandidate(4, BuildingType::Cafe, 1.5f, 1.8f) };
    std::vector<CandidateEvaluation> evals;
    SimRandom r(5);
    const uint32_t before = r.GetState();
    FacilityChoiceTrace trace;
    ChooseFacility(a, candidates, purpose, s, r, evals, 1, &trace);       // 立ち寄り(2軒目)
    CHECK(evals.size() == 4);
    CHECK(std::fabs(evals[0].affinity - 1.0f) < 1e-5f);
    CHECK(evals[1].weight > evals[0].weight);                             // 補完性で重い
    CHECK(evals[2].weight > evals[0].weight);                             // 同じ区画で重い
    CHECK(std::fabs(evals[3].affinity - s.stopoverAffinityCap) < 1e-4f);  // 1.5 × 1.8 = 2.7 → 上限
    CHECK(evals[3].weight > evals[1].weight && evals[3].weight > evals[2].weight);
    CHECK(evals[0].chance > 0.0f);                                        // 補正が無い候補も、確率が0にならない(区画の外の施設も選ばれる)

    // 主目的の施設選び(visitsThisOuting=0)では、補正はかからない。
    std::vector<CandidateEvaluation> mainEvals;
    SimRandom r2(5);
    ChooseFacility(a, candidates, purpose, s, r2, mainEvals, 0);
    CHECK(std::fabs(mainEvals[3].affinity - 1.0f) < 1e-6f);
    CHECK(std::fabs(mainEvals[1].weight - mainEvals[0].weight) < 1e-5f);

    // 乱数の消費は、補正の有無に関わらず、1回(状態が同じだけ進む)。
    SimRandom r3(5), r4(5);
    std::vector<FacilityCandidate> plain = candidates;
    for (FacilityCandidate& c : plain) { c.complementFactor = 1.0f; c.areaBoost = 1.0f; }
    std::vector<CandidateEvaluation> e1, e2;
    ChooseFacility(a, candidates, purpose, s, r3, e1, 1);
    ChooseFacility(a, plain, purpose, s, r4, e2, 1);
    CHECK(r3.GetState() == r4.GetState());
    CHECK(r3.GetState() != before);
}

static void TestIndicators()
{
    ResidentSimSettings s;
    CommercialAreaSettings as;
    AreaComposition comp = ComputeAreaComposition({ BuildingType::Shop, BuildingType::Cafe, BuildingType::Cinema, BuildingType::Supermarket }, as);

    // 記録が無い: 実績は全て未計測。予測は出る。
    AreaIndicators none = ComputeAreaIndicators(comp, true, nullptr, s);
    CHECK(none.attractionPredicted > 0.0f);
    CHECK(!none.visitorsLastWeek.measured && !none.visitsPerVisitorTotal.measured && !none.salesPerVisitorTotal.measured);
    CHECK(none.reputationFixed && std::fabs(none.reputation - 1.0f) < 1e-6f);

    // 未周知なら、予測の周知の倍率は0(既定)=集客力の予測は0。構成の評価は残る。
    AreaIndicators unpub = ComputeAreaIndicators(comp, false, nullptr, s);
    CHECK(unpub.attractionPredicted == 0.0f && unpub.richness > 0.0f);
    // 店が多く用途がそろうほど、予測が高い。
    AreaComposition small = ComputeAreaComposition({ BuildingType::Shop }, as);
    CHECK(ComputeAreaIndicators(comp, true, nullptr, s).attractionPredicted > ComputeAreaIndicators(small, true, nullptr, s).attractionPredicted);
    CHECK(none.richness > ComputeAreaIndicators(small, true, nullptr, s).richness);

    // 記録はあるが、来訪者が0人: 来訪者数は「0」(測れている)、回遊性・消費効率は「未計測」。
    AreaRecord zero; zero.areaId = 1; zero.total.areaOutings = 3;
    AreaIndicators z = ComputeAreaIndicators(comp, true, &zero, s);
    CHECK(z.visitorsTotal.measured && z.visitorsTotal.value == 0.0f);
    CHECK(!z.visitsPerVisitorTotal.measured && !z.salesPerVisitorTotal.measured);

    // 来訪者がいる: 1人あたりが測れる。人数・延べ利用・売上が重複しない。
    AreaLedger ledger;
    ledger.RecordAreaOuting(1);
    ledger.RecordVisit(1, 500, true, true);       // 1人目 1軒目
    ledger.RecordVisit(1, 300, false, true);      // 1人目 2軒目(人数は増えない)
    ledger.RecordVisit(1, 400, true, false);      // 2人目
    ledger.RecordAreaStopover(1);
    AreaIndicators m = ComputeAreaIndicators(comp, true, ledger.Find(1), s);
    CHECK(m.visitorsTotal.measured && m.visitorsTotal.value == 2.0f);
    CHECK(m.visitsPerVisitorTotal.measured && std::fabs(m.visitsPerVisitorTotal.value - 1.5f) < 1e-5f);   // 3回 ÷ 2人
    CHECK(m.salesPerVisitorTotal.measured && std::fabs(m.salesPerVisitorTotal.value - 600.0f) < 1e-3f);   // 1200円 ÷ 2人
    CHECK(m.stopoversTotal == 1);
    CHECK(m.consistent);
    CHECK(!m.visitsPerVisitorLastWeek.measured);   // 先週は、まだ無い(週末の前)
    // 不整合(来訪者 > 延べ利用)は、検出できる。
    AreaRecord bad; bad.areaId = 2; bad.total.visitors = 5; bad.total.visits = 3;
    CHECK(!ComputeAreaIndicators(comp, true, &bad, s).consistent);
    // 区画内の立ち寄りは、週・月・通算に積まれ、週末で今週が先週へ移る。
    ledger.OnWeekEnd(false);
    CHECK(ledger.Find(1)->lastWeek.stopovers == 1 && ledger.Find(1)->thisWeek.stopovers == 0 && ledger.Find(1)->total.stopovers == 1);
}

int main()
{
    TestComplement();
    TestAffinityCap();
    TestPull();
    TestChanceWithPull();
    TestChooseFacilityAffinity();
    TestIndicators();
    if (g_fail == 0) { std::printf("ALL PASSED\n"); return 0; }
    std::printf("%d FAILED\n", g_fail);
    return 1;
}
