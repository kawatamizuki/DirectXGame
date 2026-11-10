// P2-D(商業エリアの評判)のルールの単体テスト。
// 受け入れ条件:
//   1. 周知前の仮区画では、評判が変化しない。
//   2. 初回の開発猶予中は、計画未達による減点が発生しない。
//   3. 猶予中でも、実際の利用評価(成功率・需要への貢献)による変化は発生する。
//   4. 初回猶予後に最低進捗未達なら、評判が徐々に(1週の上限つきで)低下する。
//   5. 最低進捗を達成すれば、計画未達の減点がなくなる。
//   6. 一度最低進捗を達成した後、店舗数が減れば、再整備の猶予が始まる(過去の最高到達だけでは、達成済みとしない)。
//   7. 再整備の猶予中に店舗数を回復すれば、減点を回避できる。
//   8. 再整備の猶予後も未達なら、徐々に低下する。猶予を、一時的な操作で引き延ばせない。
//   9. 改善によって、低下した評判が回復できる。
//  10. 保存・読み込みで、現在の進捗・過去の実績・猶予の計測・評判が維持される。古いセーブは、中立の1.0で読める。
//  その他: 成功率が100%に固定されても、評判は上がり続けない(頭打ち)。需要が満たされていても減点されない。サンプルが少ない週は、利用の項は0。移設・撤回で、評判・履歴が消えない。
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>
#include "CommercialAreaManager.h"
#include "AreaLedger.h"
#include "AreaReputation.h"
#include "AreaIndicators.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

// 区画1つを持つ、試験用の世界。週ごとに、店舗数と先週の利用を与えて、評判を更新する。
struct World
{
    CommercialAreaManager areas;
    AreaLedger ledger;
    uint32_t id = 0;
    int week = 1;

    World(bool publish = true, int publishWeek = 1)
    {
        id = areas.PlaceArea(0.0f, 0.0f, 0.0f);
        week = publishWeek;
        if (publish) { areas.Publish(id, publishWeek); }
    }
    const CommercialArea& Area() const { return *areas.Find(id); }

    // 1週進めて(週末の更新)、評判を返す。usageは、先週の利用(AreaLedgerの先週に入れる)。
    float Step(int stores, int outings = 0, int visited = 0, int visits = 0, float nominal = 0.0f, float actual = 0.0f)
    {
        ++week;
        AreaRecord record;
        record.areaId = id;
        record.lastWeek.areaResolved = outings;         // 結果が決まった外出の数
        record.lastWeek.areaSucceeded = visited;        // うち、使えた数
        record.lastWeek.visits = visits;
        record.lastWeek.needNominal = nominal;
        record.lastWeek.needActual = actual;
        ledger.SetRecord(record);
        areas.UpdateReputations(week, { { id, stores } }, ledger);
        return Area().reputation;
    }
};

static void TestUnpublished()
{
    World w(false);
    for (int i = 0; i < 20; ++i) { w.Step(0, 20, 20, 20, 100.0f, 100.0f); }
    CHECK(Near(w.Area().reputation, 1.0f));
    CHECK(w.Area().belowMinimumSinceWeek == 0 && w.Area().metMinimumStreakWeeks == 0);
    CHECK(w.Area().achieved == AreaMilestone::None);   // 仮置きでは、達成状態も変わらない
}

static void TestInitialGrace()
{
    World w;
    const CommercialAreaSettings& s = w.areas.Settings();
    // 周知(第1週)から graceWeeks 週は、店が0でも、減点なし(利用の評価も無い週)。
    for (int i = 0; i < s.graceWeeks - 1; ++i) { CHECK(Near(w.Step(0), 1.0f)); }
    CHECK(w.Area().lastPlanState == static_cast<int>(AreaPlanState::InitialGrace));
    CHECK(Near(w.Area().lastPlanTerm, 0.0f));
}

static void TestUseDuringGrace()
{
    World w;
    // 猶予中でも、実際の利用の評価で、評判が上がる(1週の上限つき)。成功率100%・需要も十分に回復。
    const float before = w.Area().reputation;
    const float after = w.Step(0, 10, 10, 20, 100.0f, 100.0f);
    CHECK(after > before);
    CHECK(after - before <= w.areas.Settings().reputationRecoverPerWeek + 1e-5f);
    CHECK(w.Area().lastPlanState == static_cast<int>(AreaPlanState::InitialGrace));
    // 成功率が低い(目的にしたのに、使えなかった人が多い)と、猶予中でも、下がる。
    World bad;
    bad.Step(0, 10, 2, 5, 100.0f, 0.0f);       // 使えなかった人が多く、需要の貢献も無い
    CHECK(bad.Area().reputation < 1.0f);
    // サンプルが少ない週(最低サンプル数 未満)は、利用の項は0。
    World few;
    few.Step(0, 4, 0, 4, 100.0f, 0.0f);
    CHECK(Near(few.Area().reputation, 1.0f) && Near(few.Area().lastUseTerm, 0.0f));
}

static void TestShortfallAfterGrace()
{
    World w;
    const CommercialAreaSettings& s = w.areas.Settings();
    for (int i = 0; i < s.graceWeeks - 1; ++i) { w.Step(2); }  // 猶予が終わる直前まで(店舗2軒 = 最低4に未達)。周知の週から数えて、猶予は graceWeeks 週
    float previous = w.Area().reputation;
    CHECK(Near(previous, 1.0f));
    bool dropped = false;
    for (int i = 0; i < 12; ++i)
    {
        const float now = w.Step(2);
        CHECK(previous - now <= s.reputationDropPerWeek + 1e-5f);   // 急に下がらない
        CHECK(now <= previous + 1e-6f);                              // 下がる方向(戻らない)
        if (now < previous - 1e-6f) { dropped = true; }
        previous = now;
    }
    CHECK(dropped);
    CHECK(Near(w.Area().reputation, 1.0f - s.planPenaltyMax * 0.5f, 1e-3f));   // 不足の割合 0.5 → 目標 0.8 へ収束
    CHECK(w.Area().lastPlanState == static_cast<int>(AreaPlanState::Shortfall));
    // 店舗数が少ないほど、目標は低い。下限は守る。
    World zero;
    for (int i = 0; i < s.graceWeeks + 30; ++i) { zero.Step(0); }
    CHECK(Near(zero.Area().reputation, 1.0f - s.planPenaltyMax, 1e-3f) && zero.Area().reputation >= s.reputationMin);
}

static void TestMeetingMinimumRemovesPenalty()
{
    World w;
    const CommercialAreaSettings& s = w.areas.Settings();
    for (int i = 0; i < s.graceWeeks + 10; ++i) { w.Step(2); }
    CHECK(w.Area().reputation < 0.9f);
    // 最低進捗を達成: 減点要因が消え、時間とともに回復する(1週の上限つき)。
    float previous = w.Area().reputation;
    for (int i = 0; i < 30; ++i)
    {
        const float now = w.Step(s.minProgressStores);
        CHECK(now >= previous - 1e-6f);
        CHECK(now - previous <= s.reputationRecoverPerWeek + 1e-5f);
        previous = now;
    }
    CHECK(Near(w.Area().reputation, 1.0f, 1e-3f));                        // 中立まで回復(計画の項は0)
    CHECK(w.Area().lastPlanState == static_cast<int>(AreaPlanState::Met));
}

static void TestReorganizeGrace()
{
    // 最低進捗を達成 → 店舗数が減少 → 再整備の猶予(減点なし)→ 猶予後に、減点。
    World w;
    const CommercialAreaSettings& s = w.areas.Settings();
    for (int i = 0; i < s.graceWeeks + 3; ++i) { w.Step(s.minProgressStores); }
    CHECK(w.Area().achieved >= AreaMilestone::Minimum);                   // 過去の最高到達(履歴)
    // 店舗が減る: 履歴(achieved)は残るが、今は未達。猶予が始まる。
    for (int i = 0; i < s.reorganizeGraceWeeks - 1; ++i)
    {
        const float r = w.Step(2);
        CHECK(Near(r, 1.0f));
        CHECK(w.Area().lastPlanState == static_cast<int>(AreaPlanState::Reorganizing));
        CHECK(w.Area().achieved >= AreaMilestone::Minimum);               // 履歴は消えない
    }
    CHECK(w.Area().belowMinimumSinceWeek > 0);
    // 猶予を過ぎても未達 → 徐々に低下。
    for (int i = 0; i < 3; ++i) { w.Step(2); }
    float previous = w.Area().reputation;
    for (int i = 0; i < 10; ++i)
    {
        const float now = w.Step(2);
        CHECK(previous - now <= s.reputationDropPerWeek + 1e-5f);
        previous = now;
    }
    CHECK(w.Area().reputation < 1.0f - 1e-3f);
    CHECK(w.Area().lastPlanState == static_cast<int>(AreaPlanState::Shortfall));
    // 履歴が最低進捗でも、「今」の評価は未達(達成済みと判定し続けない)。
    const AreaReputationResult now = EvaluateAreaReputation(w.Area(), w.week + 1, 2, AreaUsageWeek{}, s);
    CHECK(!now.metNow && now.hadMetBefore && now.planState == AreaPlanState::Shortfall);
}

static void TestRecoverInsideGrace()
{
    World w;
    const CommercialAreaSettings& s = w.areas.Settings();
    for (int i = 0; i < s.graceWeeks + 3; ++i) { w.Step(s.minProgressStores); }
    // 減って、猶予中に、回復を保つ(recoveryHoldWeeks 週以上)。減点は、一度も発生しない。
    w.Step(2); w.Step(2);
    for (int i = 0; i < 12; ++i)
    {
        CHECK(Near(w.Step(s.minProgressStores), 1.0f));
    }
    CHECK(w.Area().belowMinimumSinceWeek == 0);                           // 計測が取り消された
    CHECK(w.Area().lastPlanState == static_cast<int>(AreaPlanState::Met));
    // 再び減った時は、新しい再整備の猶予(最初から)。
    CHECK(Near(w.Step(2), 1.0f));
    CHECK(w.Area().belowMinimumSinceWeek == w.week);
}

static void TestCannotExtendGrace()
{
    // 未達のまま、1週だけ店を足して(連続が recoveryHoldWeeks 未満)、また減らしても、猶予は更新されない。
    World w;
    const CommercialAreaSettings& s = w.areas.Settings();
    for (int i = 0; i < s.graceWeeks + 3; ++i) { w.Step(s.minProgressStores); }
    w.Step(2);
    const int started = w.Area().belowMinimumSinceWeek;
    CHECK(started == w.week);
    for (int i = 0; i < s.reorganizeGraceWeeks - 1; ++i) { w.Step(2); }
    w.Step(s.minProgressStores);                                          // 1週だけ達成(連続1週 < 2週)
    CHECK(w.Area().belowMinimumSinceWeek == started);                     // 計測は、取り消されない
    CHECK(Near(w.Area().reputation, 1.0f));                               // その週は、減点なし(今は達成)
    w.Step(2);                                                            // また減らす
    CHECK(w.Area().belowMinimumSinceWeek == started);                     // 猶予は、更新されていない
    CHECK(w.Area().lastPlanState == static_cast<int>(AreaPlanState::Shortfall));   // 猶予は過ぎているので、減点が始まる
}

static void TestRecoveryAfterDecline()
{
    World w;
    const CommercialAreaSettings& s = w.areas.Settings();
    for (int i = 0; i < s.graceWeeks + 20; ++i) { w.Step(1); }
    const float low = w.Area().reputation;
    CHECK(low < 0.8f);
    // 改善(店を足す + 住民が使えた)で、回復する。
    for (int i = 0; i < 40; ++i) { w.Step(s.minProgressStores, 10, 10, 20, 100.0f, 80.0f); }
    CHECK(w.Area().reputation > low + 0.15f);
    CHECK(w.Area().reputation >= 1.0f);
}

static void TestUseTermBounded()
{
    // 成功率100%・需要の貢献も大きい状態が続いても、評判は上がり続けない(目標に収束。上限を超えない)。
    World w;
    const CommercialAreaSettings& s = w.areas.Settings();
    float last = 0.0f;
    for (int i = 0; i < 80; ++i) { last = w.Step(8, 20, 20, 40, 200.0f, 200.0f); }
    const float expected = 1.0f + s.planCompleteBonus + s.useSuccessWeight + s.useNeedWeight;    // 8軒 = 計画規模 → 加点あり
    CHECK(Near(last, std::fmin(expected, s.reputationMax), 1e-3f));
    CHECK(Near(w.Step(8, 20, 20, 40, 200.0f, 200.0f), last, 1e-5f));                           // これ以上、上がらない
    CHECK(last <= s.reputationMax + 1e-6f);

    // 成功率だけが100%(需要の貢献は無い: 需要が満たされている)でも、小さな加点で頭打ち。減点にはならない。
    World satisfied;
    float r = 1.0f;
    for (int i = 0; i < 60; ++i) { r = satisfied.Step(s.minProgressStores, 20, 20, 40, 200.0f, 0.0f); }
    CHECK(r > 1.0f && r <= 1.0f + s.useSuccessWeight + 1e-3f);
    CHECK(Near(satisfied.Area().lastUseTerm, s.useSuccessWeight, 1e-4f));

    // 需要への貢献は、満たされていても減点しない。標準以上に回復しても、上限がある。
    const AreaUsageWeek none;
    AreaUsageWeek over; over.visits = 30; over.needNominal = 100.0f; over.needActual = 500.0f;
    CommercialArea area; area.published = true; area.publishedWeek = 1;
    const AreaReputationResult overResult = EvaluateAreaReputation(area, 5, 4, over, s);
    CHECK(overResult.needMeasured && overResult.needEffect <= 1.0f && overResult.needTerm <= s.useNeedWeight + 1e-6f);
    AreaUsageWeek zero; zero.visits = 30; zero.needNominal = 100.0f; zero.needActual = 0.0f;
    CHECK(EvaluateAreaReputation(area, 5, 4, zero, s).needTerm == 0.0f);
    (void)none;
}

static void TestSaveLoad()
{
    World w;
    const CommercialAreaSettings& s = w.areas.Settings();
    for (int i = 0; i < s.graceWeeks + 3; ++i) { w.Step(s.minProgressStores, 10, 9, 18, 100.0f, 60.0f); }
    for (int i = 0; i < s.reorganizeGraceWeeks + 4; ++i) { w.Step(2, 10, 9, 18, 100.0f, 60.0f); }   // 下回って、猶予を過ぎて、減点中
    const CommercialArea before = w.Area();
    CHECK(before.reputation < 1.0f && before.belowMinimumSinceWeek > 0 && before.achieved >= AreaMilestone::Minimum);

    Json section = Json::MakeObject();
    SaveContext sc;
    w.areas.Save(section, sc);
    Json parsed; std::string error;
    CHECK(Json::Parse(section.Dump(true), parsed, error));
    CommercialAreaManager loaded;
    LoadContext lc;
    loaded.Load(parsed, 1, lc);
    const CommercialArea* a = loaded.Find(w.id);
    CHECK(a != nullptr);
    if (a)
    {
        CHECK(Near(a->reputation, before.reputation) && a->belowMinimumSinceWeek == before.belowMinimumSinceWeek);
        CHECK(a->metMinimumStreakWeeks == before.metMinimumStreakWeeks && a->achieved == before.achieved);
        CHECK(Near(a->lastReputationTarget, before.lastReputationTarget) && Near(a->lastPlanTerm, before.lastPlanTerm) && Near(a->lastUseTerm, before.lastUseTerm));
        CHECK(a->lastPlanState == before.lastPlanState && a->publishedWeek == before.publishedWeek);
        // 読み込み後も、同じ続きになる(同じ入力で、同じ評判)。
        CommercialAreaManager& live = w.areas;
        AreaLedger ledger2;
        live.UpdateReputations(w.week + 1, { { w.id, 2 } }, ledger2);
        loaded.UpdateReputations(w.week + 1, { { w.id, 2 } }, ledger2);
        CHECK(Near(live.Find(w.id)->reputation, loaded.Find(w.id)->reputation));
    }

    // 古いセーブ(評判の項目が無い)は、中立の1.0・計測なしで読める。
    Json old = Json::MakeObject();
    Json areas = Json::MakeArray();
    Json item = Json::MakeObject();
    item.Set("id", 1); item.Set("minX", 0.0f); item.Set("minZ", 0.0f); item.Set("maxX", 8.0f); item.Set("maxZ", 8.0f);
    item.Set("published", true); item.Set("publishedWeek", 3);
    areas.Push(std::move(item));
    old.Set("areas", std::move(areas));
    CommercialAreaManager fromOld;
    fromOld.Load(old, 1, lc);
    CHECK(fromOld.Find(1) && Near(fromOld.Find(1)->reputation, 1.0f) && fromOld.Find(1)->belowMinimumSinceWeek == 0);
}

static void TestMoveAndWithdraw()
{
    World w;
    const CommercialAreaSettings& s = w.areas.Settings();
    for (int i = 0; i < s.graceWeeks + 10; ++i) { w.Step(2); }
    const float rep = w.Area().reputation;
    CHECK(rep < 1.0f);
    // 移設しても、評判・履歴・計測は消えない。
    CHECK(w.areas.MoveArea(w.id, 100.0f, 100.0f, 0.0f, w.week));
    CHECK(Near(w.Area().reputation, rep) && w.Area().history.size() >= 2);
    // 撤回すると、撤回の記録に、評判が残る。
    CHECK(w.areas.DeleteArea(w.id, w.week));
    CHECK(w.areas.Withdrawn().size() == 1 && Near(w.areas.Withdrawn()[0].reputation, rep));
}

// ---- 利用成功率: 分子・分母が、同じ集団・同じ週(出発の週と利用の週がずれても、0〜1に収まる) ----
static void TestSuccessRateSameCohort()
{
    AreaLedger ledger;
    // 第1週: 10人が、エリアを目的に出発(areaOutings)。まだ、結果は決まっていない。
    for (int i = 0; i < 10; ++i) { ledger.RecordAreaOuting(1); }
    ledger.OnWeekEnd(false);
    // 第2週: 先週出発した10人のうち、8人が使えた(成功)、2人は使えずに外出が終わった(失敗)。結果は、決まった週(第2週)に、分子・分母を一緒に数える。
    for (int i = 0; i < 8; ++i) { ledger.RecordAreaOutingResult(1, true); }
    for (int i = 0; i < 2; ++i) { ledger.RecordAreaOutingResult(1, false); }
    ledger.OnWeekEnd(false);
    const AreaRecord* r = ledger.Find(1);
    CHECK(r->lastWeek.areaOutings == 0);                                  // 第2週は、出発した人は、いない(旧方式では、分子 > 分母になり得た週)
    CHECK(r->lastWeek.areaResolved == 10 && r->lastWeek.areaSucceeded == 8);
    CHECK(Near(static_cast<float>(r->lastWeek.areaSucceeded) / static_cast<float>(r->lastWeek.areaResolved), 0.8f));
    // 成功数は、決まった数を超えない(どの期間でも)。
    for (int i = 0; i < 50; ++i) { ledger.RecordAreaOutingResult(1, (i % 3) != 0); }
    for (const AreaTally* t : { &r->thisWeek, &r->lastWeek, &r->thisMonth, &r->total }) { CHECK(t->areaSucceeded <= t->areaResolved); }
    CHECK(ledger.Find(1)->total.areaResolved == 60 && ledger.Find(1)->total.areaSucceeded <= 60);

    // 評判の計算: 成功率は、切り詰めず、そのまま使う。0〜1。成功数 > 決まった数(集計の不整合)は、測れていない扱い。
    CommercialAreaSettings s;
    CommercialArea area; area.published = true; area.publishedWeek = 1;
    AreaUsageWeek u; u.resolvedOutings = 10; u.succeededOutings = 8;
    AreaReputationResult ok = EvaluateAreaReputation(area, 5, 4, u, s);
    CHECK(ok.successMeasured && Near(ok.successRate, 0.8f) && ok.successRate <= 1.0f);
    CHECK(ok.successTerm < 0.0f);                                          // 0.85 を下回る → わずかな減点
    u.succeededOutings = 12;                                               // 不整合(1.2)
    AreaReputationResult bad = EvaluateAreaReputation(area, 5, 4, u, s);
    CHECK(!bad.successMeasured && bad.successTerm == 0.0f);               // 1.0に切り詰めて、隠さない
    AreaIndicators ind = ComputeAreaIndicators(AreaComposition{}, true, [&]() { static AreaRecord rec; rec.total.areaResolved = 3; rec.total.areaSucceeded = 5; return &rec; }(), ResidentSimSettings{}, 1.0f);
    CHECK(!ind.consistent);                                                // 台帳の不整合は、検出できる
}

// ---- 計画規模と最低進捗の整合性 / 完成ボーナス ----
static void TestThresholdsAndBonus()
{
    CommercialAreaSettings s;               // 既定: 計画規模 8 / 最低進捗 4 / 来訪範囲の拡大 6
    // 8店舗の計画で、4店舗は最低進捗の達成、8店舗で完成。
    CHECK(ComputeAreaProgress(3, 8, s).current == AreaMilestone::None);
    CHECK(ComputeAreaProgress(4, 8, s).current == AreaMilestone::Minimum);
    CHECK(ComputeAreaProgress(6, 8, s).current == AreaMilestone::Expanded);
    CHECK(ComputeAreaProgress(7, 8, s).current == AreaMilestone::Expanded);
    CHECK(ComputeAreaProgress(8, 8, s).current == AreaMilestone::Complete);
    // 完成ボーナスは、完成した時だけ。最低進捗に届いただけでは、発生しない。
    CommercialArea area; area.published = true; area.publishedWeek = 1; area.plannedCapacity = 8;
    const AreaUsageWeek none;
    CHECK(EvaluateAreaReputation(area, 20, 4, none, s).planTerm == 0.0f);
    CHECK(EvaluateAreaReputation(area, 20, 7, none, s).planTerm == 0.0f);
    CHECK(Near(EvaluateAreaReputation(area, 20, 8, none, s).planTerm, s.planCompleteBonus));
    CHECK(EvaluateAreaReputation(area, 20, 4, none, s).planState == AreaPlanState::Met);

    // 計画規模が最低進捗より小さい設定でも、矛盾しない: 最低進捗を満たさずに、完成にならない。完成ボーナスも、最低進捗だけでは出ない。
    CommercialAreaSettings conflict;
    conflict.plannedCapacity = 2; conflict.minProgressStores = 4; conflict.expandedRangeStores = 1;
    CHECK(DescribeAreaSettingsConflict(conflict) != nullptr);
    CHECK(DescribeAreaSettingsConflict(s) == nullptr);
    CHECK(ComputeAreaProgress(2, 2, conflict).current == AreaMilestone::None);       // 計画規模(2)に届いても、最低進捗(4)に届かなければ、未達成
    CHECK(ComputeAreaProgress(3, 2, conflict).current == AreaMilestone::None);
    CHECK(ComputeAreaProgress(4, 2, conflict).current == AreaMilestone::Complete);   // 最低進捗以上で、初めて、段階が付く
    const AreaThresholds t = ComputeAreaThresholds(2, conflict);
    CHECK(t.minStores == 4 && t.completeStores == 4 && t.expandedStores >= t.minStores && t.expandedStores <= t.completeStores);
    CommercialArea small; small.published = true; small.publishedWeek = 1; small.plannedCapacity = 2;
    CHECK(EvaluateAreaReputation(small, 20, 2, none, conflict).planState == AreaPlanState::Shortfall);   // 2店舗は、最低進捗(4)未満
    CHECK(EvaluateAreaReputation(small, 20, 2, none, conflict).planTerm < 0.0f);
    CHECK(EvaluateAreaReputation(small, 20, 4, none, conflict).planTerm == 0.0f);                       // 最低進捗だけでは、ボーナスなし(計画の完成と区別できない設定)
    // 来訪範囲の拡大が、範囲外でも、順序が破綻しない。
    CommercialAreaSettings expanded; expanded.expandedRangeStores = 20;
    const AreaThresholds e = ComputeAreaThresholds(8, expanded);
    CHECK(e.minStores <= e.expandedStores && e.expandedStores <= e.completeStores);
    CHECK(ComputeAreaProgress(5, 8, expanded).current == AreaMilestone::Minimum);

    // 周知した時の計画規模は、最低進捗を下回らない。
    CommercialAreaManager manager;
    manager.Settings().plannedCapacity = 2;
    const uint32_t id = manager.PlaceArea(0.0f, 0.0f, 0.0f);
    manager.Publish(id, 1);
    CHECK(manager.Find(id)->plannedCapacity >= manager.Settings().minProgressStores);
}

int main()
{
    TestUnpublished();
    TestInitialGrace();
    TestUseDuringGrace();
    TestShortfallAfterGrace();
    TestMeetingMinimumRemovesPenalty();
    TestReorganizeGrace();
    TestRecoverInsideGrace();
    TestCannotExtendGrace();
    TestRecoveryAfterDecline();
    TestUseTermBounded();
    TestSaveLoad();
    TestMoveAndWithdraw();
    TestSuccessRateSameCohort();
    TestThresholdsAndBonus();
    if (g_fail == 0) { std::printf("ALL PASSED\n"); return 0; }
    std::printf("%d FAILED\n", g_fail);
    return 1;
}
