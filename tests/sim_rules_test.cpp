// 住民のルール(ResidentLogic)・施設の表(FacilityData)・施設の集計(FacilityLedger)・週の時計(GameClock)の単体テスト。
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include "GameClock.h"
#include "ResidentLogic.h"
#include "FacilityData.h"
#include "FacilityLedger.h"
#include "Json.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

// ============ 時計(週) ============
static void TestClock()
{
    GameClock c;
    CHECK(Near(c.GetTimeOfDayHours(), 8.0f) && c.GetWeekCount() == 1 && c.GetMonth() == 1 && c.GetYear() == 1 && c.GetWeekOfMonth() == 1);
    CHECK(!c.IsSleeping());
    CHECK(Near(GameClock::kDefaultSecondsPerGameHour, 7.5f) && Near(GameClock::kDefaultSleepSecondsPerGameHour, 1.25f));

    // 8時 → 22時: 14時間 × 7.5秒 = 105秒。その手前では、週末は起きない。
    c.Update(104.0f);
    CHECK(c.ConsumeWeekEnds() == 0 && !c.IsSleeping());
    CHECK(Near(c.GetActiveDeltaHours(), 104.0f / 7.5f));
    c.Update(1.0f);
    CHECK(c.ConsumeWeekEnds() == 1 && c.IsSleeping());
    CHECK(c.ConsumeWeekEnds() == 0);              // 受け取ると、0に戻る
    CHECK(c.GetWeekCount() == 1);                 // 週が替わるのは、起床の時
    // 就寝: 8時間 × 1.25秒 = 10秒。就寝中の時間は、活動時間として数えない。
    c.Update(5.0f);
    CHECK(c.IsSleeping() && Near(c.GetActiveDeltaHours(), 0.0f));
    CHECK(c.GetDayCount() == 2 || Near(c.GetTimeOfDayHours(), 5.0f * 0.0f + c.GetTimeOfDayHours()));
    c.Update(5.0f);
    CHECK(c.ConsumeWeekStarts() == 1 && c.GetWeekCount() == 2 && !c.IsSleeping());
    CHECK(Near(c.GetTimeOfDayHours(), 6.0f, 0.01f));
    CHECK(c.GetDayCount() == 2);                  // 24時を越えて日が替わった

    // 1フレームで、複数の区切りをまたいでも、取りこぼさない。
    GameClock d;
    d.Update(300.0f);
    CHECK(d.ConsumeWeekEnds() == 2 && d.ConsumeWeekStarts() == 2 && d.GetWeekCount() == 3);
    CHECK(Near(d.GetTimeOfDayHours(), 6.0f + 55.0f / 7.5f, 0.01f));
    CHECK(Near(d.GetActiveDeltaHours(), 14.0f + 16.0f + 55.0f / 7.5f, 0.01f));

    // 停止(倍率0)では、進まない。倍速では、速く進む。
    GameClock e;
    e.SetTimeScale(0.0f);
    e.Update(1000.0f);
    CHECK(Near(e.GetTimeOfDayHours(), 8.0f) && e.ConsumeWeekEnds() == 0);
    e.SetTimeScale(4.0f);
    e.Update(105.0f / 4.0f);
    CHECK(e.ConsumeWeekEnds() == 1);

    // 月・年(1か月=4週、1年=12か月)。起床するたびに、週が進む。
    GameClock f;
    f.Update(115.0f);                                       // 8時から始まる最初の週は、少し短い(105+10秒)
    for (int i = 0; i < 3; ++i) { f.Update(130.0f); }       // 以後の週は、120+10秒
    CHECK(f.GetWeekCount() == 5 && f.GetMonth() == 2 && f.GetWeekOfMonth() == 1);
    CHECK(f.ConsumeWeekEnds() == 4 && f.ConsumeWeekStarts() == 4);
    for (int i = 0; i < 43; ++i) { f.Update(130.0f); }
    CHECK(f.GetWeekCount() == 48 && f.GetMonth() == 12 && f.GetWeekOfMonth() == 4 && f.GetYear() == 1 && f.IsLastWeekOfMonth());
    f.Update(130.0f);
    CHECK(f.GetWeekCount() == 49 && f.GetYear() == 2 && f.GetMonth() == 1 && f.GetWeekOfMonth() == 1);

    // 保存と復元(週も)。週の記録が無い古いセーブは、日数を週の数にする。
    Json section = Json::MakeObject();
    SaveContext sc; f.Save(section, sc);
    GameClock g; LoadContext lc; g.Load(section, 1, lc);
    CHECK(g.GetWeekCount() == 49 && Near(g.GetTimeOfDayHours(), f.GetTimeOfDayHours()));
    CHECK(g.ConsumeWeekEnds() == 0 && g.ConsumeWeekStarts() == 0);
    Json old = Json::MakeObject(); old.Set("timeOfDayHours", 9.0f); old.Set("dayCount", 7);
    GameClock h; h.Load(old, 1, lc);
    CHECK(h.GetWeekCount() == 7 && h.GetDayCount() == 7);
    h.ResetToDefault();
    CHECK(h.GetWeekCount() == 1 && Near(h.GetTimeOfDayHours(), 8.0f));
}

// ============ 乱数 ============
static void TestRandom()
{
    SimRandom a(42), b(42), c(43);
    bool same = true, differ = false;
    for (int i = 0; i < 100; ++i) { float x = a.Next01(), y = b.Next01(), z = c.Next01(); if (x != y) { same = false; } if (x != z) { differ = true; } CHECK(x >= 0.0f && x < 1.0f); }
    CHECK(same && differ);
    SimRandom z(0);                       // 種0でも、止まらない
    CHECK(z.NextU32() != 0);
    SimRandom r(7);
    float lo = 100, hi = -100; for (int i = 0; i < 1000; ++i) { float v = r.Range(5.0f, 8.0f); lo = std::fmin(lo, v); hi = std::fmax(hi, v); }
    CHECK(lo >= 5.0f && hi < 8.0f && hi - lo > 2.5f);
    double sum = 0; SimRandom u(99); for (int i = 0; i < 20000; ++i) { sum += u.Next01(); }
    CHECK(std::fabs(sum / 20000.0 - 0.5) < 0.02);
}

// ============ 住民の特徴・初期化 ============
static void TestTraits()
{
    ResidentSimSettings s;
    std::set<int> variety;
    for (uint32_t seed = 1; seed <= 200; ++seed)
    {
        ResidentTraits t = MakeResidentTraits(seed, s.staminaMax);
        CHECK(t.staminaCapBase >= 85.0f - 1e-3f && t.staminaCapBase <= 115.0f + 1e-3f);
        CHECK(t.needDecayScale >= 0.8f - 1e-3f && t.needDecayScale <= 1.25f + 1e-3f);
        CHECK(t.qualityWeight >= 0.6f - 1e-3f && t.qualityWeight <= 1.5f + 1e-3f);
        CHECK(t.priceSensitivity >= 0.5f - 1e-3f && t.priceSensitivity <= 1.6f + 1e-3f);
        CHECK(t.distanceTolerance >= 0.7f - 1e-3f && t.distanceTolerance <= 1.5f + 1e-3f);
        CHECK(t.spontaneity >= 0.5f - 1e-3f && t.spontaneity <= 1.6f + 1e-3f);
        variety.insert(static_cast<int>(t.qualityWeight * 100));
        ResidentTraits again = MakeResidentTraits(seed, s.staminaMax);
        CHECK(again.qualityWeight == t.qualityWeight && again.spontaneity == t.spontaneity);
    }
    CHECK(variety.size() > 50);                     // 住民ごとに、値が散らばる
    ResidentAgent a, b;
    InitializeResident(a, 5, s); InitializeResident(b, 6, s);
    CHECK(a.lifeNeed >= s.initialNeedMin && a.lifeNeed <= s.initialNeedMax && a.activityNeed >= s.initialNeedMin && a.activityNeed <= s.initialNeedMax);
    CHECK(Near(a.stamina, EffectiveStaminaCap(a, s)));
    CHECK(a.lifeNeed != b.lifeNeed || a.activityNeed != b.activityNeed);
    ResidentAgent a2; InitializeResident(a2, 5, s);
    CHECK(a2.lifeNeed == a.lifeNeed && a2.traits.qualityWeight == a.traits.qualityWeight);
}

// ============ 施設の表 ============
static void TestFacilityData()
{
    CHECK(FindFacilityDefinition(BuildingType::House) == nullptr && FindFacilityDefinition(BuildingType::Office) == nullptr);
    CHECK(!IsFacility(BuildingType::House) && IsFacility(BuildingType::Shop));
    const BuildingType types[] = { BuildingType::Shop, BuildingType::Supermarket, BuildingType::Cinema, BuildingType::Cafe };
    for (BuildingType t : types)
    {
        const FacilityDefinition* f = FindFacilityDefinition(t);
        CHECK(f != nullptr && f->type == t && f->name && f->name[0] && f->quality > 0.0f && f->pricePerVisit > 0 && f->visitDurationHours > 0.0f);
        for (int i = 0; i < kUsagePurposeCount; ++i) { CHECK(f->purposeSuitability[i] > 0.0f); }
        CHECK(f->category == FacilityCategory::Daily ? (f->lifeRecovery > 0.0f) : (f->activityRecovery > 0.0f));
        CHECK(GetBuildingDefinition(t).cost > 0);
    }
    // コンビニとスーパーは、単純な上位互換ではない(目的の適性が逆)。
    const FacilityDefinition* conv = FindFacilityDefinition(BuildingType::Shop);
    const FacilityDefinition* super = FindFacilityDefinition(BuildingType::Supermarket);
    CHECK(conv->purposeSuitability[(int)UsagePurpose::TopUp] > super->purposeSuitability[(int)UsagePurpose::TopUp]);
    CHECK(super->purposeSuitability[(int)UsagePurpose::BulkBuy] > conv->purposeSuitability[(int)UsagePurpose::BulkBuy]);
    CHECK(GetBuildingDefinition(BuildingType::House).representativeAgentCount == 1);
    CHECK(kBuildingTypeCount == 6);
}

// ============ 施設の選び方 ============
static ResidentAgent MakeAgent(const ResidentSimSettings& s)
{
    ResidentAgent a;
    a.traits = ResidentTraits();   // 全て標準(1)
    a.traits.staminaCapBase = 100.0f;
    a.stamina = 100.0f; a.lifeNeed = 50.0f; a.activityNeed = 100.0f;
    (void)s;
    return a;
}

static void TestWeights()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    const FacilityDefinition* conv = FindFacilityDefinition(BuildingType::Shop);
    const FacilityDefinition* super = FindFacilityDefinition(BuildingType::Supermarket);
    const FacilityDefinition* cinema = FindFacilityDefinition(BuildingType::Cinema);

    // 距離: 遠いほど軽い。0でも正。
    float w0 = ComputeFacilityWeight(a, *conv, UsagePurpose::TopUp, 0.0f, s);
    float w10 = ComputeFacilityWeight(a, *conv, UsagePurpose::TopUp, 10.0f, s);
    float w40 = ComputeFacilityWeight(a, *conv, UsagePurpose::TopUp, 40.0f, s);
    CHECK(w0 > w10 && w10 > w40 && w40 > 0.0f);
    // 距離の目安(14マス)で、約 1/e。
    CHECK(Near(ComputeFacilityWeight(a, *conv, UsagePurpose::TopUp, 14.0f, s) / w0, std::exp(-1.0f), 0.01f));

    // 買い足しはコンビニ、まとめ買いはスーパー(同じ距離なら)。
    CHECK(ComputeFacilityWeight(a, *conv, UsagePurpose::TopUp, 8.0f, s) > ComputeFacilityWeight(a, *super, UsagePurpose::TopUp, 8.0f, s));
    CHECK(ComputeFacilityWeight(a, *super, UsagePurpose::BulkBuy, 8.0f, s) > ComputeFacilityWeight(a, *conv, UsagePurpose::BulkBuy, 8.0f, s));

    // 品質・値段・住民の特徴の効き方。
    FacilityDefinition good = *conv; good.quality = 2.0f;
    FacilityDefinition bad = *conv; bad.quality = 0.5f;
    CHECK(ComputeFacilityWeight(a, good, UsagePurpose::TopUp, 5.0f, s) > ComputeFacilityWeight(a, bad, UsagePurpose::TopUp, 5.0f, s));
    FacilityDefinition pricey = *conv; pricey.pricePerVisit = 1200;
    CHECK(ComputeFacilityWeight(a, pricey, UsagePurpose::TopUp, 5.0f, s) < ComputeFacilityWeight(a, *conv, UsagePurpose::TopUp, 5.0f, s));
    ResidentAgent picky = a; picky.traits.qualityWeight = 1.5f;
    ResidentAgent casual = a; casual.traits.qualityWeight = 0.6f;
    CHECK(ComputeFacilityWeight(picky, good, UsagePurpose::TopUp, 5.0f, s) / ComputeFacilityWeight(picky, bad, UsagePurpose::TopUp, 5.0f, s)
        > ComputeFacilityWeight(casual, good, UsagePurpose::TopUp, 5.0f, s) / ComputeFacilityWeight(casual, bad, UsagePurpose::TopUp, 5.0f, s));
    ResidentAgent thrifty = a; thrifty.traits.priceSensitivity = 1.6f;
    ResidentAgent spender = a; spender.traits.priceSensitivity = 0.5f;
    CHECK(ComputeFacilityWeight(thrifty, pricey, UsagePurpose::TopUp, 5.0f, s) < ComputeFacilityWeight(spender, pricey, UsagePurpose::TopUp, 5.0f, s));
    ResidentAgent walker = a; walker.traits.distanceTolerance = 1.5f;
    ResidentAgent homebody = a; homebody.traits.distanceTolerance = 0.7f;
    CHECK(ComputeFacilityWeight(walker, *cinema, UsagePurpose::Movie, 30.0f, s) > ComputeFacilityWeight(homebody, *cinema, UsagePurpose::Movie, 30.0f, s));

    // 重みの下限(到達できるなら、極端に遠くても0にならない)。
    CHECK(ComputeFacilityWeight(a, *conv, UsagePurpose::Movie, 100000.0f, s) >= s.minimumWeight);

    // 必要な体力は、距離とともに増える。
    CHECK(RequiredStamina(*conv, 20.0f, s) > RequiredStamina(*conv, 5.0f, s));
    CHECK(Near(RequiredStamina(*conv, 0.0f, s), conv->staminaCost));
}

static FacilityCandidate Cand(uint32_t id, BuildingType t, bool reachable, float path)
{
    FacilityCandidate c; c.objectId = id; c.type = t; c.reachable = reachable; c.pathLength = path; return c;
}

static void TestChoose()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    SimRandom rng(1);
    OutingPurposeChoice daily; daily.needDriven = true; daily.category = FacilityCategory::Daily; daily.purpose = UsagePurpose::TopUp;
    std::vector<CandidateEvaluation> ev;

    // 候補なし → -1
    CHECK(ChooseFacility(a, {}, daily, s, rng, ev) == -1 && ev.empty());

    // 到達不可・カテゴリ違い・体力不足は、抽選の対象外(理由が記録される)。
    std::vector<FacilityCandidate> list = {
        Cand(1, BuildingType::Shop, false, 0.0f),            // 到達不可
        Cand(2, BuildingType::Cinema, true, 5.0f),           // カテゴリ違い
        Cand(3, BuildingType::Supermarket, true, 1000.0f),   // 遠すぎて体力が足りない
        Cand(4, BuildingType::Shop, true, 6.0f),             // 行ける
    };
    int idx = ChooseFacility(a, list, daily, s, rng, ev);
    CHECK(idx == 3 && ev.size() == 4);
    CHECK(ev[0].reject == CandidateReject::Unreachable && ev[0].weight == 0.0f && !ev[0].chosen);
    CHECK(ev[1].reject == CandidateReject::WrongCategory && ev[1].weight == 0.0f);
    CHECK(ev[2].reject == CandidateReject::NotEnoughStamina && ev[2].weight == 0.0f);
    CHECK(ev[3].reject == CandidateReject::None && ev[3].weight > 0.0f && ev[3].chosen && Near(ev[3].chance, 1.0f));

    // 全部が対象外 → -1(利用扱いにならない)。
    std::vector<FacilityCandidate> none = { Cand(1, BuildingType::Shop, false, 0.0f), Cand(2, BuildingType::Supermarket, false, 0.0f) };
    CHECK(ChooseFacility(a, none, daily, s, rng, ev) == -1);
    for (const auto& e : ev) { CHECK(!e.chosen && e.chance == 0.0f); }

    // 確率の合計は1。選ばれた1つだけ chosen。
    std::vector<FacilityCandidate> two = { Cand(1, BuildingType::Shop, true, 6.0f), Cand(2, BuildingType::Supermarket, true, 6.0f) };
    ChooseFacility(a, two, daily, s, rng, ev);
    CHECK(Near(ev[0].chance + ev[1].chance, 1.0f, 1e-4f));
    int chosenCount = 0; for (const auto& e : ev) { chosenCount += e.chosen ? 1 : 0; }
    CHECK(chosenCount == 1);

    // 統計: 重みの比どおりに選ばれる(強い方が必ず選ばれるわけではない)。同じ条件の2つは、半々に近い。
    {
        std::vector<FacilityCandidate> same = { Cand(1, BuildingType::Shop, true, 6.0f), Cand(2, BuildingType::Shop, true, 6.0f) };
        std::map<int, int> count; SimRandom r(2024);
        for (int i = 0; i < 20000; ++i) { count[ChooseFacility(a, same, daily, s, r, ev)]++; }
        CHECK(std::abs(count[0] - 10000) < 400);
    }
    {
        // 近いコンビニと、遠いコンビニ: 近い方が多いが、遠い方も選ばれる。
        std::vector<FacilityCandidate> nf = { Cand(1, BuildingType::Shop, true, 4.0f), Cand(2, BuildingType::Shop, true, 18.0f) };
        std::map<int, int> count; SimRandom r(77);
        for (int i = 0; i < 20000; ++i) { count[ChooseFacility(a, nf, daily, s, r, ev)]++; }
        CHECK(count[0] > count[1] && count[1] > 1000);
        ChooseFacility(a, nf, daily, s, r, ev);
        const double expected = ev[0].chance * 20000.0;
        CHECK(std::fabs(count[0] - expected) < 500.0);
    }
    {
        // 品質の高い施設ばかり100%にならない: 品質1.5の映画館と、品質1.1のカフェ(娯楽・軽食目的)。
        ResidentAgent b = a; b.activityNeed = 30.0f;
        OutingPurposeChoice leisure; leisure.needDriven = true; leisure.category = FacilityCategory::Leisure; leisure.purpose = UsagePurpose::Light;
        std::vector<FacilityCandidate> lc = { Cand(1, BuildingType::Cinema, true, 8.0f), Cand(2, BuildingType::Cafe, true, 8.0f) };
        std::map<int, int> count; SimRandom r(5);
        for (int i = 0; i < 5000; ++i) { count[ChooseFacility(b, lc, leisure, s, r, ev)]++; }
        CHECK(count[0] > 0 && count[1] > 0);
    }

    // 距離の違いが、住宅の位置による選択の違いになる: 近いコンビニ(2)が遠い(1)より選ばれやすい、位置を入れ替えると逆転。
    {
        std::vector<FacilityCandidate> p1 = { Cand(1, BuildingType::Shop, true, 3.0f), Cand(2, BuildingType::Shop, true, 15.0f) };
        std::vector<FacilityCandidate> p2 = { Cand(1, BuildingType::Shop, true, 15.0f), Cand(2, BuildingType::Shop, true, 3.0f) };
        std::vector<CandidateEvaluation> e1, e2; SimRandom r(3);
        ChooseFacility(a, p1, daily, s, r, e1); ChooseFacility(a, p2, daily, s, r, e2);
        CHECK(e1[0].chance > e1[1].chance && e2[1].chance > e2[0].chance);
    }

    // 同じ種・同じ条件なら、同じ選択(再現できる)。
    {
        std::vector<FacilityCandidate> three = { Cand(1, BuildingType::Shop, true, 4.0f), Cand(2, BuildingType::Shop, true, 8.0f), Cand(3, BuildingType::Shop, true, 12.0f) };
        SimRandom r1(555), r2(555);
        for (int i = 0; i < 50; ++i) { CHECK(ChooseFacility(a, three, daily, s, r1, ev) == ChooseFacility(a, three, daily, s, r2, ev)); }
    }
}

static void TestPick()
{
    CHECK(PickWeightedIndex({}, 0.5f) == -1);
    CHECK(PickWeightedIndex({ 0.0f, 0.0f }, 0.5f) == -1);
    CHECK(PickWeightedIndex({ -1.0f, 2.0f }, 0.0f) == 1);          // 負・0は、飛ばす
    CHECK(PickWeightedIndex({ 1.0f, 1.0f }, 0.0f) == 0);
    CHECK(PickWeightedIndex({ 1.0f, 1.0f }, 0.49f) == 0);
    CHECK(PickWeightedIndex({ 1.0f, 1.0f }, 0.5f) == 1);
    CHECK(PickWeightedIndex({ 1.0f, 1.0f }, 0.9999999f) == 1);
    CHECK(PickWeightedIndex({ 1.0f, 3.0f, 0.0f }, 0.9999999f) == 1);  // 重み0の末尾は選ばない
    CHECK(PickWeightedIndex({ 1.0f, 1.0f }, 5.0f) == 1 && PickWeightedIndex({ 1.0f, 1.0f }, -1.0f) == 0);
}

static void TestPurpose()
{
    ResidentSimSettings s;
    SimRandom rng(11);
    ResidentAgent a = MakeAgent(s);

    a.lifeNeed = 40.0f; a.activityNeed = 95.0f;
    for (int i = 0; i < 100; ++i) { auto p = ChoosePurpose(a, s, rng); CHECK(p.needDriven && p.category == FacilityCategory::Daily && (p.purpose == UsagePurpose::TopUp || p.purpose == UsagePurpose::BulkBuy)); }
    a.lifeNeed = 95.0f; a.activityNeed = 40.0f;
    for (int i = 0; i < 100; ++i) { auto p = ChoosePurpose(a, s, rng); CHECK(p.needDriven && p.category == FacilityCategory::Leisure && (p.purpose == UsagePurpose::Movie || p.purpose == UsagePurpose::Light)); }
    a.lifeNeed = 30.0f; a.activityNeed = 60.0f;       // 両方不足 → より不足している生活
    for (int i = 0; i < 50; ++i) { CHECK(ChoosePurpose(a, s, rng).category == FacilityCategory::Daily); }
    a.lifeNeed = 60.0f; a.activityNeed = 30.0f;
    for (int i = 0; i < 50; ++i) { CHECK(ChoosePurpose(a, s, rng).category == FacilityCategory::Leisure); }
    // 生活が大きく不足しているほど、まとめ買いが増える。
    auto bulkRate = [&](float life) { ResidentAgent b = a; b.lifeNeed = life; b.activityNeed = 100.0f; int bulk = 0; SimRandom r(8); for (int i = 0; i < 4000; ++i) { bulk += (ChoosePurpose(b, s, r).purpose == UsagePurpose::BulkBuy) ? 1 : 0; } return bulk / 4000.0; };
    CHECK(bulkRate(10.0f) > bulkRate(70.0f) + 0.2);
    // 需要が足りている時は、自発的(娯楽が多め。日用品もある)。
    a.lifeNeed = 100.0f; a.activityNeed = 100.0f;
    int leisure = 0, daily = 0; SimRandom r2(9);
    for (int i = 0; i < 5000; ++i) { auto p = ChoosePurpose(a, s, r2); CHECK(!p.needDriven); (p.category == FacilityCategory::Leisure ? leisure : daily)++; }
    CHECK(leisure > daily && daily > 800);
    // ちょうど80は充足(不足ではない)。
    a.lifeNeed = 80.0f; a.activityNeed = 80.0f;
    CHECK(!IsLifeNeedUnmet(a, s) && !IsActivityNeedUnmet(a, s));
    a.lifeNeed = 79.9f; CHECK(IsLifeNeedUnmet(a, s));
}

// ============ 利用の結果・時間・週 ============
static void TestUseAndWeek()
{
    ResidentSimSettings s;
    ResidentAgent a = MakeAgent(s);
    const FacilityDefinition* super = FindFacilityDefinition(BuildingType::Supermarket);
    a.lifeNeed = 40.0f; a.stamina = 50.0f;
    ApplyFacilityUse(a, *super, 7, 3, 12.5f, s);
    CHECK(Near(a.lifeNeed, 100.0f) && Near(a.stamina, 50.0f - super->staminaCost));      // 40+70 → 上限100
    CHECK(a.history.size() == 1 && a.history[0].facilityObjectId == 7 && a.history[0].weekIndex == 3 && Near(a.history[0].hour, 12.5f));
    a.stamina = 2.0f; ApplyFacilityUse(a, *super, 8, 3, 13.0f, s);
    CHECK(a.stamina == 0.0f);                                                             // 体力は、0を下回らない
    for (uint32_t i = 0; i < 20; ++i) { ApplyFacilityUse(a, *super, 100 + i, 4, 1.0f, s); }
    CHECK(a.history.size() == ResidentAgent::kMaxHistory && a.history[0].facilityObjectId == 119);   // 新しい順、最大8件

    // 時間の経過: 不足の継続・充足の継続・家での体力回復(上限まで)。
    ResidentAgent b = MakeAgent(s);
    b.lifeNeed = 50.0f; b.activityNeed = 90.0f; b.stamina = 10.0f; b.state = ResidentAgentState::AtHome;
    AdvanceResidentTime(b, 2.0f, s);
    CHECK(Near(b.unmetLifeHours, 2.0f) && Near(b.unmetActivityHours, 0.0f) && Near(b.satisfiedHours, 0.0f));
    CHECK(Near(b.stamina, 10.0f + s.staminaHomeRecoveryPerHour * 2.0f));
    b.lifeNeed = 90.0f; AdvanceResidentTime(b, 3.0f, s);
    CHECK(Near(b.unmetLifeHours, 0.0f) && Near(b.satisfiedHours, 3.0f));                // 充足したら、不足の継続は数え直し
    b.lifeNeed = 50.0f; AdvanceResidentTime(b, 1.0f, s);
    CHECK(Near(b.unmetLifeHours, 1.0f) && Near(b.satisfiedHours, 0.0f));                // 不足になったら、充足の継続は数え直し
    b.lifeNeed = 90.0f; AdvanceResidentTime(b, 3.0f, s);
    b.stamina = 99.0f; AdvanceResidentTime(b, 10.0f, s);
    CHECK(Near(b.stamina, EffectiveStaminaCap(b, s)));
    b.state = ResidentAgentState::GoingOut; b.stamina = 10.0f; AdvanceResidentTime(b, 5.0f, s);
    CHECK(Near(b.stamina, 10.0f));                      // 外出中は、家での回復をしない
    AdvanceResidentTime(b, 0.0f, s); AdvanceResidentTime(b, -1.0f, s);
    CHECK(Near(b.satisfiedHours, 3.0f + 10.0f + 5.0f));

    // ---- 週末 ----
    // (1) 不足が猶予を超えて続いた → ペナルティ +1。判定の後で、自然減少。継続時間は0へ。
    ResidentAgent c = MakeAgent(s);
    c.lifeNeed = 50.0f; c.activityNeed = 95.0f; c.unmetLifeHours = s.unmetGraceHours + 1.0f;     // 週末の今も不足
    ProcessResidentWeekEnd(c, s);
    CHECK(c.penaltyLevel == 1);
    CHECK(Near(c.lifeNeed, 50.0f - s.weeklyDecayLife) && Near(c.activityNeed, 95.0f - s.weeklyDecayActivity));
    CHECK(c.unmetLifeHours == 0.0f && c.unmetActivityHours == 0.0f && c.satisfiedHours == 0.0f);
    CHECK(Near(EffectiveStaminaCap(c, s), 100.0f - s.penaltyStaminaCapStep));
    // 最大の段階を超えない。
    for (int i = 0; i < 10; ++i) { c.activityNeed = 40.0f; c.unmetActivityHours = s.unmetGraceHours + 1.0f; ProcessResidentWeekEnd(c, s); }
    CHECK(c.penaltyLevel == s.maxPenaltyLevel);

    // (2) 週末までに改善できた(不足の時間が猶予以内) → ペナルティ無し。
    ResidentAgent d = MakeAgent(s);
    d.lifeNeed = 50.0f; d.unmetLifeHours = s.unmetGraceHours - 1.0f;                       // 今も不足だが、猶予以内
    ProcessResidentWeekEnd(d, s);
    CHECK(d.penaltyLevel == 0);
    ResidentAgent d2 = MakeAgent(s);
    d2.lifeNeed = 90.0f; d2.unmetLifeHours = s.unmetGraceHours + 5.0f;                    // 長く不足していたが、週末までに改善できた(今は充足)
    ProcessResidentWeekEnd(d2, s);
    CHECK(d2.penaltyLevel == 0);

    // (3) 週末の自然減少で、新しく不足になった分は、その週の罰にならない: ずっと充足していた週 → 週末に70まで下がっても、ペナルティ無し。
    ResidentAgent e = MakeAgent(s);
    e.lifeNeed = 100.0f; e.activityNeed = 100.0f; e.satisfiedHours = 15.0f;
    ProcessResidentWeekEnd(e, s);
    CHECK(e.penaltyLevel == 0 && e.lifeNeed < s.needSatisfiedThreshold);
    CHECK(e.unmetLifeHours == 0.0f);                    // 新しい週の不足は、ここから数える

    // (4) ペナルティの回復: 充足が十分長く続いた週は、1段階軽くなる。足りなければ、そのまま。
    ResidentAgent f = MakeAgent(s);
    f.penaltyLevel = 2; f.satisfiedHours = s.recoverSatisfiedHours - 0.5f;
    ProcessResidentWeekEnd(f, s);
    CHECK(f.penaltyLevel == 2);
    f.satisfiedHours = s.recoverSatisfiedHours;
    ProcessResidentWeekEnd(f, s);
    CHECK(f.penaltyLevel == 1);
    f.satisfiedHours = 100.0f; ProcessResidentWeekEnd(f, s); ProcessResidentWeekEnd(f, s);
    CHECK(f.penaltyLevel == 0);                         // 0より下がらない
    // 不足が猶予を超えた週は、充足の時間が長くても、悪化する側が優先される。
    f.satisfiedHours = 100.0f; f.lifeNeed = 40.0f; f.unmetLifeHours = s.unmetGraceHours + 2.0f;
    ProcessResidentWeekEnd(f, s);
    CHECK(f.penaltyLevel == 1);

    // 需要は0を下回らない。体力の上限の下限。
    ResidentAgent g = MakeAgent(s);
    g.lifeNeed = 5.0f; g.activityNeed = 5.0f; ProcessResidentWeekEnd(g, s);
    CHECK(g.lifeNeed == 0.0f && g.activityNeed == 0.0f);
    g.penaltyLevel = 99; CHECK(EffectiveStaminaCap(g, s) >= 20.0f - 1e-3f);

    // 需要の減りやすさ(住民の特徴)が、週末の減少に効く。
    ResidentAgent h1 = MakeAgent(s), h2 = MakeAgent(s);
    h1.traits.needDecayScale = 1.25f; h2.traits.needDecayScale = 0.8f;
    h1.lifeNeed = h2.lifeNeed = 100.0f;
    ProcessResidentWeekEnd(h1, s); ProcessResidentWeekEnd(h2, s);
    CHECK(h1.lifeNeed < h2.lifeNeed);

    // ---- 週の始まり ----
    ResidentAgent w = MakeAgent(s);
    w.stamina = 10.0f; ProcessResidentWeekStart(w, s);
    CHECK(Near(w.stamina, 10.0f + s.staminaSleepRecovery));
    w.stamina = 80.0f; ProcessResidentWeekStart(w, s);
    CHECK(Near(w.stamina, EffectiveStaminaCap(w, s)));
    w.penaltyLevel = 3; w.stamina = 5.0f; ProcessResidentWeekStart(w, s);
    CHECK(w.stamina <= EffectiveStaminaCap(w, s) + 1e-3f);          // ペナルティで、上限が下がる
}

// ============ 施設の集計 ============
static void TestLedger()
{
    FacilityLedger l;
    CHECK(l.GetRecords().empty() && l.SumTotal().visits == 0 && l.Find(1) == nullptr);
    l.RecordVisit(1, BuildingType::Shop, 120);
    l.RecordVisit(1, BuildingType::Shop, 120);
    l.RecordVisit(2, BuildingType::Cinema, 600);
    CHECK(l.GetRecords().size() == 2);
    CHECK(l.Find(1)->thisWeek.visits == 2 && l.Find(1)->thisWeek.sales == 240 && l.Find(2)->total.sales == 600);
    CHECK(l.SumThisWeek().visits == 3 && l.SumThisWeek().sales == 840);
    l.OnWeekEnd(false);
    CHECK(l.SumThisWeek().visits == 0 && l.SumLastWeek().visits == 3 && l.SumLastWeek().sales == 840);
    CHECK(l.SumThisMonth().visits == 3 && l.SumTotal().sales == 840);
    l.RecordVisit(1, BuildingType::Shop, 120);
    l.OnWeekEnd(true);                                              // 月末: 月の集計も0へ。通算は残る
    CHECK(l.SumThisMonth().visits == 0 && l.SumTotal().visits == 4 && l.SumLastWeek().visits == 1);
    l.OnWeekEnd(false);
    CHECK(l.SumLastWeek().visits == 0 && l.SumTotal().visits == 4);
    FacilityRecord r; r.objectId = 1; r.type = BuildingType::Shop; r.total.visits = 99;
    l.SetRecord(r);
    CHECK(l.GetRecords().size() == 2 && l.Find(1)->total.visits == 99);
    l.Clear();
    CHECK(l.GetRecords().empty());
}

static void TestDemolition()
{
    // 解体済みの施設の記録: 過去の売上・来訪者数・種類を残し、合計にも入る。解体後の利用は、足されない(呼び出さない)。
    FacilityLedger ledger;
    ledger.RecordVisit(11, BuildingType::Shop, 120);
    ledger.RecordVisit(11, BuildingType::Shop, 120);
    ledger.RecordVisit(12, BuildingType::Cafe, 300);
    CHECK(!ledger.Find(11)->demolished, "not demolished at first");
    ledger.MarkDemolished(11);
    ledger.MarkDemolished(999); // 記録の無い施設は、何も起きない
    CHECK(ledger.Find(11)->demolished && ledger.Find(11)->total.visits == 2 && ledger.Find(11)->total.sales == 240, "demolished record keeps visits/sales");
    CHECK(ledger.Find(11)->type == BuildingType::Shop, "demolished record keeps the type");
    CHECK(!ledger.Find(12)->demolished, "other facility unaffected");
    CHECK(ledger.SumTotal().visits == 3 && ledger.SumTotal().sales == 540, "totals still include the demolished facility");
    CHECK(ledger.SumThisWeek().sales == 540, "this week still includes it");
    ledger.OnWeekEnd(false);
    CHECK(ledger.SumLastWeek().sales == 540 && ledger.Find(11)->lastWeek.sales == 240, "week roll-over still includes the demolished record");
    CHECK(ledger.GetRecords().size() == 2, "record count unchanged (not merged into one)");
    FacilityRecord copy = *ledger.Find(11);
    ledger.Clear(); ledger.SetRecord(copy);
    CHECK(ledger.Find(11) && ledger.Find(11)->demolished, "SetRecord restores the flag");

    // 住居なしのずらし(本物の関数)
    CHECK(HomelessOffset(0, 0.2f).x == 0.0f && HomelessOffset(0, 0.2f).y == 0.0f, "HomelessOffset(0) = origin");
    float minDist = 1e9f; DirectX::XMFLOAT2 pts[30];
    for (int i = 0; i < 30; ++i) { pts[i] = HomelessOffset(i, 0.2f); }
    for (int i = 0; i < 30; ++i) for (int j = i + 1; j < 30; ++j) { minDist = (std::min)(minDist, std::hypot(pts[i].x - pts[j].x, pts[i].y - pts[j].y)); }
    CHECK(minDist > 0.1f, "HomelessOffset: no overlap among 30 (min %.3f)", minDist);

    // 住居なしの状態は、需要・体力の週の処理を免除されない(ProcessResidentWeekEnd は状態を見ない)。
    ResidentSimSettings settings;
    ResidentAgent agent; InitializeResident(agent, 5u, settings);
    agent.state = ResidentAgentState::Homeless; agent.homeObjectId = 0;
    const float lifeBefore = agent.lifeNeed;
    ProcessResidentWeekEnd(agent, settings);
    CHECK(agent.lifeNeed < lifeBefore, "week-end decay still applies to a homeless resident (%.1f -> %.1f)", lifeBefore, agent.lifeNeed);
}
static void TestRoadLoss()
{
    ResidentSimSettings settings;
    ResidentAgent agent; InitializeResident(agent, 9u, settings);
    CHECK(agent.satisfaction == settings.satisfactionMax && settings.satisfactionMax == 100.0f, "new resident starts at max satisfaction");
    const float cost1 = RoadLossStaminaCost(2.0f, settings);
    const float cost2 = RoadLossStaminaCost(4.0f, settings);
    CHECK(cost1 > 0.0f && std::fabs(cost2 - cost1 * 2.0f) < 1e-4f, "detour stamina cost is proportional to the route length (%.3f / %.3f)", cost1, cost2);
    CHECK(RoadLossStaminaCost(-1.0f, settings) == 0.0f, "negative length costs 0");
    ResidentSimSettings free = settings; free.roadLossStaminaCostScale = 0.0f;
    CHECK(RoadLossStaminaCost(5.0f, free) == 0.0f, "scale 0 turns the stamina cost off");

    ApplyRoadLossPenalty(agent, false, settings);
    CHECK(std::fabs(agent.satisfaction - (100.0f - settings.roadLossRerouteSatisfactionPenalty)) < 1e-4f, "reroute lowers satisfaction by %.1f", settings.roadLossRerouteSatisfactionPenalty);
    ApplyRoadLossPenalty(agent, true, settings);
    CHECK(std::fabs(agent.satisfaction - (100.0f - settings.roadLossRerouteSatisfactionPenalty - settings.roadLossStrandedSatisfactionPenalty)) < 1e-4f, "stranded lowers it more");
    for (int i = 0; i < 50; ++i) { ApplyRoadLossPenalty(agent, true, settings); }
    CHECK(agent.satisfaction == 0.0f, "never below 0 (%.2f)", agent.satisfaction);
    ResidentSimSettings negative = settings; negative.roadLossStrandedSatisfactionPenalty = -10.0f;
    ResidentAgent a2; InitializeResident(a2, 3u, settings);
    ApplyRoadLossPenalty(a2, true, negative);
    CHECK(a2.satisfaction == 100.0f, "a negative setting never raises satisfaction");

}
int main()
{
    TestClock();
    TestRandom();
    TestTraits();
    TestFacilityData();
    TestWeights();
    TestChoose();
    TestPick();
    TestPurpose();
    TestUseAndWeek();
    TestLedger();
    TestDemolition();
    TestRoadLoss();
    if (g_fail == 0) { std::printf("ALL PASSED\n"); return 0; }
    std::printf("FAILED: %d\n", g_fail);
    return 1;
}
