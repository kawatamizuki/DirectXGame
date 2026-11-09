// P1(利用履歴・立ち寄り)のルールの単体テスト。
#include <cmath>
#include <cstdio>
#include <map>
#include "ResidentLogic.h"
#include "FacilityData.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

static ResidentAgent MakeAgent(const ResidentSimSettings& s)
{
    ResidentAgent a;
    InitializeResident(a, 7, s);
    a.homeObjectId = 1;
    a.stamina = 100.0f;
    return a;
}

static void PushVisit(ResidentAgent& a, uint32_t facilityId)
{
    VisitRecord r; r.facilityObjectId = facilityId; r.weekIndex = 1; r.hour = 10.0f;
    a.history.insert(a.history.begin(), r);
}

// ---- 利用履歴: 連続利用の重みの倍率 ----
static void TestRepeatFactor()
{
    ResidentSimSettings s;   // window=3, factor=0.5, needDrivenDailyStrength=0.25
    ResidentAgent a = MakeAgent(s);

    CHECK(Near(ComputeRepeatVisitFactor(a, 10, false, s), 1.0f));          // 履歴なし: 下がらない
    PushVisit(a, 10);
    CHECK(Near(ComputeRepeatVisitFactor(a, 10, false, s), 0.5f));          // 直前と同じ: 下がる
    CHECK(Near(ComputeRepeatVisitFactor(a, 11, false, s), 1.0f));          // 別の施設: 下がらない
    PushVisit(a, 11);                                                      // [11, 10]
    const float second = ComputeRepeatVisitFactor(a, 10, false, s);
    CHECK(second > 0.5f && second < 1.0f);                                 // 1つ前の利用: 少し弱い
    PushVisit(a, 12); PushVisit(a, 13);                                    // [13, 12, 11, 10]: 10は窓(3)の外
    CHECK(Near(ComputeRepeatVisitFactor(a, 10, false, s), 1.0f));
    // 窓の中に複数あれば、一番下がる物
    ResidentAgent b = MakeAgent(s);
    PushVisit(b, 5); PushVisit(b, 6); PushVisit(b, 5);                     // [5, 6, 5]
    CHECK(Near(ComputeRepeatVisitFactor(b, 5, false, s), 0.5f));

    // 不足による日用品の外出: 下げ幅が弱い(0.5 → 1-(1-0.5)*0.25 = 0.875)
    ResidentAgent c = MakeAgent(s);
    PushVisit(c, 10);
    CHECK(Near(ComputeRepeatVisitFactor(c, 10, true, s), 0.875f));
    s.needDrivenDailyRepeatStrength = 0.0f;
    CHECK(Near(ComputeRepeatVisitFactor(c, 10, true, s), 1.0f));
    s.needDrivenDailyRepeatStrength = 1.0f;
    CHECK(Near(ComputeRepeatVisitFactor(c, 10, true, s), 0.5f));
    // 係数1なら、下がらない
    s.repeatVisitWeightFactor = 1.0f;
    CHECK(Near(ComputeRepeatVisitFactor(c, 10, false, s), 1.0f));
}

// ---- 利用履歴: 抽選の割合 ----
static std::vector<FacilityCandidate> TwoCinemas()
{
    std::vector<FacilityCandidate> c(2);
    c[0].objectId = 10; c[0].type = BuildingType::Cinema; c[0].reachable = true; c[0].pathLength = 4.0f;
    c[1].objectId = 11; c[1].type = BuildingType::Cinema; c[1].reachable = true; c[1].pathLength = 4.0f;
    return c;
}

static void TestRepeatInChoose()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    OutingPurposeChoice purpose; purpose.needDriven = false; purpose.category = FacilityCategory::Leisure; purpose.purpose = UsagePurpose::Movie;
    std::vector<FacilityCandidate> candidates = TwoCinemas();

    // 履歴なし: 同条件なら 50:50。
    std::vector<CandidateEvaluation> ev;
    ChooseFacility(a, candidates, purpose, s, *new SimRandom(1), ev);
    CHECK(Near(ev[0].chance, 0.5f) && Near(ev[1].chance, 0.5f));

    // 直前に#10を使った: #10の確率は下がる(1:2 → 約33%)が、0にはならない。
    PushVisit(a, 10);
    ChooseFacility(a, candidates, purpose, s, *new SimRandom(1), ev);
    CHECK(Near(ev[0].repeatFactor, 0.5f) && Near(ev[1].repeatFactor, 1.0f));
    CHECK(Near(ev[0].chance, 1.0f / 3.0f, 0.01f));
    CHECK(ev[0].chance > 0.0f);

    // 回数で確かめる: #10に行く割合が、33%付近。毎回は繰り返さない。
    SimRandom random(99);
    int toRepeat = 0, total = 4000;
    for (int i = 0; i < total; ++i)
    {
        const int picked = ChooseFacility(a, candidates, purpose, s, random, ev);
        if (picked == 0) { ++toRepeat; }
    }
    const float rate = static_cast<float>(toRepeat) / static_cast<float>(total);
    std::printf("  repeat cinema rate = %.3f (expected ~0.333)\n", rate);
    CHECK(rate > 0.28f && rate < 0.38f);

    // 選べる施設が1つしか無い時は、履歴があっても、その施設を選ぶ(完全禁止しない)。
    std::vector<FacilityCandidate> only(candidates.begin(), candidates.begin() + 1);
    int onlyPicked = 0;
    for (int i = 0; i < 200; ++i) { if (ChooseFacility(a, only, purpose, s, random, ev) == 0) { ++onlyPicked; } }
    CHECK(onlyPicked == 200);

    // 不足による日用品の外出では、下げ幅が弱い。
    ResidentAgent d = MakeAgent(s);
    PushVisit(d, 20);
    std::vector<FacilityCandidate> shops(2);
    shops[0].objectId = 20; shops[0].type = BuildingType::Shop; shops[0].reachable = true; shops[0].pathLength = 3.0f;
    shops[1].objectId = 21; shops[1].type = BuildingType::Shop; shops[1].reachable = true; shops[1].pathLength = 3.0f;
    OutingPurposeChoice need; need.needDriven = true; need.category = FacilityCategory::Daily; need.purpose = UsagePurpose::TopUp;
    ChooseFacility(d, shops, need, s, random, ev);
    CHECK(Near(ev[0].repeatFactor, 0.875f));
    OutingPurposeChoice spont = need; spont.needDriven = false;
    ChooseFacility(d, shops, spont, s, random, ev);
    CHECK(Near(ev[0].repeatFactor, 0.5f));
}

// ---- 立ち寄りの候補の除外 ----
static void TestStopCandidates()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    OutingPurposeChoice purpose; purpose.needDriven = false; purpose.category = FacilityCategory::Leisure; purpose.purpose = UsagePurpose::Light;

    std::vector<FacilityCandidate> c(4);
    c[0].objectId = 10; c[0].type = BuildingType::Cinema; c[0].reachable = true; c[0].pathLength = 3.0f;    // 今回使用済み
    c[1].objectId = 11; c[1].type = BuildingType::Cafe;   c[1].reachable = true; c[1].pathLength = 3.0f;    // 候補
    c[2].objectId = 12; c[2].type = BuildingType::Cafe;   c[2].reachable = true; c[2].pathLength = 20.0f; c[2].outOfRange = true; // 遠い
    c[3].objectId = 13; c[3].type = BuildingType::Cafe;   c[3].reachable = false; // 到達不可
    PushVisit(a, 10);   // 主目的の映画館
    std::vector<CandidateEvaluation> ev;
    SimRandom random(5);

    // P2-0: 今いる施設からの距離の上限は、既定では無い(距離は、確率の減衰で扱う)。遠い候補も、抽選の対象で、重みが小さいだけ。
    ChooseFacility(a, c, purpose, s, random, ev, 1);
    CHECK(ev[0].reject == CandidateReject::VisitedThisOuting);
    CHECK(ev[2].reject == CandidateReject::None && ev[2].weight < ev[1].weight);
    CHECK(ev[3].reject == CandidateReject::Unreachable);

    // 設定で上限(マス)を決めると、範囲外は、除外される(従来の動き。0=上限なし)。
    s.stopoverMaxDistanceLegacyCells = 8.0f;
    const int picked = ChooseFacility(a, c, purpose, s, random, ev, 1);
    CHECK(picked == 1);
    CHECK(ev[0].reject == CandidateReject::VisitedThisOuting);
    CHECK(ev[1].reject == CandidateReject::None && ev[1].chosen);
    CHECK(ev[2].reject == CandidateReject::OutOfRange);
    CHECK(ev[3].reject == CandidateReject::Unreachable);
    s.stopoverMaxDistanceLegacyCells = 0.0f;

    // visitsThisOuting=0 なら(通常の施設選び)、履歴にあっても除外しない(重みが下がるだけ)。
    ChooseFacility(a, c, purpose, s, random, ev, 0);
    CHECK(ev[0].reject == CandidateReject::None && ev[0].repeatFactor < 1.0f);

    // 体力(P2-0): 移動では使わない。施設を使う分だけ。2軒目以降は、少し高い(カフェ 5 × (1 + 0.15 × 何軒目か))。
    ResidentAgent tired = MakeAgent(s);
    std::vector<FacilityCandidate> near(1);
    near[0].objectId = 11; near[0].type = BuildingType::Cafe; near[0].reachable = true; near[0].pathLength = 3.0f;
    tired.stamina = 5.8f;       // 立ち寄り1軒目(visits=1)の必要は 5.75
    CHECK(ChooseFacility(tired, near, purpose, s, random, ev, 1) == 0);
    tired.stamina = 5.7f;
    CHECK(ChooseFacility(tired, near, purpose, s, random, ev, 1) == -1);
    CHECK(ev[0].reject == CandidateReject::NotEnoughStamina && Near(ev[0].requiredStamina, 5.75f));
    // 道のりが長くても、必要な体力は、変わらない(移動では、体力を使わない)。
    near[0].pathLength = 500.0f;
    tired.stamina = 5.8f;
    CHECK(ChooseFacility(tired, near, purpose, s, random, ev, 1) == 0);
    CHECK(Near(ev[0].requiredStamina, 5.75f));
    // 主目的(visits=0)は、5。
    tired.stamina = 5.0f;
    CHECK(ChooseFacility(tired, near, purpose, s, random, ev, 0) == 0);
    CHECK(Near(ev[0].requiredStamina, 5.0f));
}

// ---- 立ち寄りの確率・条件 ----
static void TestStopoverChance()
{
    // P2-0: 確率 = 基本 0.4 × 回数の減衰 0.4^済み × 体力の割合 × 需要の補正(足りている=0.7) × 理由の倍率(手軽 0.5)。
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    a.lifeNeed = 100.0f; a.activityNeed = 100.0f;          // 需要は、足りている
    a.stamina = EffectiveStaminaCap(a, s);                  // 体力は、満タン
    a.outingReason = OutingReason::Quick;
    a.outingStopCount = 0;
    CHECK(Near(ComputeStopoverChance(a, s), 0.4f * 0.7f * 0.5f));
    a.outingStopCount = 1;
    CHECK(Near(ComputeStopoverChance(a, s), 0.4f * 0.4f * 0.7f * 0.5f));
    a.outingStopCount = 2;
    CHECK(Near(ComputeStopoverChance(a, s), 0.4f * 0.4f * 0.4f * 0.7f * 0.5f));
    // 3回目以降も、続けられる(最大回数は、安全弁。確率は、回数とともに下がり続ける)。
    const float chance2 = ComputeStopoverChance(a, s);
    a.outingStopCount = 3;
    CHECK(ComputeStopoverChance(a, s) < chance2 && ComputeStopoverChance(a, s) > 0.0f);

    // 理由: がっつりは、手軽の 1.2 / 0.5 倍。
    a.outingStopCount = 0;
    a.outingReason = OutingReason::Substantial;
    CHECK(Near(ComputeStopoverChance(a, s), 0.4f * 0.7f * 1.2f));
    a.outingReason = OutingReason::Quick;

    // 体力: 連続(割合に比例)。しきい値は無い。
    const float full = ComputeStopoverChance(a, s);
    a.stamina = EffectiveStaminaCap(a, s) * 0.5f;
    CHECK(Near(ComputeStopoverChance(a, s), full * 0.5f, 1e-4f));
    a.stamina = 5.0f;
    CHECK(ComputeStopoverChance(a, s) > 0.0f && ComputeStopoverChance(a, s) < full * 0.1f);
    CHECK(CanConsiderStopover(a, 12.0f, s));                // 体力が少なくても、確率が下がるだけ(考えてはよい)
    a.stamina = 0.0f;
    CHECK(ComputeStopoverChance(a, s) == 0.0f);
    CHECK(!CanConsiderStopover(a, 12.0f, s));               // 体力が0なら、考えない
    CHECK(ExplainStopoverBlock(a, 12.0f, s) == StopoverBlock::LowStamina);
    a.stamina = EffectiveStaminaCap(a, s);

    // 需要: 切迫しているほど、立ち寄りやすい(足りている=0.7 → 危機的=1.0)。
    a.lifeNeed = 10.0f;
    CHECK(Near(ComputeStopoverChance(a, s), 0.4f * 1.0f * 0.5f));
    a.lifeNeed = 100.0f;

    CHECK(CanConsiderStopover(a, 12.0f, s));
    CHECK(!CanConsiderStopover(a, 19.5f, s));            // 遅い時刻
    a.outingStopCount = 2;
    CHECK(!CanConsiderStopover(a, 12.0f, s));            // 上限
    a.outingStopCount = 0;
    a.homeObjectId = 0;
    CHECK(!CanConsiderStopover(a, 12.0f, s));            // 帰る家が無い
    a.homeObjectId = 1;
    s.maxStopovers = 0;
    CHECK(!CanConsiderStopover(a, 12.0f, s));            // 設定で無効

    // 立ち寄りが起きる回数を数える: 1回目が約14%(需要が足りて・体力満タン・手軽)、2回目はさらに低い。
    ResidentSimSettings t;
    SimRandom random(2024);
    int first = 0, second = 0, trials = 20000;
    for (int i = 0; i < trials; ++i)
    {
        ResidentAgent b = MakeAgent(t);
        b.lifeNeed = 100.0f; b.activityNeed = 100.0f; b.stamina = EffectiveStaminaCap(b, t);
        if (random.Next01() < ComputeStopoverChance(b, t))
        {
            ++first;
            b.outingStopCount = 1;
            if (random.Next01() < ComputeStopoverChance(b, t)) { ++second; }
        }
    }
    const float r1 = static_cast<float>(first) / trials;
    const float r2 = static_cast<float>(second) / trials;
    std::printf("  stopover rate: first=%.3f (exp 0.14)  second(any)=%.4f (exp 0.0056)\n", r1, r2);
    CHECK(r1 > 0.12f && r1 < 0.16f);
    CHECK(r2 < r1 * 0.1f);
}

// ---- 食事系の連続 ----
static void TestFoodChain()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    const FacilityDefinition* cafe = FindFacilityDefinition(BuildingType::Cafe);
    const FacilityDefinition* cinema = FindFacilityDefinition(BuildingType::Cinema);
    CHECK(cafe && cinema && cafe->foodLike && !cinema->foodLike);

    a.usedFoodLikeFacilityThisOuting = false;
    CHECK(Near(ComputeFoodChainAcceptChance(a, *cafe, s), 1.0f));          // 食事系をまだ使っていない: 通常
    a.usedFoodLikeFacilityThisOuting = true;
    CHECK(Near(ComputeFoodChainAcceptChance(a, *cafe, s), 0.2f));          // 食事系の後の食事系: 下がる
    CHECK(Near(ComputeFoodChainAcceptChance(a, *cinema, s), 1.0f));        // 食事系の後でも、映画館は通常
    s.foodChainAcceptChance = 0.0f;
    CHECK(Near(ComputeFoodChainAcceptChance(a, *cafe, s), 0.0f));
    s.foodChainAcceptChance = 1.0f;
    CHECK(Near(ComputeFoodChainAcceptChance(a, *cafe, s), 1.0f));
}

// ---- 既存の動作が変わっていない: 従来の呼び出しは、履歴が無ければ、同じ結果 ----
static void TestBackwardCompatible()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    OutingPurposeChoice purpose; purpose.needDriven = true; purpose.category = FacilityCategory::Daily; purpose.purpose = UsagePurpose::TopUp;
    std::vector<FacilityCandidate> c(2);
    c[0].objectId = 1; c[0].type = BuildingType::Shop;        c[0].reachable = true; c[0].pathLength = 2.0f;
    c[1].objectId = 2; c[1].type = BuildingType::Supermarket; c[1].reachable = true; c[1].pathLength = 6.0f;
    std::vector<CandidateEvaluation> ev;
    SimRandom r1(3), r2(3);
    const int p1 = ChooseFacility(a, c, purpose, s, r1, ev);
    const float w0 = ev[0].weight, w1 = ev[1].weight;
    const float expected0 = ComputeFacilityWeight(a, *FindFacilityDefinition(BuildingType::Shop), purpose.purpose, 2.0f, s);
    const float expected1 = ComputeFacilityWeight(a, *FindFacilityDefinition(BuildingType::Supermarket), purpose.purpose, 6.0f, s);
    CHECK(Near(w0, expected0) && Near(w1, expected1));
    const int p2 = ChooseFacility(a, c, purpose, s, r2, ev, 0);
    CHECK(p1 == p2);
}

int main()
{
    TestRepeatFactor();
    TestRepeatInChoose();
    TestStopCandidates();
    TestStopoverChance();
    TestFoodChain();
    TestBackwardCompatible();
    if (g_fail == 0) { std::printf("ALL PASSED\n"); } else { std::printf("%d FAILED\n", g_fail); }
    return g_fail == 0 ? 0 : 1;
}
