#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ResidentSimSettings.h"
#include "SimRunDigest.h"

// テストマップの自動検証(テストマップ 段階3)の、実行条件・試行ごとの結果・種の作り方。
// 画面・ゲーム本体に依存しない(単体テストから使える)。実際に進めるのは SimEndurance(耐久試験と同じ進め方)。
//  ・各試行は、必ずテストマップの原本(ファイルの中身)から読み込み直して始める(前の試行の住民・時間を引き継がない)。
//  ・試行ごとの種は、基準の種と試行の番号を混ぜて作る(MakeTrialSeed)。同じ条件なら、同じ種の並びになる。
//  ・合否の判定はしない(住民の停滞なども、失敗とは扱わない)。決定論の確認は「同じ種の2回が一致するか」だけを見る。

// 1回の試行が終わった時点の、街の簡単な指標。表示するだけで、合否の判定には使わない。
struct SimTrialIndicators
{
    int populationTotal = 0;    // 表示上の人口
    int agentCount = 0;         // シミュレートしている住民の数
    int unmetDemand = 0;        // 需要が不足している住民の数
    int homelessCount = 0;      // 住居が無い住民の数
    int strandedCount = 0;      // 道路が無くなって、帰れなくなった住民の数
    int cash = 0;               // 資金
};

// 自動検証の実行条件。始める前に決めて、結果と一緒に残す(後から、同じ条件で、やり直せるように)。
struct SimTrialConditions
{
    std::string testMapId;                  // 使ったテストマップのid
    std::string testMapPath;                // そのファイル
    uint64_t testMapFingerprint = 0;        // ファイルの初期状態の指紋(TestMapStore::ComputeFingerprint)。同じ原本かの確認用
    int weeks = 26;                         // 1回の試行で進める週数(ゲーム内)
    float timeScale = 20.0f;                // 時計の倍率。暫定で20倍に固定。結果は倍率にも左右されるので、条件として残す
    uint32_t baseSeed = 1;                  // 基準の種(試行ごとの種は、これと試行の番号から作る)
    int trialCount = 1;                     // 試行の回数(決定論の確認の、追加の1回は含まない)
    bool determinismCheck = false;          // 最後に、1回目と同じ種で、もう1回進めて、指紋を比べる
    int wallLimitSecondsPerTrial = 600;     // 1回の試行の、実時間の上限(秒)
    bool stopTrialOnAnomaly = false;        // 異常が出たら、その試行を止める(次の試行へ進む)
    std::string gameBuild;                  // 実行したゲームのビルド
    ResidentSimSettings settings;           // 使った設定値(記録だけ。段階3では変えない。種 settings.seed は、試行ごとの種で置き換えて進める)
};

// 1回の試行の結果。
struct SimTrialResult
{
    int index = 0;                          // 何回目か(0から)
    uint32_t seed = 0;                      // 使った種
    bool determinismRun = false;            // 決定論の確認の、追加の1回か
    bool completed = false;                 // 指定の週数まで進めたか
    std::string stopReason;                 // 終わった理由
    uint64_t steps = 0;                     // 進めたステップ数(1ステップ = 1/60秒 × 倍率)
    int weeksDone = 0;                      // 進めた週数
    double wallSeconds = 0.0;               // かかった実時間(秒)
    uint64_t anomalies = 0;                 // 検出した異常の数
    SimRunDigestResult digest;              // イベント・抽選の指紋
    SimTrialIndicators indicators;          // 終わった時点の、簡単な指標
};

// 決定論の確認の結果。
struct SimTrialDeterminism
{
    bool checked = false;                   // 比べられたか(同じ種の2回が、両方とも最後まで進んだ時だけ)
    bool matched = false;                   // 指紋が一致したか
};

// 試行の回数・週数の上限(打ち間違いで、何時間も止まらないように)。
constexpr int kSimTrialMaxCount = 100;
constexpr int kSimTrialMaxWeeks = 520;

// 試行ごとの種。基準の種と試行の番号を、混ぜて作る(xorshift32 は、連番の種だと、最初の乱数が似通うため)。0 にはしない。
inline uint32_t MakeTrialSeed(uint32_t baseSeed, uint32_t index)
{
    uint32_t h = baseSeed ^ (index * 0x9E3779B9u + 0x7F4A7C15u);
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h == 0 ? 1u : h;
}

// 実行条件に、おかしな所が無いか。問題があれば、その説明(無ければ空)。
inline std::string ValidateTrialConditions(const SimTrialConditions& conditions)
{
    if (conditions.weeks < 1 || conditions.weeks > kSimTrialMaxWeeks) { return "週数は、1〜" + std::to_string(kSimTrialMaxWeeks) + " にしてください"; }
    if (conditions.trialCount < 1 || conditions.trialCount > kSimTrialMaxCount) { return "試行の回数は、1〜" + std::to_string(kSimTrialMaxCount) + " にしてください"; }
    if (!(conditions.timeScale > 0.0f)) { return "時計の倍率は、0より大きくしてください"; }
    if (conditions.wallLimitSecondsPerTrial < 1) { return "1回の実時間の上限は、1秒以上にしてください"; }
    return std::string();
}

// 実行する試行の、種の並び。決定論の確認ありなら、最後に、1回目と同じ種を足す。
inline std::vector<uint32_t> BuildTrialSeeds(const SimTrialConditions& conditions)
{
    std::vector<uint32_t> seeds;
    const int count = conditions.trialCount < 1 ? 1 : conditions.trialCount;
    for (int i = 0; i < count; ++i)
    {
        seeds.push_back(MakeTrialSeed(conditions.baseSeed, static_cast<uint32_t>(i)));
    }
    if (conditions.determinismCheck)
    {
        seeds.push_back(seeds.front());
    }
    return seeds;
}

// 決定論の確認: 1回目と、同じ種の追加の1回の指紋を比べる。
// 種が違う試行どうしの指紋の違いは、判定に使わない(違う種でも、同じ結果になることがある。例: 住民がいない街)。
inline SimTrialDeterminism EvaluateDeterminism(const std::vector<SimTrialResult>& results)
{
    SimTrialDeterminism determinism;
    const SimTrialResult* first = nullptr;
    const SimTrialResult* repeat = nullptr;
    for (const SimTrialResult& result : results)
    {
        if (result.determinismRun) { repeat = &result; }
        else if (!first) { first = &result; }
    }
    if (!first || !repeat || first->seed != repeat->seed || !first->completed || !repeat->completed)
    {
        return determinism;
    }
    determinism.checked = true;
    determinism.matched = (first->digest == repeat->digest);
    return determinism;
}
