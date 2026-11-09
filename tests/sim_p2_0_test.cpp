// P2-0(住民行動の土台)のルールの単体テスト: 需要の段階・外出の理由・理由ごとの距離・体力(移動では消費しない)・立ち寄りの連続的な確率。
// 受け入れ条件(仕様書 §P2-0):
//   1. 外出の理由が3種類あり、理由ごとに、距離の効き方が違う。
//   2. エリアの外出は、候補が無い間は選ばれない(他の理由から、同じ乱数で選び直す)。乱数の消費は、状態に依らず固定。
//   3. 移動では、体力を消費しない。施設を使う時だけ消費する。2軒目以降は、少し高い。
//   4. 立ち寄りは、体力・需要・理由・回数で、連続的に確率が変わる(固定のしきい値を持たない)。立ち寄り先のカテゴリは、更新された需要で選び直す。
//   5. 生活 50/100・余暇 10/100 → 生活が高い確率(100%ではない)で選ばれる。
#include <cmath>
#include <cstdio>
#include <vector>
#include "ResidentLogic.h"
#include "FacilityData.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

static ResidentAgent MakeAgent(const ResidentSimSettings& s)
{
    ResidentAgent a;
    InitializeResident(a, 7, s);
    a.homeObjectId = 1;
    a.traits.spontaneity = 1.0f;                // 係数を1に固定(式の確認を、単純にする)
    a.traits.distanceTolerance = 1.0f;
    a.penaltyLevel = 0;
    a.traits.staminaCapBase = 100.0f;           // 上限 100
    a.stamina = 100.0f;
    a.lifeNeed = 100.0f;
    a.activityNeed = 100.0f;
    return a;
}

// ---- 需要の段階 ----
static void TestNeedStages()
{
    ResidentSimSettings s;   // 80 / 50 / 25
    CHECK(GetNeedStage(100.0f, s) == NeedStage::Satisfied);
    CHECK(GetNeedStage(80.0f, s) == NeedStage::Satisfied);          // ちょうど80は、充足
    CHECK(GetNeedStage(79.9f, s) == NeedStage::Short);
    CHECK(GetNeedStage(50.0f, s) == NeedStage::Short);              // ちょうど50は、まだ不足(深刻は、50未満)
    CHECK(GetNeedStage(49.9f, s) == NeedStage::Severe);
    CHECK(GetNeedStage(25.0f, s) == NeedStage::Severe);
    CHECK(GetNeedStage(24.9f, s) == NeedStage::Critical);
    CHECK(GetNeedStage(0.0f, s) == NeedStage::Critical);
    // しきい値は、設定で変えられる。
    s.needSevereThreshold = 60.0f;
    CHECK(GetNeedStage(55.0f, s) == NeedStage::Severe);

    // 切迫度: 充足=0。生活の危機的=1。余暇だけが危機的でも、重要度(0.5 / 1.0)の分、低い。
    ResidentSimSettings d;
    ResidentAgent a = MakeAgent(d);
    CHECK(Near(ComputeNeedPressure(a, d), 0.0f));
    a.lifeNeed = 10.0f;
    CHECK(Near(ComputeNeedPressure(a, d), 1.0f));
    a.lifeNeed = 100.0f; a.activityNeed = 10.0f;
    CHECK(Near(ComputeNeedPressure(a, d), 0.5f));
    a.lifeNeed = 70.0f; a.activityNeed = 100.0f;                    // 生活が不足
    CHECK(Near(ComputeNeedPressure(a, d), d.needPressureByStage[1]));
}

// ---- 外出の理由 ----
static void TestOutingReason()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);

    // 乱数は、いつもちょうど1回(状態・重みに依らない)。
    {
        SimRandom r(5), expected(5);
        OutingReason reason = OutingReason::Quick;
        OutingReasonTrace trace;
        CHECK(ChooseOutingReason(a, s, r, false, reason, &trace));
        const float roll = expected.Next01();
        CHECK(r.GetState() == expected.GetState() && Near(trace.roll, roll, 0.0f));
        // トレースの有無で、乱数の消費は変わらない。
        SimRandom r2(5);
        ChooseOutingReason(a, s, r2, false, reason);
        CHECK(r2.GetState() == r.GetState());
        // 重みが全て0でも、乱数は1回引く(選べず、falseを返す)。
        ResidentSimSettings zero = s;
        zero.reasonQuickWeight = 0.0f; zero.reasonSubstantialWeight = 0.0f; zero.reasonAreaWeight = 0.0f;
        SimRandom r3(5);
        OutingReason untouched = OutingReason::Substantial;
        CHECK(!ChooseOutingReason(a, zero, r3, false, untouched, &trace));
        CHECK(r3.GetState() == expected.GetState() && untouched == OutingReason::Substantial && !trace.valid);
    }

    // 重みの式: 手軽 = 基本。がっつり = 基本 × 体力の割合^指数 × (1 − 切迫度 × ペナルティ) × 自発性。エリア = 候補が無ければ0。
    {
        SimRandom r(1);
        OutingReason reason;
        OutingReasonTrace t;
        a.stamina = 100.0f; a.lifeNeed = 100.0f; a.activityNeed = 100.0f;
        ChooseOutingReason(a, s, r, false, reason, &t);
        CHECK(Near(t.weight[0], 1.0f) && Near(t.weight[1], 0.8f) && Near(t.weight[2], 0.0f));
        a.stamina = 50.0f;                                                   // 体力が半分 → がっつりが半分
        ChooseOutingReason(a, s, r, false, reason, &t);
        CHECK(Near(t.weight[1], 0.4f) && Near(t.staminaRatio, 0.5f));
        a.stamina = 100.0f; a.lifeNeed = 10.0f;                              // 生活が危機的(切迫度1) → がっつりは 1 − 0.85
        ChooseOutingReason(a, s, r, false, reason, &t);
        CHECK(Near(t.weight[1], 0.8f * 0.15f) && Near(t.weight[0], 1.0f));
        a.lifeNeed = 100.0f;
        a.traits.spontaneity = 1.5f;                                         // 自発的な住民ほど、がっつり
        ChooseOutingReason(a, s, r, false, reason, &t);
        CHECK(Near(t.weight[1], 0.8f * 1.5f));
        a.traits.spontaneity = 1.0f;
    }

    // 割合(状態の反映): 健康で体力が満タン → がっつりが多い。生活が危機的 → 手軽が多い。体力が少ない → 手軽が多い。
    auto substantialRate = [&](float life, float stamina)
    {
        ResidentAgent b = a; b.lifeNeed = life; b.stamina = stamina;
        int n = 0; SimRandom r(33);
        for (int i = 0; i < 8000; ++i)
        {
            OutingReason reason = OutingReason::Quick;
            if (ChooseOutingReason(b, s, r, false, reason) && reason == OutingReason::Substantial) { ++n; }
        }
        return n / 8000.0;
    };
    const double healthy = substantialRate(100.0f, 100.0f);
    const double critical = substantialRate(10.0f, 100.0f);
    const double tired = substantialRate(100.0f, 20.0f);
    std::printf("  substantial rate: healthy=%.3f dailyCritical=%.3f tired=%.3f\n", healthy, critical, tired);
    CHECK(healthy > 0.40 && healthy < 0.48);       // 0.8 / 1.8 = 0.444
    CHECK(critical < healthy * 0.3);
    CHECK(tired < healthy * 0.5);
    CHECK(critical > 0.0 && tired > 0.0);          // 例外は残る(0%にはならない)

    // エリア: 候補が無い間は、重みを設定しても、選ばれない(他の理由から、同じ乱数で選ぶ)。候補があれば、選ばれる。
    {
        ResidentSimSettings withArea = s;
        withArea.reasonAreaWeight = 3.0f;
        int area = 0, areaWhenAvailable = 0; SimRandom r(44), r2(44);
        for (int i = 0; i < 3000; ++i)
        {
            OutingReason reason = OutingReason::Quick;
            CHECK(ChooseOutingReason(a, withArea, r, false, reason));
            if (reason == OutingReason::Area) { ++area; }
            ChooseOutingReason(a, withArea, r2, true, reason);
            if (reason == OutingReason::Area) { ++areaWhenAvailable; }
        }
        CHECK(area == 0);
        CHECK(areaWhenAvailable > 1000);               // 3.0 / (1.0 + 0.8 + 3.0) ≒ 62%
        // エリアの重みを0にすると、候補があっても、選ばれない。既定の重み(P2-C1で 0.5)なら、候補がある時だけ、一定の割合で選ばれる。
        ResidentSimSettings noArea = s;
        noArea.reasonAreaWeight = 0.0f;
        int none = 0, byDefault = 0, byDefaultNoCandidate = 0; SimRandom r3(45), r4(46), r5(46);
        for (int i = 0; i < 2000; ++i)
        {
            OutingReason reason;
            ChooseOutingReason(a, noArea, r3, true, reason); if (reason == OutingReason::Area) { ++none; }
            ChooseOutingReason(a, s, r4, true, reason); if (reason == OutingReason::Area) { ++byDefault; }
            ChooseOutingReason(a, s, r5, false, reason); if (reason == OutingReason::Area) { ++byDefaultNoCandidate; }
        }
        CHECK(none == 0);
        CHECK(byDefault > 200 && byDefault < 1000);    // 0.5 / (1.0 + 0.8 + 0.5 × ...) の周辺。候補があれば、一定の割合で選ばれる
        CHECK(byDefaultNoCandidate == 0);
    }
}

// ---- 理由ごとの距離 ----
static void TestReasonDistance()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    const FacilityDefinition* cafe = FindFacilityDefinition(BuildingType::Cafe);

    const float w0Quick = ComputeFacilityWeight(a, *cafe, UsagePurpose::Light, 0.0f, s, OutingReason::Quick);
    const float w0Sub = ComputeFacilityWeight(a, *cafe, UsagePurpose::Light, 0.0f, s, OutingReason::Substantial);
    CHECK(Near(w0Quick, w0Sub, 1e-6f));                                    // 距離0では、同じ(理由で変わるのは、距離の効き方だけ)

    // 既定(手軽)は、従来と同じ: 距離の目安(14マス)で 1/e。
    CHECK(Near(ComputeFacilityWeight(a, *cafe, UsagePurpose::Light, 14.0f, s) / w0Quick, std::exp(-1.0f), 0.002f));
    CHECK(Near(ComputeFacilityWeight(a, *cafe, UsagePurpose::Light, 14.0f, s, OutingReason::Quick) / w0Quick, std::exp(-1.0f), 0.002f));

    // がっつり: 目安が 2.5 倍(35マス)。同じ距離でも、手軽より、強く下がらない。
    for (float d : { 5.0f, 14.0f, 30.0f, 60.0f })
    {
        const float q = ComputeFacilityWeight(a, *cafe, UsagePurpose::Light, d, s, OutingReason::Quick);
        const float sub = ComputeFacilityWeight(a, *cafe, UsagePurpose::Light, d, s, OutingReason::Substantial);
        CHECK(sub > q);
    }
    CHECK(Near(ComputeFacilityWeight(a, *cafe, UsagePurpose::Light, 35.0f, s, OutingReason::Substantial) / w0Sub, std::exp(-1.0f), 0.002f));
    // 連続的(しきい値が無い): 距離が増えるごとに、単調に下がる。
    float prev = 1e9f;
    for (float d = 0.0f; d <= 80.0f; d += 0.5f)
    {
        const float w = ComputeFacilityWeight(a, *cafe, UsagePurpose::Light, d, s, OutingReason::Quick);
        CHECK(w <= prev + 1e-9f);
        prev = w;
    }
    // 形 k: k=2 なら、近場は下がりにくく、目安を超えると、急に下がる。
    ResidentSimSettings shaped = s;
    shaped.reasonDistanceShape[0] = 2.0f;
    FacilityWeightBreakdown b1, b2;
    ComputeFacilityWeightBreakdown(a, *cafe, UsagePurpose::Light, 7.0f, s, b1, OutingReason::Quick);
    ComputeFacilityWeightBreakdown(a, *cafe, UsagePurpose::Light, 7.0f, shaped, b2, OutingReason::Quick);
    CHECK(b2.distanceFactor > b1.distanceFactor);                          // 目安の半分では、k=2 の方が高い
    CHECK(Near(b2.distanceFactor, std::exp(-0.25f), 1e-3f) && Near(b2.distanceShape, 2.0f));
    ComputeFacilityWeightBreakdown(a, *cafe, UsagePurpose::Light, 28.0f, s, b1, OutingReason::Quick);
    ComputeFacilityWeightBreakdown(a, *cafe, UsagePurpose::Light, 28.0f, shaped, b2, OutingReason::Quick);
    CHECK(b2.distanceFactor < b1.distanceFactor);                          // 目安の2倍では、k=2 の方が低い
    CHECK(b1.reason == OutingReason::Quick);

    // 立ち寄り先は、距離の目安にさらに倍率が掛かる(今いる施設から、近い所に寄りやすい)。
    const float near1 = ComputeFacilityWeight(a, *cafe, UsagePurpose::Light, 10.0f, s, OutingReason::Quick, 1.0f);
    const float near06 = ComputeFacilityWeight(a, *cafe, UsagePurpose::Light, 10.0f, s, OutingReason::Quick, 0.6f);
    CHECK(near06 < near1);
}

// ---- 乱数の消費: 外出の1回で、状態に依らず固定(理由1 + カテゴリ・目的2 + 施設1) ----
static void TestFixedRandomDraws()
{
    ResidentSimSettings s;
    SimRandom gen(77);
    for (int n = 0; n < 500; ++n)
    {
        ResidentAgent a = MakeAgent(s);
        a.lifeNeed = gen.Range(0.0f, 100.0f);
        a.activityNeed = gen.Range(0.0f, 100.0f);
        a.stamina = gen.Range(0.0f, 100.0f);
        a.traits.spontaneity = gen.Range(0.5f, 1.6f);

        SimRandom r(1000 + n), expected(1000 + n);
        OutingReason reason = OutingReason::Quick;
        ChooseOutingReason(a, s, r, false, reason);
        OutingPurposeChoice purpose = ChoosePurpose(a, s, r);
        purpose.reason = reason;
        std::vector<FacilityCandidate> candidates;
        const int count = static_cast<int>(gen.Range(0.0f, 4.0f));        // 0〜3軒(0軒でも、乱数は引く)
        for (int i = 0; i < count; ++i)
        {
            FacilityCandidate c; c.objectId = 1 + i; c.type = BuildingType::Shop; c.reachable = true; c.pathLength = gen.Range(0.0f, 30.0f);
            candidates.push_back(c);
        }
        std::vector<CandidateEvaluation> ev;
        ChooseFacility(a, candidates, purpose, s, r, ev);
        for (int i = 0; i < 4; ++i) { expected.Next01(); }
        CHECK(r.GetState() == expected.GetState());
    }
}

// ---- 体力: 移動では消費しない / 施設の利用で消費 / 2軒目以降は少し高い / 体力が足りない施設は、候補から外れる ----
static void TestStaminaRules()
{
    ResidentSimSettings s;
    const FacilityDefinition* cinema = FindFacilityDefinition(BuildingType::Cinema);   // 利用の消費 14
    CHECK(Near(RequiredStamina(*cinema, 0, s), 14.0f));
    CHECK(Near(RequiredStamina(*cinema, 1, s), 14.0f * 1.15f));
    CHECK(Near(RequiredStamina(*cinema, 2, s), 14.0f * 1.30f));

    // 体力が足りない施設は、どんなに近くても、除外される。足りる施設は、どんなに遠くても、除外されない。
    ResidentAgent a = MakeAgent(s);
    a.stamina = 10.0f;
    OutingPurposeChoice purpose; purpose.category = FacilityCategory::Leisure; purpose.purpose = UsagePurpose::Light;
    std::vector<FacilityCandidate> c(2);
    c[0].objectId = 1; c[0].type = BuildingType::Cinema; c[0].reachable = true; c[0].pathLength = 0.5f;     // 近いが、14 > 10
    c[1].objectId = 2; c[1].type = BuildingType::Cafe;   c[1].reachable = true; c[1].pathLength = 800.0f;   // 遠いが、5 ≦ 10
    std::vector<CandidateEvaluation> ev;
    SimRandom r(3);
    CHECK(ChooseFacility(a, c, purpose, s, r, ev) == 1);
    CHECK(ev[0].reject == CandidateReject::NotEnoughStamina && ev[1].reject == CandidateReject::None);

    // 利用で体力が減るのは、利用の分だけ(帰りは、体力0でもできる: 帰宅の処理は、体力を見ない)。
    ResidentAgent b = MakeAgent(s);
    b.stamina = 20.0f;
    ApplyFacilityUse(b, *cinema, 5, 1, 12.0f, s, 0);
    CHECK(Near(b.stamina, 6.0f));
    ApplyFacilityUse(b, *cinema, 6, 1, 13.0f, s, 1);
    CHECK(b.stamina == 0.0f);                                              // 足りない分は、0で止まる(負にならない)
}

// ---- 立ち寄り: 3回以上できる作り / 更新された需要でカテゴリを選び直す ----
static void TestStopoverContinuous()
{
    ResidentSimSettings s;
    s.maxStopovers = 5;                                // 上限は、設定。3回以上も、続けられる
    ResidentAgent a = MakeAgent(s);
    for (int count = 0; count < 5; ++count)
    {
        a.outingStopCount = count;
        CHECK(CanConsiderStopover(a, 12.0f, s));
        CHECK(ComputeStopoverChance(a, s) > 0.0f);
    }
    a.outingStopCount = 5;
    CHECK(!CanConsiderStopover(a, 12.0f, s) && ExplainStopoverBlock(a, 12.0f, s) == StopoverBlock::MaxStops);

    // 利用の後に、需要が更新される → 次のカテゴリは、更新された需要で選び直される。
    ResidentSimSettings d;
    ResidentAgent b = MakeAgent(d);
    b.lifeNeed = 20.0f; b.activityNeed = 100.0f;       // 生活が危機的
    auto dailyRate = [&](const ResidentAgent& agent)
    {
        int n = 0; SimRandom r(66);
        for (int i = 0; i < 4000; ++i) { n += (ChoosePurpose(agent, d, r).category == FacilityCategory::Daily) ? 1 : 0; }
        return n / 4000.0;
    };
    const double before = dailyRate(b);
    ApplyFacilityUse(b, *FindFacilityDefinition(BuildingType::Supermarket), 9, 1, 12.0f, d);   // 生活が充足するまで回復する
    const double after = dailyRate(b);
    std::printf("  daily rate before/after the use: %.3f -> %.3f\n", before, after);
    CHECK(before > 0.9 && after < 0.35 && after < before * 0.5);   // 充足した後は、余暇が多め(約7割)

    // 切迫度が下がると、立ち寄りの確率も下がる(連続的)。
    ResidentAgent c = MakeAgent(d); c.lifeNeed = 20.0f;
    const float urgent = ComputeStopoverChance(c, d);
    c.lifeNeed = 100.0f;
    CHECK(ComputeStopoverChance(c, d) < urgent);
}

int main()
{
    TestNeedStages();
    TestOutingReason();
    TestReasonDistance();
    TestFixedRandomDraws();
    TestStaminaRules();
    TestStopoverContinuous();
    std::printf(g_fail == 0 ? "ALL PASSED\n" : "%d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
