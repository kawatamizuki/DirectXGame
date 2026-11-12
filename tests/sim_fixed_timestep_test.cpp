// 固定時間刻みへの移行(F1): 固定刻みの部品(FixedTimestep)と、時計の Advance の単体テスト。
// 「描画のFPS・倍率が変わっても、1回の刻みは同じで、同じゲーム内の時間なら同じ回数・同じ時計の状態になる」ことを確かめる。
#include <cmath>
#include <cstdio>
#include <vector>
#include "FixedTimestep.h"
#include "GameClock.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)

// フレームの実時間の並び(秒)を与えて、毎フレームの回数を集める。
static std::vector<int> RunFrames(FixedTimestep& stepper, const std::vector<double>& frames, double scale)
{
    std::vector<int> counts;
    for (double dt : frames) { counts.push_back(stepper.Accumulate(dt, scale)); }
    return counts;
}

static std::vector<double> Constant(double dt, int count) { return std::vector<double>(static_cast<size_t>(count), dt); }

// ばらつくフレームの間隔(5〜30ms)。決まった並びにするため、自前の簡単な乱数で作る。合計は total 秒ちょうど。
static std::vector<double> Jittered(double total, unsigned seed)
{
    std::vector<double> frames;
    double sum = 0.0;
    unsigned s = seed;
    while (true)
    {
        s = s * 1103515245u + 12345u;
        const double dt = 0.005 + 0.025 * static_cast<double>((s >> 8) & 0xFFFF) / 65535.0;
        if (sum + dt >= total) { frames.push_back(total - sum); break; }
        frames.push_back(dt);
        sum += dt;
    }
    return frames;
}

static int Sum(const std::vector<int>& v) { int s = 0; for (int x : v) { s += x; } return s; }

// 固定刻みで時計を進めて、最後の状態を返す(FPSの違いを、フレームの並びで表す)。
struct ClockState { int week = 0; int day = 0; float hours = 0.0f; int weekEnds = 0; int steps = 0; };
static ClockState DriveClock(const std::vector<double>& frames, double scale)
{
    FixedTimestep stepper;
    GameClock clock;
    ClockState state;
    for (double dt : frames)
    {
        const int n = stepper.Accumulate(dt, scale);
        for (int i = 0; i < n; ++i)
        {
            clock.Advance(static_cast<float>(stepper.StepGameSeconds()));
            state.weekEnds += clock.ConsumeWeekEnds();
            ++state.steps;
        }
    }
    state.week = clock.GetWeekCount();
    state.day = clock.GetDayCount();
    state.hours = clock.GetTimeOfDayHours();
    return state;
}

int main()
{
    // ---- 描画のFPSが違っても、同じ時間なら、同じ回数 ----
    {
        FixedTimestep a, b, c;
        const std::vector<int> fps60 = RunFrames(a, Constant(1.0 / 60.0, 600), 1.0);
        const std::vector<int> fps30 = RunFrames(b, Constant(1.0 / 30.0, 300), 1.0);
        const std::vector<int> fps120 = RunFrames(c, Constant(1.0 / 120.0, 1200), 1.0);
        CHECK(Sum(fps60) == 600 && Sum(fps30) == 600 && Sum(fps120) == 600);   // 10秒 = 600回
        bool each60 = true; for (int n : fps60) { each60 = each60 && n == 1; }
        bool each30 = true; for (int n : fps30) { each30 = each30 && n == 2; }
        bool each120 = true; for (int n : fps120) { each120 = each120 && (n == 0 || n == 1); }
        CHECK(each60 && each30 && each120);
        // ばらつくフレームの間隔でも、合計の回数は同じ(残りは1回分未満)。
        FixedTimestep d;
        const int jitter = Sum(RunFrames(d, Jittered(10.0, 7u), 1.0));
        CHECK(jitter == 600 || jitter == 599);
        CHECK(d.AccumulatedGameSeconds() < d.StepGameSeconds());
    }
    // ---- 倍率を上げても、刻みは同じで、回数が増えるだけ ----
    {
        FixedTimestep a;
        const std::vector<int> fast = RunFrames(a, Constant(1.0 / 60.0, 600), 4.0);
        CHECK(Sum(fast) == 2400);
        bool each = true; for (int n : fast) { each = each && n == 4; }
        CHECK(each);
        CHECK(a.StepGameSeconds() == 1.0 / 60.0);
    }
    // ---- 停止(倍率0)では、貯めない・回さない ----
    {
        FixedTimestep a;
        a.Accumulate(1.0 / 120.0, 1.0);                 // 半分だけ貯まる
        const double kept = a.AccumulatedGameSeconds();
        CHECK(a.Accumulate(1.0, 0.0) == 0 && a.AccumulatedGameSeconds() == kept);
        CHECK(a.Accumulate(-1.0, 1.0) == 0 && a.Accumulate(NAN, 1.0) == 0 && a.Accumulate(1.0 / 60.0, NAN) == 0);
    }
    // ---- 長い停止は、1フレームの上限で切り捨てる(一気に追いつかない) ----
    {
        FixedTimestep a;
        CHECK(a.Accumulate(2.0, 1.0) == 15);            // 0.25秒分 = 15回
        CHECK(std::fabs(a.DroppedGameSeconds() - 1.75) < 1e-9);
        FixedTimestepSettings s; s.maxStepsPerFrame = 10;
        FixedTimestep b(s);
        CHECK(b.Accumulate(0.25, 4.0) == 10);           // 60回分のうち、上限の10回だけ
        CHECK(std::fabs(b.DroppedGameSeconds() - 50.0 / 60.0) < 1e-9);
        CHECK(b.AccumulatedGameSeconds() < b.StepGameSeconds());
        CHECK(b.TotalSteps() == 10);
    }
    // ---- 補間の割合・捨てる ----
    {
        FixedTimestep a;
        a.Accumulate(1.5 / 60.0, 1.0);                  // 1回出して、半分残る
        CHECK(std::fabs(a.Alpha() - 0.5) < 1e-6);
        a.Reset();
        CHECK(a.AccumulatedGameSeconds() == 0.0 && a.Alpha() == 0.0);
    }
    // ---- 時計: Update(dt) と Advance(dt × 倍率) は、全く同じ結果 ----
    {
        GameClock u, v;
        u.SetTimeScale(4.0f);
        bool same = true;
        for (int i = 0; i < 20000; ++i)
        {
            const float dt = (i % 3 == 0) ? (1.0f / 30.0f) : (1.0f / 60.0f);
            u.Update(dt);
            v.Advance(dt * 4.0f);
            same = same && u.GetTimeOfDayHours() == v.GetTimeOfDayHours() && u.GetActiveDeltaHours() == v.GetActiveDeltaHours() &&
                   u.GetScaledDeltaSeconds() == v.GetScaledDeltaSeconds() && u.ConsumeWeekEnds() == v.ConsumeWeekEnds() &&
                   u.ConsumeWeekStarts() == v.ConsumeWeekStarts() && u.GetWeekCount() == v.GetWeekCount();
        }
        CHECK(same);
    }
    // ---- 固定刻みで時計を進めると、30・60・120FPS・ばらつくFPS で、同じ時計の状態になる(倍率も同じなら) ----
    {
        const double seconds = 400.0;                   // 3週間ほど
        const ClockState s60 = DriveClock(Constant(1.0 / 60.0, 24000), 1.0);
        const ClockState s30 = DriveClock(Constant(1.0 / 30.0, 12000), 1.0);
        const ClockState s120 = DriveClock(Constant(1.0 / 120.0, 48000), 1.0);
        const ClockState jitter = DriveClock(Jittered(seconds, 99u), 1.0);
        CHECK(s60.steps == 24000 && s30.steps == 24000 && s120.steps == 24000);
        CHECK(s60.hours == s30.hours && s60.hours == s120.hours && s60.week == s30.week && s60.week == s120.week && s60.weekEnds == s120.weekEnds);
        // ばらつく間隔では、最後に1回分未満の端数が残ることがある(その時は1回少ない)。回数が同じなら、状態も同じ。
        CHECK(jitter.steps == 24000 || jitter.steps == 23999);
        if (jitter.steps == 24000) { CHECK(jitter.hours == s60.hours && jitter.week == s60.week); }
        // 4倍で同じゲーム内の時間(実時間は4分の1)なら、同じ回数・同じ状態。
        const ClockState fast = DriveClock(Constant(1.0 / 60.0, 6000), 4.0);
        CHECK(fast.steps == 24000 && fast.hours == s60.hours && fast.week == s60.week && fast.weekEnds == s60.weekEnds);
        CHECK(s60.week >= 3);
    }
    std::printf(g_fail == 0 ? "ALL PASSED\n" : "%d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
