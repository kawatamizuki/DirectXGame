// 段階4: 行動統計(SimStats)の単体テスト。
#include <cmath>
#include <cstdio>
#include "SimStats.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)
static bool Near(double a, double b, double eps = 1e-3) { return std::fabs(a - b) < eps; }

static SimEvent Ev(SimEventKind kind, SimEventResult result, SimReason reason, int stopIndex = 0, uint32_t facility = 0, int week = 1, uint32_t resident = 1, uint32_t testId = 0)
{
    SimEvent e; e.kind = kind; e.result = result; e.reason = reason; e.stopIndex = stopIndex; e.facilityId = facility; e.week = week; e.residentId = resident; e.testId = testId;
    return e;
}

int main()
{
    // ---- Wilson ----
    {
        double lo = 0, hi = 0;
        SimWilsonInterval(50, 100, lo, hi);
        CHECK(Near(lo, 0.4038, 2e-3) && Near(hi, 0.5962, 2e-3));
        SimWilsonInterval(0, 0, lo, hi);
        CHECK(lo == 0.0 && hi == 1.0);
        SimWilsonInterval(0, 10, lo, hi);
        CHECK(lo == 0.0 && hi > 0.2 && hi < 0.4);
        SimWilsonInterval(10, 10, lo, hi);
        CHECK(hi == 1.0 && lo > 0.6);
        SimWilsonInterval(20, 10, lo, hi);                       // 成功が試行を超えても壊れない
        CHECK(hi <= 1.0 && lo >= 0.0);
    }
    // ---- SimRollTally ----
    {
        SimRollTally t;
        t.Add(0.25f, true); t.Add(0.25f, false); t.Add(0.5f, false); t.Add(1.5f, true); // 確率は0〜1に収める
        CHECK(t.trials == 4 && t.hits == 2);
        CHECK(Near(t.ExpectedHits(), 0.25 + 0.25 + 0.5 + 1.0));
        CHECK(Near(t.sumVariance, 0.1875 + 0.1875 + 0.25 + 0.0));
        CHECK(Near(t.StdDev(), std::sqrt(0.625)));
    }
    // ---- 外出判定は、通常の範囲だけ ----
    {
        SimStats s;
        s.OnOutingRoll(1, OutingChanceBranch::NeedDriven, 0.2f, false);
        s.OnOutingRoll(1, OutingChanceBranch::NeedDriven, 0.2f, true);
        s.OnOutingRoll(1, OutingChanceBranch::Spontaneous, 0.1f, false);
        s.OnOutingRoll(1, OutingChanceBranch::SpontaneousLowStamina, 0.0f, false);
        const auto& n = s.Scope(SimStatScope::Normal);
        CHECK(n.total.outingRollNeed.trials == 2 && n.total.outingRollNeed.hits == 1);
        CHECK(n.total.outingRollSpontaneous.trials == 1 && n.total.outingRollLowStamina.trials == 1);
        CHECK(n.thisWeek.outingRollNeed.trials == 2);
        CHECK(s.Scope(SimStatScope::Test).total.outingRollNeed.trials == 0);
        CHECK(s.StartWeek() == 1 && s.CurrentWeek() == 1);
    }
    // ---- 外出の流れ・利用・施設・住民 ----
    {
        SimStats s;
        s.OnEvent(Ev(SimEventKind::OutingRoll, SimEventResult::Success, SimReason::OutingNeedDriven));
        s.OnEvent(Ev(SimEventKind::PurposeChoice, SimEventResult::Info, SimReason::None));
        s.OnEvent(Ev(SimEventKind::FacilitySelect, SimEventResult::Success, SimReason::None, 0, 15));
        s.OnEvent(Ev(SimEventKind::MoveStart, SimEventResult::Info, SimReason::None, 0, 15));
        s.OnEvent(Ev(SimEventKind::Arrived, SimEventResult::Info, SimReason::None, 0, 15));
        SimEvent v = Ev(SimEventKind::VisitFinished, SimEventResult::Success, SimReason::None, 0, 15); v.a = 120; v.b = 30; v.c = 4;
        s.OnEvent(v);
        s.OnEvent(Ev(SimEventKind::ReturnHome, SimEventResult::Success, SimReason::None));
        const auto& n = s.Scope(SimStatScope::Normal);
        CHECK(n.total.outingStarted == 1 && n.total.purposeChosen == 1 && n.total.mainSelectOk == 1);
        CHECK(n.total.departed == 1 && n.total.arrived == 1 && n.total.mainVisits == 1 && n.total.returnedHome == 1);
        CHECK(n.total.visits == 1 && Near(n.total.revenue, 120) && Near(n.total.lifeRecovered, 30) && Near(n.total.staminaUsed, 4));
        const auto& f = n.facilities.at(15);
        CHECK(f.chosen == 1 && f.chosenMain == 1 && f.arrivals == 1 && f.visits == 1 && Near(f.revenue, 120));
        const auto& r = n.residents.at(1);
        CHECK(r.outings == 1 && r.visits == 1 && Near(r.revenue, 120) && r.firstWeek == 1);

        // 行き先の変更(テスト)の MoveStart は、出発に数えない。立ち寄りの MoveStart/Arrived も、主目的の数に入らない。
        s.OnEvent(Ev(SimEventKind::MoveStart, SimEventResult::Info, SimReason::TestApplied, 0, 16));
        s.OnEvent(Ev(SimEventKind::MoveStart, SimEventResult::Info, SimReason::StopStarted, 1, 16));
        s.OnEvent(Ev(SimEventKind::Arrived, SimEventResult::Info, SimReason::None, 1, 16));
        CHECK(n.total.departed == 1 && n.total.arrived == 1);
        CHECK(n.facilities.at(16).arrivals == 1);
        // 施設が無くなっていた到着(Warning)は、数えない。
        s.OnEvent(Ev(SimEventKind::Arrived, SimEventResult::Warning, SimReason::FacilityMissingOnArrival, 0, 16));
        CHECK(n.facilities.at(16).arrivals == 1 && n.total.arrived == 1);
    }
    // ---- 外出の不成立の理由(排他) ----
    {
        SimStats s;
        s.OnEvent(Ev(SimEventKind::OutingRoll, SimEventResult::Success, SimReason::OutingNeedDriven));
        s.OnEvent(Ev(SimEventKind::OutingBlocked, SimEventResult::NotMet, SimReason::HomeNoRoad));
        s.OnEvent(Ev(SimEventKind::OutingRoll, SimEventResult::Success, SimReason::OutingNeedDriven));
        s.OnEvent(Ev(SimEventKind::FacilitySelect, SimEventResult::NotMet, SimReason::NoPickableCandidate, 0, 0));
        const auto& t = s.Scope(SimStatScope::Normal).total;
        CHECK(t.outingStarted == 2 && t.outingFailed == 2);
        CHECK(t.outingFailReason[static_cast<size_t>(SimReason::HomeNoRoad)] == 1);
        CHECK(t.outingFailReason[static_cast<size_t>(SimReason::NoPickableCandidate)] == 1);
        CHECK(t.mainSelectOk == 0);
    }
    // ---- 立ち寄り: 1回の判定が、必ず、どれか1つの結果に数えられる ----
    {
        SimStats s;
        auto check = [&](SimReason r, float chance = 0.0f) { SimEvent e = Ev(SimEventKind::StopoverCheck, r == SimReason::StopRollHit ? SimEventResult::Success : SimEventResult::NotMet, r, 1, 10); e.a = chance; s.OnEvent(e); };
        check(SimReason::StopTooLate);                       // 条件で考えなかった
        check(SimReason::StopMaxReached);
        check(SimReason::StopRollMiss, 0.25f);               // 抽選に外れた
        check(SimReason::StopRollHit, 0.25f);                // 当選 → 候補が無い
        check(SimReason::StopNoCandidates);
        check(SimReason::StopRollHit, 0.25f);                // 当選 → 全て除外(FacilitySelect で記録)
        s.OnEvent(Ev(SimEventKind::FacilitySelect, SimEventResult::NotMet, SimReason::StopNoPickable, 1));
        check(SimReason::StopRollHit, 0.25f);                // 当選 → 寄った
        s.OnEvent(Ev(SimEventKind::FacilitySelect, SimEventResult::Success, SimReason::StopStarted, 1, 12));
        check(SimReason::StopRollHit, 0.25f);                // 当選 → 食事系で取りやめ(FacilitySelect は成功扱いで先に記録される)
        s.OnEvent(Ev(SimEventKind::FacilitySelect, SimEventResult::Success, SimReason::None, 1, 12));
        check(SimReason::StopFoodChain);
        const auto& t = s.Scope(SimStatScope::Normal).total;
        CHECK(t.stopConsidered == 7);                        // 判定は7回(条件で2・外れ1・当選4)
        CHECK(t.stopRoll.trials == 5 && t.stopRoll.hits == 4);
        CHECK(Near(t.stopRoll.ExpectedHits(), 1.25));
        auto r = [&](SimReason x) { return t.stopReason[static_cast<size_t>(x)]; };
        const uint64_t terminals = r(SimReason::StopNoHome) + r(SimReason::StopMaxReached) + r(SimReason::StopTooLate) + r(SimReason::StopLowStamina) + r(SimReason::StopRollMiss)
            + r(SimReason::StopHomeMissing) + r(SimReason::StopNoRoad) + r(SimReason::StopNoCandidates) + r(SimReason::StopNoPickable) + r(SimReason::StopFoodChain) + r(SimReason::StopStarted);
        CHECK(terminals == t.stopConsidered);
        CHECK(r(SimReason::StopRollHit) == 4);               // 「当選」は通過点(最終結果ではない)
        CHECK(t.stopStarted == 1 && s.Scope(SimStatScope::Normal).facilities.at(12).chosenStop == 1); // 食事系で取りやめた施設は、選ばれた数に入らない
        CHECK(s.Scope(SimStatScope::Normal).facilities.at(12).chosen == 1);
        CHECK(s.Scope(SimStatScope::Normal).residents.at(1).stopovers == 1);
    }
    // ---- テストの範囲を分ける ----
    {
        SimStats s;
        s.OnEvent(Ev(SimEventKind::OutingRoll, SimEventResult::Success, SimReason::OutingNeedDriven, 0, 0, 1, 1, 0));
        s.OnEvent(Ev(SimEventKind::OutingRoll, SimEventResult::Success, SimReason::OutingNeedDriven, 0, 0, 1, 2, 7));
        s.OnEvent(Ev(SimEventKind::TestCommand, SimEventResult::Success, SimReason::TestApplied, 0, 0, 1, 2, 7));
        s.OnEvent(Ev(SimEventKind::TestCommand, SimEventResult::NotMet, SimReason::TestRejected, 0, 0, 1, 3, 8));
        SimEvent v = Ev(SimEventKind::VisitFinished, SimEventResult::Success, SimReason::None, 0, 5, 1, 2, 7); v.a = 500; s.OnEvent(v);
        CHECK(s.Scope(SimStatScope::Normal).total.outingStarted == 1);
        CHECK(s.Scope(SimStatScope::Test).total.outingStarted == 1);
        CHECK(s.Scope(SimStatScope::Test).total.testApplied == 1 && s.Scope(SimStatScope::Test).total.testRejected == 1);
        CHECK(Near(s.Scope(SimStatScope::Test).total.revenue, 500) && Near(s.Scope(SimStatScope::Normal).total.revenue, 0));
        CHECK(s.Scope(SimStatScope::Normal).facilities.count(5) == 0 && s.Scope(SimStatScope::Test).facilities.count(5) == 1);
        // 判断の内訳も、テストの範囲へ。
        SimDecisionDetail d; d.week = 1; d.testId = 7;
        CandidateEvaluation e1; e1.candidate.objectId = 5; e1.reject = CandidateReject::None;
        CandidateEvaluation e2; e2.candidate.objectId = 6; e2.reject = CandidateReject::NotEnoughStamina;
        d.evaluations = { e1, e2 };
        s.OnDetail(d);
        CHECK(s.Scope(SimStatScope::Test).facilities.at(6).rejected[static_cast<size_t>(CandidateReject::NotEnoughStamina)] == 1);
        CHECK(s.Scope(SimStatScope::Test).facilities.at(5).candidateIn == 1);
        CHECK(s.Scope(SimStatScope::Normal).facilities.count(6) == 0);
        SimDecisionDetail empty; empty.week = 1;                  // 候補が無い判断は、数えない
        const uint64_t before = s.TotalCounted();
        s.OnDetail(empty);
        CHECK(s.TotalCounted() == before);
    }
    // ---- 週 ----
    {
        SimStats s;
        s.OnEvent(Ev(SimEventKind::OutingRoll, SimEventResult::Success, SimReason::None, 0, 0, 3));
        s.OnEvent(Ev(SimEventKind::OutingRoll, SimEventResult::Success, SimReason::None, 0, 0, 3));
        CHECK(s.StartWeek() == 3 && s.CurrentWeek() == 3);
        s.OnEvent(Ev(SimEventKind::OutingRoll, SimEventResult::Success, SimReason::None, 0, 0, 4));
        const auto& n = s.Scope(SimStatScope::Normal);
        CHECK(n.total.outingStarted == 3 && n.thisWeek.outingStarted == 1 && n.lastWeek.outingStarted == 2 && n.hasLastWeek);
        CHECK(n.weekly.size() == 1 && n.weekly[0].week == 3 && n.weekly[0].bucket.outingStarted == 2);
        s.OnEvent(Ev(SimEventKind::OutingRoll, SimEventResult::Success, SimReason::None, 0, 0, 3));   // 古い週のイベントは、今週に数える(戻らない)
        CHECK(s.CurrentWeek() == 4 && n.thisWeek.outingStarted == 2);
        s.OnEvent(Ev(SimEventKind::OutingRoll, SimEventResult::Success, SimReason::None, 0, 0, 7));   // 週が飛んでも、1行
        CHECK(n.weekly.size() == 2 && n.weekly[1].week == 4 && n.lastWeek.outingStarted == 2 && n.thisWeek.outingStarted == 1);
        // 週の推移の上限。
        for (int w = 8; w < 8 + static_cast<int>(kSimStatsMaxWeeks) + 10; ++w) { s.OnEvent(Ev(SimEventKind::OutingRoll, SimEventResult::Success, SimReason::None, 0, 0, w)); }
        CHECK(n.weekly.size() == kSimStatsMaxWeeks);
        s.Reset();
        CHECK(s.StartWeek() == -1 && n.total.outingStarted == 0 && n.weekly.empty() && n.residents.empty() && s.TotalCounted() == 0);
    }
    // ---- 中断・立ち往生・住居なし ----
    {
        SimStats s;
        s.OnEvent(Ev(SimEventKind::OutingAborted, SimEventResult::Warning, SimReason::FacilityDemolished, 0, 3));
        s.OnEvent(Ev(SimEventKind::BecameStranded, SimEventResult::Warning, SimReason::NoRouteHome));
        s.OnEvent(Ev(SimEventKind::StrandedRecovered, SimEventResult::Info, SimReason::RoadReconnected));
        s.OnEvent(Ev(SimEventKind::BecameHomeless, SimEventResult::Warning, SimReason::None));
        s.OnEvent(Ev(SimEventKind::ReturnRerouted, SimEventResult::Info, SimReason::DetourReturn));
        const auto& t = s.Scope(SimStatScope::Normal).total;
        CHECK(t.aborted == 1 && t.stranded == 1 && t.strandedRecovered == 1 && t.homeless == 1 && t.rerouted == 1);
        const auto& r = s.Scope(SimStatScope::Normal).residents.at(1);
        CHECK(r.aborted == 1 && r.stranded == 1 && r.homeless == 1);
    }
    // ---- 記録部との連携(観察者) ----
    {
        SimDebugRecorder rec;
        SimStats s;
        rec.AddObserver(&s);
        SimEvent e = Ev(SimEventKind::OutingRoll, SimEventResult::Success, SimReason::OutingNeedDriven);
        rec.AddEvent(e);                                          // OFFの間は、数えない
        CHECK(s.Scope(SimStatScope::Normal).total.outingStarted == 0);
        rec.SetEnabled(true);
        rec.AddEvent(e);
        CHECK(s.Scope(SimStatScope::Normal).total.outingStarted == 1);
        rec.SetActiveTest(1, 9);                                  // テスト中の住民のイベントは、テストの範囲へ
        rec.AddEvent(e);
        CHECK(s.Scope(SimStatScope::Test).total.outingStarted == 1 && s.Scope(SimStatScope::Normal).total.outingStarted == 1);
        rec.NoteOutingRoll(1, OutingChanceBranch::NeedDriven, 0.3f, 0.2f, true, 2, 8.0f);
        CHECK(s.Scope(SimStatScope::Normal).total.outingRollNeed.trials == 1);
        SimDecisionDetail d; d.residentId = 1; d.week = 1; CandidateEvaluation c; c.candidate.objectId = 4; d.evaluations = { c };
        rec.AddDetail(d);
        CHECK(s.Scope(SimStatScope::Test).facilities.at(4).candidateIn == 1);
        rec.StartNewRun();                                        // 新しい世界で消える
        CHECK(s.Scope(SimStatScope::Normal).total.outingStarted == 0 && s.StartWeek() == -1);
        rec.RemoveObserver(&s);
        rec.AddEvent(e);                                          // 観察者が無くても動く
        CHECK(s.Scope(SimStatScope::Normal).total.outingStarted == 0);
    }
    std::printf(g_fail == 0 ? "ALL PASS\n" : "%d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}

