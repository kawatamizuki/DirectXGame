// 段階5: 乱数の状態の保存・復元(SimRandom)と、実行結果の指紋(SimRunDigest)の単体テスト。
#include <cstdio>
#include "SimRunDigest.h"
#include "SimStats.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)

static SimEvent MakeEvent(uint32_t resident, SimEventKind kind, float a)
{
    SimEvent e; e.residentId = resident; e.kind = kind; e.week = 2; e.hour = 9.5f; e.a = a; e.facilityId = 7; e.snapshot.stamina = 80.0f;
    return e;
}

int main()
{
    // ---- SimRandom: 状態を戻すと、同じ列が続く ----
    {
        SimRandom a(777);
        for (int i = 0; i < 10; ++i) { a.Next01(); }
        const uint32_t state = a.GetState();
        float expected[5];
        for (int i = 0; i < 5; ++i) { expected[i] = a.Next01(); }
        SimRandom b(1);
        b.SetState(state);
        bool same = true;
        for (int i = 0; i < 5; ++i) { same = same && (b.Next01() == expected[i]); }
        CHECK(same);
        SimRandom c(1); c.SetState(0);                 // 0は止まってしまう値なので、種と同じ扱い
        CHECK(c.GetState() != 0);
        c.NextU32();
        CHECK(c.GetState() != 0);
        SimRandom d(5), e(5);                          // 同じ種なら、同じ列
        CHECK(d.NextU32() == e.NextU32() && d.NextU32() == e.NextU32());
    }
    // ---- SimRunDigest: 同じ列は同じ指紋、違えば違う ----
    {
        SimRunDigest x, y, z;
        for (SimRunDigest* d : { &x, &y })
        {
            d->OnOutingRoll(1, OutingChanceBranch::NeedDriven, 0.25f, false);
            d->OnEvent(MakeEvent(1, SimEventKind::OutingRoll, 0.25f));
            d->OnEvent(MakeEvent(2, SimEventKind::MoveStart, 40.0f));
        }
        CHECK(x.Result() == y.Result());
        CHECK(x.Result().events == 2 && x.Result().rolls == 1);
        // 数値が1つ違うだけで、指紋が変わる。
        z.OnOutingRoll(1, OutingChanceBranch::NeedDriven, 0.25f, false);
        z.OnEvent(MakeEvent(1, SimEventKind::OutingRoll, 0.25f));
        z.OnEvent(MakeEvent(2, SimEventKind::MoveStart, 40.5f));
        CHECK(x.Result() != z.Result());
        // 順番が違っても、指紋が変わる。
        SimRunDigest p, q;
        p.OnEvent(MakeEvent(1, SimEventKind::OutingRoll, 1.0f)); p.OnEvent(MakeEvent(2, SimEventKind::OutingRoll, 1.0f));
        q.OnEvent(MakeEvent(2, SimEventKind::OutingRoll, 1.0f)); q.OnEvent(MakeEvent(1, SimEventKind::OutingRoll, 1.0f));
        CHECK(p.Result() != q.Result());
        // 記録の通し番号・外出番号・テスト番号の値は、混ぜない(世界を作り直すと、値が進むため)。
        SimRunDigest r, s;
        SimEvent e1 = MakeEvent(1, SimEventKind::ReturnHome, 0.0f); e1.eventId = 5; e1.outingId = 3; e1.testId = 11; e1.runId = 1;
        SimEvent e2 = MakeEvent(1, SimEventKind::ReturnHome, 0.0f); e2.eventId = 905; e2.outingId = 77; e2.testId = 42; e2.runId = 9;
        r.OnEvent(e1); s.OnEvent(e2);
        CHECK(r.Result() == s.Result());
        // ただし、テスト由来かどうかは、混ぜる。
        SimRunDigest t;
        SimEvent e3 = e2; e3.testId = 0;
        t.OnEvent(e3);
        CHECK(t.Result() != s.Result());
        // 新しい世界で、0に戻る。
        x.OnWorldReset();
        SimRunDigest fresh;
        CHECK(x.Result() == fresh.Result() && x.Result().events == 0);
    }
    // ---- 記録部の観察者(複数) ----
    {
        SimDebugRecorder rec;
        SimRunDigest d1, d2;
        SimStats stats;
        rec.AddObserver(&d1); rec.AddObserver(&d2); rec.AddObserver(&stats); rec.AddObserver(&stats);   // 同じ物を2回付けても、1回だけ
        rec.SetEnabled(true);
        rec.AddEvent(MakeEvent(1, SimEventKind::OutingRoll, 0.5f));
        CHECK(d1.Result() == d2.Result() && d1.Result().events == 1);
        CHECK(stats.Scope(SimStatScope::Normal).total.outingStarted == 1);
        rec.RemoveObserver(&d2);
        rec.AddEvent(MakeEvent(1, SimEventKind::OutingRoll, 0.5f));
        CHECK(d1.Result().events == 2 && d2.Result().events == 1);
        rec.StartNewRun();
        CHECK(d1.Result().events == 0 && stats.StartWeek() == -1);
        // 記録がOFFの間は、指紋も変わらない。
        rec.SetEnabled(false);
        rec.AddEvent(MakeEvent(1, SimEventKind::OutingRoll, 0.5f));
        CHECK(d1.Result().events == 0);
    }
    std::printf(g_fail == 0 ? "ALL PASS\n" : "%d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
