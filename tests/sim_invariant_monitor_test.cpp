// 段階6: 外出の流れの不変条件の監視(SimInvariantMonitor)の単体テスト。
#include <cstdio>
#include "SimInvariantMonitor.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)

static SimEvent Ev(SimEventKind kind, uint32_t outing = 1, uint32_t resident = 1, uint32_t facility = 0, int stopIndex = 0,
                   SimEventResult result = SimEventResult::Info, SimReason reason = SimReason::None, float a = 0.0f, uint32_t testId = 0)
{
    SimEvent e; e.kind = kind; e.outingId = outing; e.residentId = resident; e.facilityId = facility; e.stopIndex = stopIndex; e.result = result; e.reason = reason; e.a = a; e.testId = testId;
    return e;
}

static std::vector<SimFlowViolation> Violations(SimInvariantMonitor& m)
{
    std::vector<SimFlowViolation> v; std::vector<std::pair<uint32_t, uint32_t>> s;
    m.Drain(v, s);
    return v;
}

// 正常な1回の外出: 外出判定 → 歩き始め → 到着 → 利用完了 → 帰宅開始 → 帰宅
static void Normal(SimInvariantMonitor& m, uint32_t outing, uint32_t resident = 1, uint32_t facility = 10)
{
    m.OnEvent(Ev(SimEventKind::OutingRoll, outing, resident));
    m.OnEvent(Ev(SimEventKind::MoveStart, outing, resident, facility));
    m.OnEvent(Ev(SimEventKind::Arrived, outing, resident, facility));
    m.OnEvent(Ev(SimEventKind::VisitFinished, outing, resident, facility, 0, SimEventResult::Success, SimReason::None, 120.0f));
    m.OnEvent(Ev(SimEventKind::ReturnStart, outing, resident));
    m.OnEvent(Ev(SimEventKind::ReturnHome, outing, resident));
}

int main()
{
    // ---- 正常な流れは、違反なし(立ち寄り2回を含む) ----
    {
        SimInvariantMonitor m;
        m.SetMaxStopovers(2);
        Normal(m, 1);
        Normal(m, 2, 2, 11);
        // 立ち寄り2回: 主目的 → 立ち寄り1 → 立ち寄り2 → 帰宅
        m.OnEvent(Ev(SimEventKind::OutingRoll, 3));
        m.OnEvent(Ev(SimEventKind::MoveStart, 3, 1, 10));
        m.OnEvent(Ev(SimEventKind::Arrived, 3, 1, 10));
        m.OnEvent(Ev(SimEventKind::VisitFinished, 3, 1, 10, 0, SimEventResult::Success, SimReason::None, 100.0f));
        m.OnEvent(Ev(SimEventKind::FacilitySelect, 3, 1, 11, 1, SimEventResult::Success, SimReason::StopStarted));
        m.OnEvent(Ev(SimEventKind::MoveStart, 3, 1, 11, 1));
        m.OnEvent(Ev(SimEventKind::Arrived, 3, 1, 11, 1));
        m.OnEvent(Ev(SimEventKind::VisitFinished, 3, 1, 11, 1, SimEventResult::Success, SimReason::None, 80.0f));
        m.OnEvent(Ev(SimEventKind::FacilitySelect, 3, 1, 12, 2, SimEventResult::Success, SimReason::StopStarted));
        m.OnEvent(Ev(SimEventKind::MoveStart, 3, 1, 12, 2));
        m.OnEvent(Ev(SimEventKind::Arrived, 3, 1, 12, 2));
        m.OnEvent(Ev(SimEventKind::VisitFinished, 3, 1, 12, 2, SimEventResult::Success, SimReason::None, 60.0f));
        m.OnEvent(Ev(SimEventKind::ReturnStart, 3));
        m.OnEvent(Ev(SimEventKind::ReturnHome, 3));
        CHECK(Violations(m).empty());
        CHECK(m.TotalViolations() == 0);
    }
    // ---- 売上を確定した(住民, 施設)が、取り出せる ----
    {
        SimInvariantMonitor m;
        Normal(m, 1, 5, 42);
        std::vector<SimFlowViolation> v; std::vector<std::pair<uint32_t, uint32_t>> s;
        m.Drain(v, s);
        CHECK(v.empty() && s.size() == 1 && s[0].first == 5 && s[0].second == 42);
        m.Drain(v, s);
        CHECK(v.empty() && s.empty());           // 取り出すと、空になる
    }
    // ---- 到着の前に、歩き始めていない ----
    {
        SimInvariantMonitor m;
        m.OnEvent(Ev(SimEventKind::OutingRoll));
        m.OnEvent(Ev(SimEventKind::Arrived, 1, 1, 10));
        const auto v = Violations(m);
        CHECK(v.size() == 1 && v[0].kind == SimAnomaly::ArrivedWithoutMove && v[0].residentId == 1);
    }
    // ---- 到着の前に、売上 / 1回の到着で、売上が2回 ----
    {
        SimInvariantMonitor m;
        m.OnEvent(Ev(SimEventKind::OutingRoll));
        m.OnEvent(Ev(SimEventKind::VisitFinished, 1, 1, 10, 0, SimEventResult::Success, SimReason::None, 50.0f));   // 到着していない
        auto v = Violations(m);
        CHECK(v.size() == 1 && v[0].kind == SimAnomaly::SettleWithoutArrival);

        SimInvariantMonitor m2;
        m2.OnEvent(Ev(SimEventKind::OutingRoll));
        m2.OnEvent(Ev(SimEventKind::MoveStart, 1, 1, 10));
        m2.OnEvent(Ev(SimEventKind::Arrived, 1, 1, 10));
        m2.OnEvent(Ev(SimEventKind::VisitFinished, 1, 1, 10, 0, SimEventResult::Success, SimReason::None, 50.0f));
        m2.OnEvent(Ev(SimEventKind::VisitFinished, 1, 1, 11, 0, SimEventResult::Success, SimReason::None, 50.0f));  // 2回目の売上(別の施設でも)
        v = Violations(m2);
        CHECK(v.size() == 1 && v[0].kind == SimAnomaly::SettleWithoutArrival);
    }
    // ---- 同じ外出で、同じ施設を、2回 ----
    {
        SimInvariantMonitor m;
        m.OnEvent(Ev(SimEventKind::OutingRoll));
        for (int i = 0; i < 2; ++i)
        {
            m.OnEvent(Ev(SimEventKind::MoveStart, 1, 1, 10, i));
            m.OnEvent(Ev(SimEventKind::Arrived, 1, 1, 10, i));
            m.OnEvent(Ev(SimEventKind::VisitFinished, 1, 1, 10, i, SimEventResult::Success, SimReason::None, 50.0f));
        }
        const auto v = Violations(m);
        CHECK(v.size() == 1 && v[0].kind == SimAnomaly::DuplicateFacilityInOuting && v[0].facilityId == 10);
        // 別の外出なら、同じ施設を使ってよい。
        SimInvariantMonitor m2;
        Normal(m2, 1); Normal(m2, 2);
        CHECK(Violations(m2).empty());
    }
    // ---- 利用の途中で、歩き出す / 帰宅する(行動テストの強制は、除く) ----
    {
        SimInvariantMonitor m;
        m.OnEvent(Ev(SimEventKind::OutingRoll));
        m.OnEvent(Ev(SimEventKind::MoveStart, 1, 1, 10));
        m.OnEvent(Ev(SimEventKind::Arrived, 1, 1, 10));
        m.OnEvent(Ev(SimEventKind::MoveStart, 1, 1, 11, 1));      // 売上の前に、次へ
        auto v = Violations(m);
        CHECK(v.size() == 1 && v[0].kind == SimAnomaly::MoveWhileVisiting);

        SimInvariantMonitor m2;
        m2.OnEvent(Ev(SimEventKind::OutingRoll));
        m2.OnEvent(Ev(SimEventKind::MoveStart, 1, 1, 10));
        m2.OnEvent(Ev(SimEventKind::Arrived, 1, 1, 10));
        m2.OnEvent(Ev(SimEventKind::ReturnHome));                  // 売上の前に、帰宅
        v = Violations(m2);
        CHECK(v.size() == 1 && v[0].kind == SimAnomaly::ReturnWhileVisiting);

        // 行動テストの「帰宅させる」は、利用の途中でも、強制できる(通常の流れではないので、違反にしない)。
        SimInvariantMonitor m3;
        m3.OnEvent(Ev(SimEventKind::OutingRoll));
        m3.OnEvent(Ev(SimEventKind::MoveStart, 1, 1, 10));
        m3.OnEvent(Ev(SimEventKind::Arrived, 1, 1, 10));
        m3.OnEvent(Ev(SimEventKind::ReturnStart, 1, 1, 0, 0, SimEventResult::Info, SimReason::TestApplied, 0.0f, 7));
        m3.OnEvent(Ev(SimEventKind::ReturnHome, 1, 1, 0, 0, SimEventResult::Success, SimReason::None, 0.0f, 7));
        CHECK(Violations(m3).empty());
    }
    // ---- 立ち寄りの回数が、設定の上限を超えた ----
    {
        SimInvariantMonitor m;
        m.SetMaxStopovers(2);
        m.OnEvent(Ev(SimEventKind::OutingRoll));
        m.OnEvent(Ev(SimEventKind::FacilitySelect, 1, 1, 12, 2, SimEventResult::Success, SimReason::StopStarted));
        CHECK(Violations(m).empty());                               // 2回目までは、よい
        m.OnEvent(Ev(SimEventKind::FacilitySelect, 1, 1, 13, 3, SimEventResult::Success, SimReason::StopStarted));
        const auto v = Violations(m);
        CHECK(v.size() == 1 && v[0].kind == SimAnomaly::StopCountExceeded && v[0].value == 3.0f);
        // 食事系で取りやめる時の「成功の施設選択」(reasonがStopStartedではない)は、数えない。
        m.OnEvent(Ev(SimEventKind::FacilitySelect, 1, 1, 14, 5, SimEventResult::Success, SimReason::None));
        CHECK(Violations(m).empty());
    }
    // ---- 売上が、負・NaN ----
    {
        SimInvariantMonitor m;
        m.OnEvent(Ev(SimEventKind::OutingRoll));
        m.OnEvent(Ev(SimEventKind::MoveStart, 1, 1, 10));
        m.OnEvent(Ev(SimEventKind::Arrived, 1, 1, 10));
        m.OnEvent(Ev(SimEventKind::VisitFinished, 1, 1, 10, 0, SimEventResult::Success, SimReason::None, -5.0f));
        const auto v = Violations(m);
        CHECK(v.size() == 1 && v[0].kind == SimAnomaly::NegativeRevenue);
    }
    // ---- 途中から見た外出(outingId==0。ロード直後など)は、追わない ----
    {
        SimInvariantMonitor m;
        m.OnEvent(Ev(SimEventKind::Arrived, 0, 1, 10));
        m.OnEvent(Ev(SimEventKind::VisitFinished, 0, 1, 10, 0, SimEventResult::Success, SimReason::None, 50.0f));
        m.OnEvent(Ev(SimEventKind::ReturnHome, 0));
        CHECK(Violations(m).empty());
        // 解体で売上なしに終わった利用(Warning)の後の、帰宅は、違反にしない。
        SimInvariantMonitor m2;
        m2.OnEvent(Ev(SimEventKind::OutingRoll));
        m2.OnEvent(Ev(SimEventKind::MoveStart, 1, 1, 10));
        m2.OnEvent(Ev(SimEventKind::Arrived, 1, 1, 10));
        m2.OnEvent(Ev(SimEventKind::VisitFinished, 1, 1, 10, 0, SimEventResult::Warning, SimReason::FacilityDemolished));
        m2.OnEvent(Ev(SimEventKind::ReturnStart));
        m2.OnEvent(Ev(SimEventKind::ReturnHome));
        CHECK(Violations(m2).empty());
        // 施設が無くなっていた到着(Warning)は、利用を始めない。
        SimInvariantMonitor m3;
        m3.OnEvent(Ev(SimEventKind::OutingRoll));
        m3.OnEvent(Ev(SimEventKind::MoveStart, 1, 1, 10));
        m3.OnEvent(Ev(SimEventKind::Arrived, 1, 1, 10, 0, SimEventResult::Warning, SimReason::FacilityMissingOnArrival));
        m3.OnEvent(Ev(SimEventKind::ReturnStart));
        m3.OnEvent(Ev(SimEventKind::ReturnHome));
        CHECK(Violations(m3).empty());
    }
    // ---- 中断・立ち往生・住居なしで、状態を捨てる / 新しい外出で、状態が新しくなる / 世界が変わると消える ----
    {
        SimInvariantMonitor m;
        m.OnEvent(Ev(SimEventKind::OutingRoll));
        m.OnEvent(Ev(SimEventKind::MoveStart, 1, 1, 10));
        m.OnEvent(Ev(SimEventKind::Arrived, 1, 1, 10));
        m.OnEvent(Ev(SimEventKind::OutingAborted));                 // 解体された
        m.OnEvent(Ev(SimEventKind::ReturnStart));
        m.OnEvent(Ev(SimEventKind::ReturnHome));
        CHECK(Violations(m).empty());
        // 同じ住民の、別の外出は、別の履歴(同じ施設を使っても、よい)。
        Normal(m, 2); Normal(m, 3);
        CHECK(Violations(m).empty());
        m.OnEvent(Ev(SimEventKind::OutingRoll, 4));
        m.OnEvent(Ev(SimEventKind::Arrived, 4, 1, 10));
        CHECK(m.HasPending());
        m.OnWorldReset();
        CHECK(!m.HasPending());
    }
    std::printf(g_fail == 0 ? "ALL PASS\n" : "%d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
