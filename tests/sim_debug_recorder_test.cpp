// 段階1(住民デバッグの基本)の単体テスト: ルールの関数に足した「判断の内訳」が、以前の計算と完全に一致すること、
// 乱数の消費がトレースの有無で変わらないこと、記録(SimDebugRecorder)の上限・外出のまとまり・異常の検出。
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <limits>
#include "ResidentLogic.h"
#include "SimDebugRecorder.h"
#include "FacilityData.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)
static bool BitEq(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

// ============ 以前の実装(比較用の写し。段階1より前の ResidentLogic.cpp のまま) ============
namespace old
{
    float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    float ComputeFacilityWeight(const ResidentAgent& agent, const FacilityDefinition& facility, UsagePurpose purpose,
                                float pathLength, const ResidentSimSettings& settings, float distanceScaleFactor = 1.0f)
    {
        const int purposeIndex = static_cast<int>(purpose);
        const float suitability = (purposeIndex >= 0 && purposeIndex < kUsagePurposeCount) ? facility.purposeSuitability[purposeIndex] : 1.0f;
        const float quality = (std::max)(0.01f, facility.quality);
        const float qualityFactor = std::pow(quality, settings.qualityExponent * agent.traits.qualityWeight);
        const float priceRatio = static_cast<float>(facility.pricePerVisit) / (std::max)(1.0f, settings.priceReferenceYen);
        const float priceFactor = 1.0f / (1.0f + settings.priceStrength * agent.traits.priceSensitivity * priceRatio);
        const float legacyDistance = pathLength / WorldScale::FromLegacy(1.0f);
        const float scale = (std::max)(0.1f, settings.distanceScaleLegacyCells * agent.traits.distanceTolerance * distanceScaleFactor);   // P2-0: 立ち寄りは、距離の目安の倍率が掛かる
        const float distanceFactor = std::exp(-(std::max)(0.0f, legacyDistance) / scale);
        const float weight = (std::max)(0.0f, suitability) * qualityFactor * priceFactor * distanceFactor;
        return (std::max)(weight, settings.minimumWeight);
    }

    int ChooseFacility(const ResidentAgent& agent, const std::vector<FacilityCandidate>& candidates, const OutingPurposeChoice& purpose,
                       const ResidentSimSettings& settings, SimRandom& random, std::vector<CandidateEvaluation>& evaluations, int visitsThisOuting)
    {
        evaluations.clear();
        std::vector<float> weights;
        const bool needDrivenDaily = purpose.needDriven && purpose.category == FacilityCategory::Daily;
        for (const FacilityCandidate& candidate : candidates)
        {
            CandidateEvaluation evaluation;
            evaluation.candidate = candidate;
            const FacilityDefinition* facility = FindFacilityDefinition(candidate.type);
            bool visitedThisOuting = false;
            for (size_t i = 0; i < agent.history.size() && static_cast<int>(i) < visitsThisOuting; ++i)
            {
                if (agent.history[i].facilityObjectId == candidate.objectId) { visitedThisOuting = true; break; }
            }
            if (facility == nullptr || facility->category != purpose.category) { evaluation.reject = CandidateReject::WrongCategory; }
            else if (visitedThisOuting) { evaluation.reject = CandidateReject::VisitedThisOuting; }
            else if (candidate.outOfRange && settings.stopoverMaxDistanceLegacyCells > 0.0f) { evaluation.reject = CandidateReject::OutOfRange; }
            else if (!candidate.reachable) { evaluation.reject = CandidateReject::Unreachable; }
            else if (agent.stamina < facility->staminaCost * (1.0f + settings.additionalUseStaminaCostStep * static_cast<float>(visitsThisOuting)))   // P2-0: 移動の分は無い。何軒目かで、少し高い
            { evaluation.reject = CandidateReject::NotEnoughStamina; }
            else
            {
                evaluation.repeatFactor = ComputeRepeatVisitFactor(agent, candidate.objectId, needDrivenDaily, settings);
                const float baseWeight = old::ComputeFacilityWeight(agent, *facility, purpose.purpose, candidate.pathLength, settings, visitsThisOuting > 0 ? settings.stopoverDistanceScaleFactor : 1.0f);
                evaluation.weight = (std::max)(baseWeight * evaluation.repeatFactor, settings.minimumWeight);
            }
            weights.push_back(evaluation.weight);
            evaluations.push_back(evaluation);
        }
        float total = 0.0f;
        for (float w : weights) { total += w; }
        if (total > 0.0f) { for (CandidateEvaluation& e : evaluations) { e.chance = e.weight / total; } }
        const int picked = PickWeightedIndex(weights, random.Next01());
        if (picked >= 0) { evaluations[static_cast<size_t>(picked)].chosen = true; }
        return picked;
    }

    bool CanConsiderStopover(const ResidentAgent& agent, float hour, const ResidentSimSettings& settings)
    {
        return agent.homeObjectId != 0 && agent.outingStopCount < settings.maxStopovers && hour < settings.stopoverLatestHour && agent.stamina > 0.0f; // P2-0: 体力が少ない時は、確率が下がる(0の時だけ除外)
    }

    float OutingChance(const ResidentAgent& agent, const ResidentSimSettings& settings)
    {
        const bool unmet = IsLifeNeedUnmet(agent, settings) || IsActivityNeedUnmet(agent, settings);
        float chance = 0.0f;
        if (unmet) { chance = settings.needDrivenChancePerTick; }
        else if (agent.stamina >= settings.spontaneousMinStamina) { chance = settings.spontaneousChancePerTick * agent.traits.spontaneity; }
        return chance;
    }
}

// ============ 乱数・住民の生成 ============
static SimRandom g_gen(2024);
static float R(float lo, float hi) { return g_gen.Range(lo, hi); }

static ResidentAgent MakeAgent(uint32_t id)
{
    ResidentAgent a;
    a.objectId = id;
    a.homeObjectId = 100 + id;
    a.traits.staminaCapBase = R(80.0f, 120.0f);
    a.traits.needDecayScale = R(0.8f, 1.25f);
    a.traits.qualityWeight = R(0.6f, 1.5f);
    a.traits.priceSensitivity = R(0.5f, 1.6f);
    a.traits.distanceTolerance = R(0.7f, 1.5f);
    a.traits.spontaneity = R(0.5f, 1.6f);
    a.stamina = R(0.0f, a.traits.staminaCapBase);
    a.lifeNeed = R(0.0f, 100.0f);
    a.activityNeed = R(0.0f, 100.0f);
    a.penaltyLevel = static_cast<int>(R(0.0f, 4.0f));
    if (g_gen.Next01() < 0.1f) { a.activityNeed = a.lifeNeed; }              // 同じだけ不足(乱数で決める分岐)
    const int hist = static_cast<int>(R(0.0f, 5.0f));
    for (int i = 0; i < hist; ++i)
    {
        VisitRecord v; v.facilityObjectId = static_cast<uint32_t>(1 + static_cast<int>(R(0.0f, 6.0f))); v.weekIndex = 1; v.hour = 10.0f;
        a.history.push_back(v);
    }
    return a;
}

static ResidentSimSettings MakeSettings()
{
    ResidentSimSettings s;
    s.qualityExponent = R(0.0f, 3.0f);
    s.priceReferenceYen = R(50.0f, 800.0f);
    s.priceStrength = R(0.0f, 3.0f);
    s.distanceScaleLegacyCells = R(1.0f, 40.0f);
    s.minimumWeight = R(0.0f, 0.05f);
    s.repeatVisitWindow = 1 + static_cast<int>(R(0.0f, 6.0f));
    s.repeatVisitWeightFactor = R(0.0f, 1.0f);
    s.needDrivenDailyRepeatStrength = R(0.0f, 1.0f);
    return s;
}

static const BuildingType kFacilityTypes[] = { BuildingType::Shop, BuildingType::Supermarket, BuildingType::Cinema, BuildingType::Cafe };

// ============ テスト ============
static void TestWeightBreakdownExact()
{
    int compared = 0;
    for (int n = 0; n < 4000; ++n)
    {
        const ResidentAgent agent = MakeAgent(1);
        const ResidentSimSettings s = MakeSettings();
        for (BuildingType type : kFacilityTypes)
        {
            const FacilityDefinition* f = FindFacilityDefinition(type);
            CHECK(f != nullptr);
            for (int p = 0; p < kUsagePurposeCount; ++p)
            {
                const float path = R(0.0f, 60.0f);
                const float oldW = old::ComputeFacilityWeight(agent, *f, static_cast<UsagePurpose>(p), path, s);
                const float newW = ComputeFacilityWeight(agent, *f, static_cast<UsagePurpose>(p), path, s);
                FacilityWeightBreakdown b;
                ComputeFacilityWeightBreakdown(agent, *f, static_cast<UsagePurpose>(p), path, s, b);
                CHECK(BitEq(oldW, newW));
                CHECK(BitEq(oldW, b.weight));
                // 内訳の積が、重みの元の式と一致する(画面に出す式が、実際の計算と同じ)。
                CHECK(BitEq(b.product, (std::max)(0.0f, b.suitability) * b.qualityFactor * b.priceFactor * b.distanceFactor));
                CHECK(BitEq(b.weight, (std::max)(b.product, b.floorWeight)));
                CHECK(BitEq(b.qualityExponent, s.qualityExponent * agent.traits.qualityWeight));
                CHECK(BitEq(b.priceStrength, s.priceStrength * agent.traits.priceSensitivity));
                ++compared;
            }
        }
    }
    std::printf("  weight breakdown: %d comparisons\n", compared);
}

static void TestChoosePurposeTrace()
{
    // P2-0: カテゴリは、いつも乱数で決める(需要の段階 × 重要度の重み)。乱数の消費は、いつも2回(カテゴリ・利用目的)。
    int daily = 0, leisure = 0;
    for (int n = 0; n < 6000; ++n)
    {
        const ResidentAgent agent = MakeAgent(2);
        const ResidentSimSettings s = MakeSettings();
        SimRandom b(1000 + n), c(1000 + n), replay(1000 + n);
        PurposeTrace trace;
        const OutingPurposeChoice withTrace = ChoosePurpose(agent, s, b, &trace);
        const OutingPurposeChoice noTrace = ChoosePurpose(agent, s, c);
        CHECK(noTrace.category == withTrace.category && noTrace.purpose == withTrace.purpose && noTrace.needDriven == withTrace.needDriven);
        CHECK(b.GetState() == c.GetState());                       // 乱数の消費は、トレースの有無で変わらない

        // 乱数は、いつもちょうど2回(カテゴリ・利用目的)。トレースの乱数は、引いた順と一致する。
        const float first = replay.Next01();
        const float second = replay.Next01();
        CHECK(replay.GetState() == b.GetState());
        CHECK(BitEq(first, trace.categoryRoll));
        CHECK(BitEq(second, trace.purposeRoll));
        CHECK(trace.categoryDrawn && !trace.tieBreakDrawn);

        // カテゴリの重み = 重要度 × 段階の補正。余暇の確率 = 余暇の重み ÷ 合計。
        CHECK(Near(trace.dailyWeight, s.dailyImportance * s.dailyStageMultiplier[static_cast<int>(trace.lifeStage)], 1e-5f));
        CHECK(Near(trace.leisureWeight, s.leisureImportance * s.leisureStageMultiplier[static_cast<int>(trace.activityStage)], 1e-5f));
        const float sum = trace.dailyWeight + trace.leisureWeight;
        CHECK(Near(trace.leisureChance, sum > 0.0f ? trace.leisureWeight / sum : 0.0f, 1e-5f));
        CHECK((withTrace.category == FacilityCategory::Leisure) == (trace.categoryRoll < trace.leisureChance));
        // 選んだカテゴリの需要が不足なら、不足による外出。
        CHECK(withTrace.needDriven == (withTrace.category == FacilityCategory::Daily ? trace.lifeUnmet : trace.activityUnmet));
        (withTrace.category == FacilityCategory::Daily ? daily : leisure)++;
    }
    CHECK(daily > 0 && leisure > 0);
    std::printf("  purpose: daily=%d leisure=%d\n", daily, leisure);
}

static void TestChooseFacilityEquivalence()
{
    int picked = 0, none = 0, withRepeat = 0, singles = 0;
    for (int n = 0; n < 6000; ++n)
    {
        const ResidentAgent agent = MakeAgent(3);
        const ResidentSimSettings s = MakeSettings();
        OutingPurposeChoice purpose;
        purpose.needDriven = g_gen.Next01() < 0.5f;
        purpose.category = (g_gen.Next01() < 0.5f) ? FacilityCategory::Daily : FacilityCategory::Leisure;
        purpose.purpose = purpose.category == FacilityCategory::Daily ? UsagePurpose::TopUp : UsagePurpose::Movie;

        std::vector<FacilityCandidate> candidates;
        const int count = 1 + static_cast<int>(R(0.0f, 7.0f));
        for (int i = 0; i < count; ++i)
        {
            FacilityCandidate c;
            c.objectId = static_cast<uint32_t>(1 + i);
            c.type = kFacilityTypes[static_cast<int>(R(0.0f, 4.0f)) % 4];
            c.reachable = g_gen.Next01() < 0.85f;
            c.pathLength = R(0.0f, 50.0f);
            c.outOfRange = g_gen.Next01() < 0.1f;
            candidates.push_back(c);
        }
        const int visits = static_cast<int>(R(0.0f, 4.0f));
        SimRandom a(777 + n), b(777 + n), clone(777 + n);
        std::vector<CandidateEvaluation> oldEv, newEv;
        const int oldPick = old::ChooseFacility(agent, candidates, purpose, s, a, oldEv, visits);
        FacilityChoiceTrace trace;
        const int newPick = ChooseFacility(agent, candidates, purpose, s, b, newEv, visits, &trace);
        CHECK(oldPick == newPick);
        CHECK(a.GetState() == b.GetState());
        CHECK(oldEv.size() == newEv.size());
        float positiveTotal = 0.0f;
        for (size_t i = 0; i < oldEv.size() && i < newEv.size(); ++i)
        {
            CHECK(oldEv[i].reject == newEv[i].reject);
            CHECK(BitEq(oldEv[i].repeatFactor, newEv[i].repeatFactor));
            CHECK(BitEq(oldEv[i].weight, newEv[i].weight));
            CHECK(BitEq(oldEv[i].chance, newEv[i].chance));
            CHECK(oldEv[i].chosen == newEv[i].chosen);
            CHECK(newEv[i].hasBreakdown == (newEv[i].reject == CandidateReject::None));
            if (newEv[i].hasBreakdown)
            {
                // 再訪補正後の重み・下限の適用が、表示用の途中値と一致する。
                CHECK(BitEq(newEv[i].weightAfterRepeat, newEv[i].breakdown.weight * newEv[i].repeatFactor));
                CHECK(BitEq(newEv[i].weight, (std::max)(newEv[i].weightAfterRepeat, s.minimumWeight)));
                ++withRepeat;
            }
            if (newEv[i].reject == CandidateReject::NotEnoughStamina) { CHECK(newEv[i].requiredStamina > agent.stamina); }
            if (newEv[i].reject == CandidateReject::None) { CHECK(newEv[i].requiredStamina <= agent.stamina); }
            // 累積の範囲は、隙間なくつながる。
            CHECK(BitEq(newEv[i].rangeBegin, positiveTotal));
            if (newEv[i].weight > 0.0f) { positiveTotal += newEv[i].weight; }
            CHECK(BitEq(newEv[i].rangeEnd, positiveTotal));
        }
        // 乱数: 抽選の乱数は、実際に引いた1つ。
        CHECK(BitEq(trace.randomUnit, clone.Next01()));
        CHECK(trace.pickedIndex == newPick);
        if (newPick >= 0)
        {
            const CandidateEvaluation& p = newEv[static_cast<size_t>(newPick)];
            // 乱数×合計が、選ばれた候補の範囲に入る(丸めの末尾だけ例外で、最後の正の重みの候補)。
            CHECK(trace.pickTarget >= p.rangeBegin - 1e-4f && trace.pickTarget <= p.rangeEnd + 1e-4f);
            ++picked;
            if (newEv.size() == 1) { ++singles; CHECK(Near(p.chance, 1.0f)); }
        }
        else { ++none; }
        float chanceSum = 0.0f;
        for (const CandidateEvaluation& e : newEv) { chanceSum += e.chance; }
        if (trace.totalWeight > 0.0f) { CHECK(Near(chanceSum, 1.0f, 1e-3f)); }
    }
    CHECK(picked > 0 && none > 0 && singles > 0);
    std::printf("  choose: picked=%d none=%d single=%d breakdowns=%d\n", picked, none, singles, withRepeat);
}

static void TestStopoverAndChance()
{
    int blocks[5] = {};
    for (int n = 0; n < 20000; ++n)
    {
        ResidentAgent agent = MakeAgent(4);
        agent.homeObjectId = (g_gen.Next01() < 0.2f) ? 0 : 5;
        agent.outingStopCount = static_cast<int>(R(0.0f, 4.0f));
        agent.stamina = (g_gen.Next01() < 0.1f) ? 0.0f : R(0.0f, 100.0f);
        ResidentSimSettings s = MakeSettings();
        s.maxStopovers = static_cast<int>(R(0.0f, 4.0f));
        s.stopoverLatestHour = R(8.0f, 22.0f);
        s.stopoverMaxDistanceLegacyCells = (g_gen.Next01() < 0.5f) ? 0.0f : R(1.0f, 30.0f);
        const float hour = R(6.0f, 22.0f);
        const bool oldOk = old::CanConsiderStopover(agent, hour, s);
        const StopoverBlock block = ExplainStopoverBlock(agent, hour, s);
        CHECK(oldOk == (block == StopoverBlock::None));
        CHECK(CanConsiderStopover(agent, hour, s) == oldOk);
        // 理由は、以前の && の並び(家・回数・時刻・体力)の、最初に外れたもの。
        if (agent.homeObjectId == 0) { CHECK(block == StopoverBlock::NoHome); }
        else if (!(agent.outingStopCount < s.maxStopovers)) { CHECK(block == StopoverBlock::MaxStops); }
        else if (!(hour < s.stopoverLatestHour)) { CHECK(block == StopoverBlock::TooLate); }
        else if (!(agent.stamina > 0.0f)) { CHECK(block == StopoverBlock::LowStamina); }
        ++blocks[static_cast<int>(block)];

        // 外出を考える確率。
        OutingChanceBranch branch;
        const float chance = ComputeOutingChance(agent, s, &branch);
        CHECK(BitEq(chance, old::OutingChance(agent, s)));
        if (branch == OutingChanceBranch::SpontaneousLowStamina) { CHECK(chance == 0.0f); }
    }
    for (int i = 0; i < 5; ++i) { CHECK(blocks[i] > 0); }
    std::printf("  stopover blocks: none=%d home=%d max=%d late=%d stamina=%d\n", blocks[0], blocks[1], blocks[2], blocks[3], blocks[4]);
}

// ============ 記録 ============
static SimEvent MakeEvent(uint32_t resident, SimEventKind kind, SimEventResult result = SimEventResult::Info, int stop = 0)
{
    SimEvent e;
    e.residentId = resident;
    e.kind = kind;
    e.result = result;
    e.stopIndex = stop;
    return e;
}

static void TestRecorderBasics()
{
    SimDebugRecorder r;
    // OFFの間は何も記録しない。
    CHECK(r.AddEvent(MakeEvent(1, SimEventKind::OutingRoll)) == 0);
    CHECK(r.AddDetail(SimDecisionDetail{}) == 0);
    r.NoteOutingRoll(1, OutingChanceBranch::Spontaneous, 0.1f, 0.5f, false, 1, 9.0f);
    CHECK(r.UpdateAnomalyState(1, 2) == 0);
    CHECK(r.Events().empty() && r.FindResident(1) == nullptr);

    r.SetEnabled(true);
    // 外出のまとまり: OutingRoll で始まり、ReturnHome で終わる。
    const uint64_t e1 = r.AddEvent(MakeEvent(1, SimEventKind::OutingRoll, SimEventResult::Success));
    const uint64_t e2 = r.AddEvent(MakeEvent(1, SimEventKind::FacilitySelect, SimEventResult::Success));
    const uint64_t e3 = r.AddEvent(MakeEvent(1, SimEventKind::ReturnHome, SimEventResult::Success));
    const uint64_t e4 = r.AddEvent(MakeEvent(1, SimEventKind::OutingRoll, SimEventResult::Success));
    CHECK(e1 == 1 && e2 == 2 && e3 == 3 && e4 == 4);  // 単調増加
    const auto& ev = r.Events();
    CHECK(ev[0].outingId != 0 && ev[0].outingId == ev[1].outingId && ev[1].outingId == ev[2].outingId);
    CHECK(ev[3].outingId != ev[0].outingId);          // 次の外出は、別の番号
    CHECK(r.CurrentOutingId(1) == ev[3].outingId);
    CHECK(ev[0].runId == r.GetRunId());

    // 別の住民の外出は、混ざらない。
    r.AddEvent(MakeEvent(2, SimEventKind::OutingRoll, SimEventResult::Success));
    CHECK(r.CurrentOutingId(2) != r.CurrentOutingId(1) && r.CurrentOutingId(2) != 0);
    // 外出できなかった場合は、そこで外出が終わる。主目的の施設選びの失敗でも終わる。立ち寄りの施設選びの失敗では終わらない。
    r.AddEvent(MakeEvent(2, SimEventKind::OutingBlocked, SimEventResult::NotMet));
    CHECK(r.CurrentOutingId(2) == 0);
    r.AddEvent(MakeEvent(3, SimEventKind::OutingRoll, SimEventResult::Success));
    r.AddEvent(MakeEvent(3, SimEventKind::FacilitySelect, SimEventResult::NotMet, 0));
    CHECK(r.CurrentOutingId(3) == 0);
    r.AddEvent(MakeEvent(4, SimEventKind::OutingRoll, SimEventResult::Success));
    r.AddEvent(MakeEvent(4, SimEventKind::FacilitySelect, SimEventResult::NotMet, 1));
    CHECK(r.CurrentOutingId(4) != 0);
    // 立ち往生は外出の終わり。復旧で、新しいまとまりが始まる。
    const uint32_t before = r.CurrentOutingId(4);
    r.AddEvent(MakeEvent(4, SimEventKind::BecameStranded, SimEventResult::Warning));
    CHECK(r.CurrentOutingId(4) == 0);
    r.AddEvent(MakeEvent(4, SimEventKind::StrandedRecovered));
    CHECK(r.CurrentOutingId(4) != 0 && r.CurrentOutingId(4) != before);

    // 外出判定の集計(不発もここに数える)。
    r.NoteOutingRoll(7, OutingChanceBranch::NeedDriven, 0.25f, 0.9f, false, 1, 8.0f);
    r.NoteOutingRoll(7, OutingChanceBranch::NeedDriven, 0.25f, 0.8f, false, 1, 8.1f);
    r.NoteOutingRoll(7, OutingChanceBranch::NeedDriven, 0.25f, 0.1f, true, 1, 8.2f);
    r.NoteOutingRoll(7, OutingChanceBranch::Spontaneous, 0.03f, 0.5f, false, 1, 8.3f);
    const SimResidentRecord* rec = r.FindResident(7);
    CHECK(rec && rec->roll.checks == 4 && rec->roll.hits == 1 && rec->roll.consecutiveMisses == 1);
    CHECK(rec && rec->roll.lastBranch == OutingChanceBranch::Spontaneous && !rec->roll.lastHit);

    // 住民ごとの直近のイベント(新しい順)。
    const auto recent = r.RecentEventsOf(1, 3);
    CHECK(recent.size() == 3 && recent[0]->eventId > recent[1]->eventId);
    CHECK(r.RecentEventsOf(999, 5).empty());
}

static void TestRecorderCapacity()
{
    SimDebugRecorder r;
    r.SetEnabled(true);
    r.SetCapacity(100, 10);
    for (int i = 0; i < 250; ++i) { r.AddEvent(MakeEvent(1, SimEventKind::MoveStart)); }
    SimRecorderStats s = r.GetStats();
    CHECK(s.eventCount == 100 && s.eventsDropped == 150 && s.eventsRecordedTotal == 250);
    CHECK(s.oldestEventId == 151);                       // 捨てた後の、一番古い番号
    CHECK(r.Events().front().eventId == 151 && r.Events().back().eventId == 250);

    // 判断の内訳: 上限で古い物が捨てられ、捨てた物は探しても無い。残った物は、idで探せる。
    uint64_t firstId = 0, lastId = 0;
    for (int i = 0; i < 25; ++i)
    {
        SimDecisionDetail d; d.week = i;
        const uint64_t id = r.AddDetail(d);
        if (i == 0) { firstId = id; }
        lastId = id;
    }
    s = r.GetStats();
    CHECK(s.detailCount == 10 && s.detailsDropped == 15);
    CHECK(r.FindDetail(firstId) == nullptr);             // 捨てられた
    CHECK(r.FindDetail(lastId) != nullptr && r.FindDetail(lastId)->week == 24);
    CHECK(r.FindDetail(0) == nullptr);
    CHECK(s.estimatedBytes > 0);

    // 容量を小さくすると、古い物を捨てる。
    r.SetCapacity(40, 5);
    CHECK(r.GetStats().eventCount == 40 && r.GetStats().detailCount == 5);

    // 新しい世界: 記録は消え、runIdが進む。通し番号は戻らない。
    const uint32_t oldRun = r.GetRunId();
    const uint64_t lastEvent = r.Events().back().eventId;
    r.StartNewRun();
    CHECK(r.GetRunId() == oldRun + 1 && r.Events().empty() && r.GetStats().eventsDropped == 0);
    const uint64_t next = r.AddEvent(MakeEvent(1, SimEventKind::MoveStart));
    CHECK(next > lastEvent);
}

static void TestInvariants()
{
    ResidentSimSettings settings;
    SimInvariantInput input;
    input.settings = &settings;

    // 正常な状態は、どの状態でも違反にならない。
    for (ResidentAgentState state : { ResidentAgentState::AtHome, ResidentAgentState::GoingOut, ResidentAgentState::Visiting, ResidentAgentState::ReturningHome,
                                      ResidentAgentState::Homeless, ResidentAgentState::Stranded })
    {
        ResidentAgent a;
        a.objectId = 1;
        a.state = state;
        a.homeObjectId = (state == ResidentAgentState::Homeless) ? 0 : 10;
        a.targetFacilityId = (state == ResidentAgentState::GoingOut || state == ResidentAgentState::Visiting) ? 20 : 0;
        if (state == ResidentAgentState::GoingOut || state == ResidentAgentState::ReturningHome)
        {
            a.waypoints = { {0, 0, 0}, {1, 0, 0}, {2, 0, 0} };
            a.currentWaypointIndex = 1;
        }
        CHECK(CheckResidentInvariants(a, input) == 0);
    }

    // 設計上ありえる状態は、違反にしない。
    {
        ResidentAgent a;
        a.state = ResidentAgentState::ReturningHome; a.homeObjectId = 10; a.targetFacilityId = 0;      // 帰宅中は行き先が0でよい
        a.waypoints = { {0, 0, 0}, {1, 0, 0} };
        CHECK(CheckResidentInvariants(a, input) == 0);
        a.waypoints.clear();                                                                          // 経路が無い移動は、到着として扱われる
        CHECK(CheckResidentInvariants(a, input) == 0);

        ResidentAgent b;                                                                              // ペナルティで実効の上限が下がっても、体力は下がらない
        b.homeObjectId = 10; b.traits.staminaCapBase = 100.0f; b.penaltyLevel = 3; b.stamina = 95.0f;
        CHECK(EffectiveStaminaCap(b, settings) < b.stamina);
        CHECK(CheckResidentInvariants(b, input) == 0);
    }

    // 壊れた状態は、違反にする。
    auto flags = [&](ResidentAgent a) { return CheckResidentInvariants(a, input); };
    auto base = []() { ResidentAgent a; a.homeObjectId = 10; return a; };
    {
        ResidentAgent a = base(); a.stamina = std::numeric_limits<float>::quiet_NaN();
        CHECK(flags(a) == SimAnomalyBit(SimAnomaly::NonFiniteValue));
        a = base(); a.lifeNeed = std::numeric_limits<float>::infinity();
        CHECK(flags(a) & SimAnomalyBit(SimAnomaly::NonFiniteValue));
        a = base(); a.stamina = -1.0f;
        CHECK(flags(a) & SimAnomalyBit(SimAnomaly::StaminaOutOfRange));
        a = base(); a.stamina = a.traits.staminaCapBase + 5.0f;
        CHECK(flags(a) & SimAnomalyBit(SimAnomaly::StaminaOutOfRange));
        a = base(); a.activityNeed = 120.0f;
        CHECK(flags(a) & SimAnomalyBit(SimAnomaly::NeedOutOfRange));
        a = base(); a.satisfaction = -3.0f;
        CHECK(flags(a) & SimAnomalyBit(SimAnomaly::SatisfactionOutOfRange));
        a = base(); a.penaltyLevel = -1;
        CHECK(flags(a) & SimAnomalyBit(SimAnomaly::PenaltyOutOfRange));
        a = base(); a.outingStopCount = -2;
        CHECK(flags(a) & SimAnomalyBit(SimAnomaly::StopCountNegative));
        a = base(); a.state = ResidentAgentState::GoingOut; a.targetFacilityId = 20; a.waypoints = { {0, 0, 0}, {1, 0, 0} }; a.currentWaypointIndex = 7;
        CHECK(flags(a) & SimAnomalyBit(SimAnomaly::WaypointIndexOutOfRange));
        a = base(); a.state = ResidentAgentState::GoingOut; a.targetFacilityId = 0;
        CHECK(flags(a) & SimAnomalyBit(SimAnomaly::MissingTargetFacility));
        a = base(); a.state = ResidentAgentState::Visiting; a.targetFacilityId = 20;
        SimInvariantInput gone = input; gone.targetObjectExists = false;
        CHECK(CheckResidentInvariants(a, gone) & SimAnomalyBit(SimAnomaly::MissingTargetFacility));
        a = base();
        gone = input; gone.homeObjectExists = false;
        CHECK(CheckResidentInvariants(a, gone) & SimAnomalyBit(SimAnomaly::MissingHome));
        a = base(); a.state = ResidentAgentState::AtHome; a.homeObjectId = 0;
        CHECK(flags(a) & SimAnomalyBit(SimAnomaly::MissingHome));
        a = base(); a.state = ResidentAgentState::Homeless; a.homeObjectId = 10;
        CHECK(flags(a) & SimAnomalyBit(SimAnomaly::HomelessWithHome));
        a = base(); gone = input; gone.markerPositionFinite = false;
        CHECK(CheckResidentInvariants(a, gone) & SimAnomalyBit(SimAnomaly::NonFiniteValue));
    }

    // 異常の記録は、新しく違反になった時だけ(同じ異常を、毎ティック記録しない)。直ったら、また記録できる。
    SimDebugRecorder r;
    r.SetEnabled(true);
    const uint32_t m = SimAnomalyBit(SimAnomaly::StaminaOutOfRange);
    CHECK(r.UpdateAnomalyState(5, 0) == 0 && r.FindResident(5) == nullptr);   // 異常の無い住民の記録は、作らない
    CHECK(r.UpdateAnomalyState(5, m) == m);                                    // 新しく違反
    CHECK(r.UpdateAnomalyState(5, m) == 0);                                    // 続いている間は、新しくない
    CHECK(r.UpdateAnomalyState(5, m | SimAnomalyBit(SimAnomaly::NeedOutOfRange)) == SimAnomalyBit(SimAnomaly::NeedOutOfRange));
    CHECK(r.UpdateAnomalyState(5, 0) == 0 && r.FindResident(5)->activeAnomalies == 0);
    CHECK(r.UpdateAnomalyState(5, m) == m);                                    // 直った後の再発は、また記録
}

int main()
{
    std::printf("[weight breakdown]\n"); TestWeightBreakdownExact();
    std::printf("[purpose]\n"); TestChoosePurposeTrace();
    std::printf("[choose facility]\n"); TestChooseFacilityEquivalence();
    std::printf("[stopover/chance]\n"); TestStopoverAndChance();
    std::printf("[recorder]\n"); TestRecorderBasics(); TestRecorderCapacity();
    std::printf("[invariants]\n"); TestInvariants();
    if (g_fail == 0) { std::printf("ALL PASSED\n"); return 0; }
    std::printf("%d FAILED\n", g_fail);
    return 1;
}
