// テストマップ 段階3(3-A): 自動検証の、試行ごとの種・実行条件の検査・決定論の判定(SimTrial.h)の単体テスト。
// 実際に街を進める部分(SimEndurance)は、画面・ゲーム本体に依存するので、ここでは扱わない(ゲームの中の診断で確かめる)。
#include <cstdio>
#include <set>
#include "SimTrial.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)

static SimTrialResult MakeResult(int index, uint32_t seed, bool determinismRun, bool completed, uint64_t hash)
{
    SimTrialResult r;
    r.index = index; r.seed = seed; r.determinismRun = determinismRun; r.completed = completed;
    r.digest.hash = hash; r.digest.events = 10; r.digest.rolls = 3;
    return r;
}

int main()
{
    // ---- 試行ごとの種: 同じ入力なら同じ種、番号・基準が違えば(ほぼ)違う種、0にならない ----
    {
        CHECK(MakeTrialSeed(12345, 0) == MakeTrialSeed(12345, 0));
        CHECK(MakeTrialSeed(12345, 0) != MakeTrialSeed(12345, 1));
        CHECK(MakeTrialSeed(12345, 0) != MakeTrialSeed(12346, 0));
        std::set<uint32_t> seen;
        bool noZero = true;
        for (uint32_t base = 0; base < 50; ++base)
        {
            for (uint32_t i = 0; i < 100; ++i)
            {
                const uint32_t s = MakeTrialSeed(base, i);
                noZero = noZero && (s != 0);
                seen.insert(s);
            }
        }
        CHECK(noZero);
        CHECK(seen.size() == 5000);     // 5000通りの組み合わせが、全て違う種になる(連番の種のまま使わない)
        // 連番の基準・番号でも、上位のビットまで混ざっている(xorshift32 の、最初の乱数が似通わないように)。
        CHECK((MakeTrialSeed(1, 0) >> 16) != (MakeTrialSeed(1, 1) >> 16));
    }
    // ---- 種の並び: 回数ぶん、決定論の確認ありなら最後に1回目と同じ種 ----
    {
        SimTrialConditions c;
        c.baseSeed = 777; c.trialCount = 3; c.determinismCheck = false;
        std::vector<uint32_t> seeds = BuildTrialSeeds(c);
        CHECK(seeds.size() == 3);
        CHECK(seeds[0] == MakeTrialSeed(777, 0) && seeds[1] == MakeTrialSeed(777, 1) && seeds[2] == MakeTrialSeed(777, 2));
        c.determinismCheck = true;
        seeds = BuildTrialSeeds(c);
        CHECK(seeds.size() == 4 && seeds[3] == seeds[0]);
        // 同じ条件なら、同じ並び(後から、同じ試行をやり直せる)。
        CHECK(BuildTrialSeeds(c) == seeds);
        c.trialCount = 1;
        seeds = BuildTrialSeeds(c);
        CHECK(seeds.size() == 2 && seeds[0] == seeds[1]);
    }
    // ---- 実行条件の検査 ----
    {
        SimTrialConditions c;
        CHECK(ValidateTrialConditions(c).empty());
        CHECK(c.timeScale == 20.0f);                                   // 暫定の固定値
        c.weeks = 0;                    CHECK(!ValidateTrialConditions(c).empty());
        c.weeks = kSimTrialMaxWeeks;    CHECK(ValidateTrialConditions(c).empty());
        c.weeks = kSimTrialMaxWeeks + 1; CHECK(!ValidateTrialConditions(c).empty());
        c.weeks = 26;
        c.trialCount = 0;               CHECK(!ValidateTrialConditions(c).empty());
        c.trialCount = kSimTrialMaxCount + 1; CHECK(!ValidateTrialConditions(c).empty());
        c.trialCount = 5;
        c.timeScale = 0.0f;             CHECK(!ValidateTrialConditions(c).empty());
        c.timeScale = 20.0f;
        c.wallLimitSecondsPerTrial = 0; CHECK(!ValidateTrialConditions(c).empty());
    }
    // ---- 決定論の判定: 同じ種の2回だけを比べる。違う種の指紋の違い・一致は、判定に使わない ----
    {
        std::vector<SimTrialResult> results;
        CHECK(!EvaluateDeterminism(results).checked);                   // 何も無い
        results.push_back(MakeResult(0, 100, false, true, 0xAA));
        results.push_back(MakeResult(1, 200, false, true, 0xAA));       // 違う種で、同じ指紋(住民がいない街など)
        CHECK(!EvaluateDeterminism(results).checked);                   // 追加の1回が無ければ、判定しない
        results.push_back(MakeResult(2, 100, true, true, 0xAA));
        SimTrialDeterminism d = EvaluateDeterminism(results);
        CHECK(d.checked && d.matched);
        results[2].digest.hash = 0xBB;
        d = EvaluateDeterminism(results);
        CHECK(d.checked && !d.matched);
        results[2].digest.hash = 0xAA;
        results[2].digest.events = 11;                                  // イベントの数が違っても、不一致
        CHECK(!EvaluateDeterminism(results).matched);
        results[2].digest.events = 10;
        results[2].completed = false;                                   // 最後まで進まなかった(中止など)は、比べない
        CHECK(!EvaluateDeterminism(results).checked);
        results[2].completed = true;
        results[2].seed = 999;                                          // 種が違えば、比べない
        CHECK(!EvaluateDeterminism(results).checked);
    }
    std::printf(g_fail == 0 ? "ALL PASSED\n" : "%d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
