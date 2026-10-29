#pragma once
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "SimDebugRecorder.h"

// 住民の行動の統計(段階4)。毎ティックの全イベントを溜めるのではなく、イベント・判断の内訳・外出判定が記録される瞬間に、
// 軽い「カウンター」を足していく(SimDebugRecorder が、記録と同時に、観察者(ISimRecordObserver)として呼ぶ)。記録の容量の上限で古いイベントを捨てても、統計は減らない。
//
//  ・ここは、数えるだけ。乱数を引かない・住民の状態を変えない(記録のON/OFFで、シミュレーションの結果は変わらない)。
//  ・ImGui・画面には依存しない(表示は DebugTools/SimStatsPanel)。人間向けの文章も持たない(数値と列挙型だけ)。
//  ・「通常のプレイ」と「行動テストで強制した流れ(testId が付いたイベント)」は、別々に数える(scope)。強制した結果で、通常のバランスの数字を汚さない。
//  ・不成立の理由は、**排他的な「最初の不成立の理由」**で数える(1回の外出・立ち寄りが、2つの理由に重複して数えられない)。
//  ・期間は、通算(集計を始めてから)・今週・先週・週ごとの推移。
//  ・ロード・リセット(新しい世界)で、全て消える(施設・住民の id が別の物になるため)。

// 数える範囲。
enum class SimStatScope : uint8_t
{
    Normal,     // 通常のプレイ(行動テストの番号が付いていないイベント)
    Test        // 行動テストで強制している流れの中で起きたイベント
};

constexpr size_t kSimStatScopeCount = 2;

// SimReason の数(SimReason に値を足したら、ここも合わせる)。
constexpr size_t kSimReasonCount = static_cast<size_t>(SimReason::TestRejected) + 1;

// 候補の除外の理由の数(CandidateReject の値の数)。
constexpr size_t kSimCandidateRejectCount = static_cast<size_t>(CandidateReject::OutOfRange) + 1;

// 確率の判定の集計。「期待した当選の数」と「実際の当選の数」を並べて、目視の感覚を数値と照合するのに使う。
struct SimRollTally
{
    uint64_t trials = 0;        // 判定した回数
    uint64_t hits = 0;          // 当選した回数
    double sumChance = 0.0;     // 各判定の確率(0〜1)の合計 = 期待される当選の数
    double sumVariance = 0.0;   // 各判定の p(1-p) の合計 = 当選の数のばらつき(分散)

    void Add(float chance, bool hit);
    double ExpectedHits() const { return sumChance; }
    // 期待値からの、当選の数のばらつき(標準偏差)。判定の確率が毎回違っても使える(ポアソン二項分布)。
    double StdDev() const;
};

// 1つの期間・1つの範囲ぶんの、行動の流れの集計。
struct SimStatBucket
{
    // ---- 外出(毎ティックの判定から、帰宅まで) ----
    SimRollTally outingRollNeed;        // 外出判定(需要の不足による)。**自然な抽選だけ**(行動テストで強制した当選は、数えない)
    SimRollTally outingRollSpontaneous; // 外出判定(自発的)
    SimRollTally outingRollLowStamina;  // 外出判定(足りているが、体力が足りない。確率は0なので、必ず不発)
    uint64_t outingStarted = 0;         // 外出を考え始めた(外出判定に当選した)
    uint64_t purposeChosen = 0;         // 目的を決めた(主目的)
    uint64_t mainSelectOk = 0;          // 主目的の施設を選べた
    uint64_t outingFailed = 0;          // 出られなかった(OutingBlocked + 主目的の施設選びの失敗)。理由は outingFailReason(排他)
    std::array<uint64_t, kSimReasonCount> outingFailReason{};
    uint64_t departed = 0;              // 施設へ歩き始めた(主目的)
    uint64_t arrived = 0;               // 施設に着いて、利用を始めた(主目的)
    uint64_t mainVisits = 0;            // 主目的の利用が終わった
    uint64_t returnedHome = 0;          // 家に着いた(外出が終わった)
    uint64_t aborted = 0;               // 行き先が解体されて、中断した
    uint64_t stranded = 0;              // 立ち往生になった
    uint64_t strandedRecovered = 0;     // 立ち往生から、帰り始めた
    uint64_t homeless = 0;              // 帰る家が無くなった
    uint64_t rerouted = 0;              // 迂回して帰った

    // ---- 立ち寄り(利用完了の後の判定から) ----
    uint64_t stopConsidered = 0;        // 立ち寄りを考えた回数(利用完了ごとの判定。**排他の最初の結果**で、必ず、どれか1つに数える)
    std::array<uint64_t, kSimReasonCount> stopReason{};  // 立ち寄りの結果(理由の別。StopRollMiss=抽選に外れた、StopStarted=寄った、など)
    SimRollTally stopRoll;              // 立ち寄りの確率の抽選(条件を通った時だけ)
    uint64_t stopStarted = 0;           // 立ち寄りを始めた
    uint64_t stopVisits = 0;            // 立ち寄り先の利用が終わった

    // ---- 利用(施設の利用が終わった回数・売上) ----
    uint64_t visits = 0;                // 利用が終わった回数(主目的 + 立ち寄り)
    double revenue = 0.0;               // 売上の合計(円)
    double lifeRecovered = 0.0;         // 生活需要の回復量の合計
    double staminaUsed = 0.0;           // 体力の消費量の合計

    // ---- 行動テスト自体 ----
    uint64_t testApplied = 0;           // テスト指示を適用した
    uint64_t testRejected = 0;          // テスト指示を、普通のルールで拒否した
};

// 施設ごとの統計(範囲ごと・通算)。
struct SimFacilityStat
{
    uint64_t candidateIn = 0;           // 候補として評価された回数(施設選び・立ち寄り選びの判断の内訳に入っていた回数)
    uint64_t chosen = 0;                // 選ばれた回数(主目的 + 立ち寄り)
    uint64_t chosenMain = 0;            // 主目的として選ばれた回数
    uint64_t chosenStop = 0;            // 立ち寄りとして選ばれた回数
    std::array<uint64_t, kSimCandidateRejectCount> rejected{}; // 候補から除外された回数(理由の別。CandidateReject)
    uint64_t arrivals = 0;              // 着いた回数
    uint64_t visits = 0;                // 利用が終わった回数(利用者数)
    double revenue = 0.0;               // 売上(円)
    double lifeRecovered = 0.0;         // 回復させた生活需要の合計
};

// 住民ごとの統計(範囲ごと・通算)。
struct SimResidentStat
{
    uint64_t outings = 0;               // 外出した回数(外出判定の当選)
    uint64_t stopovers = 0;             // 立ち寄りを始めた回数
    uint64_t visits = 0;                // 利用が終わった回数
    double revenue = 0.0;               // この住民が使ったお金(= 施設の売上)
    uint64_t stranded = 0;              // 立ち往生になった回数
    uint64_t aborted = 0;               // 中断した回数
    uint64_t homeless = 0;              // 住居なしになった回数
    int firstWeek = -1;                 // 初めて統計に現れた週
    int lastWeek = -1;                  // 直近の週
};

// 週ごとの推移の1行。
struct SimWeekRow
{
    int week = 0;
    SimStatBucket bucket;
};

// 範囲(通常/テスト)ごとの、全ての統計。
struct SimStatsScopeData
{
    SimStatBucket total;                // 通算(集計を始めてから)
    SimStatBucket thisWeek;             // 今の週
    SimStatBucket lastWeek;             // 1つ前の週(まだ無ければ、全て0)
    bool hasLastWeek = false;
    std::vector<SimWeekRow> weekly;     // 終わった週の推移(古い順。上限あり)
    std::unordered_map<uint32_t, SimFacilityStat> facilities;
    std::unordered_map<uint32_t, SimResidentStat> residents;
};

// 95%の信頼区間(Wilsonの方法)。試行が少ない時に、率だけで断定しないための目安。trials=0なら、0〜1を返す。
void SimWilsonInterval(uint64_t successes, uint64_t trials, double& low, double& high);

// 率の分母が、これより少ない時は、「参考」と明記する。
constexpr uint64_t kSimStatsMinReliableTrials = 30;

// 週ごとの推移に残す上限(週の数)。
constexpr size_t kSimStatsMaxWeeks = 104;

class SimStats : public ISimRecordObserver
{
public:
    // ---- 書き込み(SimDebugRecorder から。DemandSystem は直接呼ばない) ----
    void OnEvent(const SimEvent& event) override;
    void OnDetail(const SimDecisionDetail& detail) override;
    // 外出判定の1回。naturalHit は、強制を除いた、本当の抽選の結果。常に「通常」の範囲に数える(強制は、イベントの方に、テストの印が付く)。
    void OnOutingRoll(int week, OutingChanceBranch branch, float chance, bool naturalHit) override;
    void OnWorldReset() override { Reset(); }

    // 全て消す(集計を始め直す)。始めた週は、次の記録で決まる。
    void Reset();

    // ---- 読み取り ----
    const SimStatsScopeData& Scope(SimStatScope scope) const { return m_scopes[static_cast<size_t>(scope)]; }
    int StartWeek() const { return m_startWeek; }       // 集計を始めた週(まだ何も数えていなければ -1)
    int CurrentWeek() const { return m_currentWeek; }   // 今の週(まだ無ければ -1)
    uint64_t TotalCounted() const { return m_counted; } // 数えたイベント・判断・判定の数(動作の確認用)

private:
    // 週が進んでいたら、今週を先週へ送り、推移に残す。
    void AdvanceWeek(int week);
    SimStatsScopeData& Data(SimStatScope scope) { return m_scopes[static_cast<size_t>(scope)]; }

    std::array<SimStatsScopeData, kSimStatScopeCount> m_scopes;
    int m_startWeek = -1;
    int m_currentWeek = -1;
    uint64_t m_counted = 0;
};
