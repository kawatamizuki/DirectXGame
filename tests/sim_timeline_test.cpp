// 段階2: 一時停止の条件(SimBreakpoints)と、記録の時間軸(SimTimeline)の単体テスト。
#include <cstdio>
#include <vector>
#include "SimDebugRecorder.h"
#include "SimTimeline.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)

static SimEvent Ev(uint32_t resident, SimEventKind kind, SimEventResult result = SimEventResult::Info)
{
    SimEvent e;
    e.residentId = resident;
    e.kind = kind;
    e.result = result;
    return e;
}

int main()
{
    // ---- SimBreakpoints::Matches ----
    {
        SimBreakpoints bp;
        CHECK(!bp.AnyEnabled());
        CHECK(!bp.Matches(Ev(1, SimEventKind::OutingRoll)));        // 条件が無ければ、止めない

        bp.onViolation = true;
        CHECK(bp.AnyEnabled());
        CHECK(bp.Matches(Ev(1, SimEventKind::Anomaly, SimEventResult::Violation)));
        CHECK(!bp.Matches(Ev(1, SimEventKind::Anomaly, SimEventResult::Warning)));
        CHECK(!bp.Matches(Ev(1, SimEventKind::OutingRoll, SimEventResult::Success)));

        bp = SimBreakpoints{};
        bp.onWarning = true;                                        // 注意以上(注意・違反)
        CHECK(bp.Matches(Ev(1, SimEventKind::Anomaly, SimEventResult::Warning)));
        CHECK(bp.Matches(Ev(1, SimEventKind::Anomaly, SimEventResult::Violation)));
        CHECK(!bp.Matches(Ev(1, SimEventKind::Anomaly, SimEventResult::NotMet)));

        bp = SimBreakpoints{};
        bp.kindMask = 1u << static_cast<uint32_t>(SimEventKind::ReturnHome);
        CHECK(bp.Matches(Ev(1, SimEventKind::ReturnHome)));
        CHECK(!bp.Matches(Ev(1, SimEventKind::OutingRoll)));
        bp.resultMask = 1u << static_cast<uint32_t>(SimEventResult::Success);   // 種類+結果
        CHECK(!bp.Matches(Ev(1, SimEventKind::ReturnHome, SimEventResult::Info)));
        CHECK(bp.Matches(Ev(1, SimEventKind::ReturnHome, SimEventResult::Success)));

        bp.residentId = 7;                                          // 住民の絞り込みは、全ての条件に掛かる
        CHECK(!bp.Matches(Ev(1, SimEventKind::ReturnHome, SimEventResult::Success)));
        CHECK(bp.Matches(Ev(7, SimEventKind::ReturnHome, SimEventResult::Success)));
    }

    // ---- 記録が「止める印」を立てる ----
    {
        SimDebugRecorder rec;
        rec.SetEnabled(true);
        rec.Breakpoints().kindMask = 1u << static_cast<uint32_t>(SimEventKind::ReturnHome);
        rec.AddEvent(Ev(1, SimEventKind::OutingRoll, SimEventResult::Success));
        CHECK(!rec.IsPauseActive());
        const uint64_t id = rec.AddEvent(Ev(1, SimEventKind::ReturnHome, SimEventResult::Info));
        CHECK(rec.IsPauseActive() && rec.PausedEventId() == id);
        rec.AddEvent(Ev(1, SimEventKind::ReturnHome, SimEventResult::Info));
        CHECK(rec.PausedEventId() == id);                           // 最初の1件だけ(解除するまで、次の印は立てない)
        rec.ClearPause();
        CHECK(!rec.IsPauseActive() && rec.PausedEventId() == 0);
        rec.SetEnabled(false);                                      // OFFにすると、印も外れる(何も記録しない)
        CHECK(rec.AddEvent(Ev(1, SimEventKind::ReturnHome)) == 0);
    }

    // ---- 時間軸 ----
    {
        SimDebugRecorder rec;
        rec.SetEnabled(true);
        // 住民1: 外出A(OutingRoll,Select,MoveStart,ReturnHome) → 外出B(OutingRoll,OutingBlocked)。住民2は混ぜる。
        std::vector<uint64_t> ids;
        ids.push_back(rec.AddEvent(Ev(1, SimEventKind::OutingRoll, SimEventResult::Success)));    // 0
        rec.AddEvent(Ev(2, SimEventKind::OutingRoll, SimEventResult::Success));
        ids.push_back(rec.AddEvent(Ev(1, SimEventKind::FacilitySelect, SimEventResult::Success))); // 1
        ids.push_back(rec.AddEvent(Ev(1, SimEventKind::MoveStart)));                              // 2
        ids.push_back(rec.AddEvent(Ev(1, SimEventKind::ReturnHome)));                             // 3
        ids.push_back(rec.AddEvent(Ev(1, SimEventKind::OutingRoll, SimEventResult::Success)));    // 4
        ids.push_back(rec.AddEvent(Ev(1, SimEventKind::OutingBlocked, SimEventResult::NotMet)));  // 5

        const SimTimelineEvents all = BuildTimeline(rec, 1, nullptr);
        CHECK(all.size() == 6);
        for (size_t i = 0; i + 1 < all.size(); ++i) { CHECK(all[i]->eventId < all[i + 1]->eventId); }  // 古い順
        CHECK(BuildTimeline(rec, 2, nullptr).size() == 1);
        CHECK(BuildTimeline(rec, 99, nullptr).empty());

        const SimTimelineEvents onlyRoll = BuildTimeline(rec, 1, [](const SimEvent& e) { return e.kind == SimEventKind::OutingRoll; });
        CHECK(onlyRoll.size() == 2);

        // FindTimelineIndex
        bool clamped = true;
        CHECK(FindTimelineIndex(all, ids[2], &clamped) == 2 && !clamped);
        CHECK(FindTimelineIndex(SimTimelineEvents{}, 5) == -1);
        // 絞り込みで外れた id → それより前で一番近いイベント
        CHECK(FindTimelineIndex(onlyRoll, ids[2]) == 0);
        CHECK(FindTimelineIndex(onlyRoll, ids[5]) == 1);
        // 先頭より古い id → 0 に寄せる(clamped)
        CHECK(FindTimelineIndex(onlyRoll, 0, &clamped) == 0 && clamped);

        // 外出の前後
        CHECK(PreviousOutingIndex(all, 0) == -1);
        CHECK(PreviousOutingIndex(all, 2) == -1);       // 1つ目の外出の途中: 1つ前の外出は無い
        CHECK(PreviousOutingIndex(all, 5) == 0);        // 2つ目の外出の途中 → 1つ前の外出の最初
        CHECK(NextOutingIndex(all, 1) == 4);            // 1つ目の外出の途中 → 次の外出の最初
        CHECK(NextOutingIndex(all, 5) == -1);
        CHECK(NextOutingIndex(all, 99) == -1 && PreviousOutingIndex(all, 99) == -1);

        // 条件に合うイベントの前後
        const auto isReturn = [](const SimEvent& e) { return e.kind == SimEventKind::ReturnHome; };
        CHECK(NextMatchIndex(all, 0, isReturn) == 3);
        CHECK(NextMatchIndex(all, 3, isReturn) == -1);  // 自分自身は含めない
        CHECK(PreviousMatchIndex(all, 5, isReturn) == 3);
        CHECK(PreviousMatchIndex(all, 3, isReturn) == -1);
    }

    std::printf(g_fail == 0 ? "ALL PASSED\n" : "%d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
