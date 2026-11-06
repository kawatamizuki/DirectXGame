#pragma once
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>
#include "SimDebugRecorder.h"

// 「流れ」の不変条件の監視(段階6)。記録されるイベントの並びを、住民ごとに追いかけて、ありえない並びを見つける。
//
//  ・状態の不変条件(体力の範囲・参照が壊れていない、など)は CheckResidentInvariants が、住民の今の値から調べる。
//    こちらは、「外出1回の中の、イベントの順序」を調べる(状態を見ても分からない、二重の売上・使用済みの施設の再利用など)。
//  ・記録の観察者として、記録と同時に追う。乱数を引かない・住民の状態を変えない(記録のON/OFFで、結果は変わらない)。
//  ・違反は、ここで溜めるだけ。DemandSystem が、更新の最後に取り出して、異常のイベントとして記録する(記録の最中に、記録を足さないため)。
//  ・外出に属さないイベント(outingId==0。記録を始める前から続いていた外出・ロード直後)は、追わない(途中から見ても、誤検出しないため)。
//
//  調べる並び(1回の外出の中):
//    MoveStart(歩き始め) → Arrived(到着) → VisitFinished(利用完了・売上) → … → ReturnStart → ReturnHome
//    ・到着の前に、歩き始めていない            → ArrivedWithoutMove
//    ・売上(利用完了)の前に、到着していない / 1回の到着で、売上が2回 → SettleWithoutArrival
//    ・同じ外出で、同じ施設を、2回使った       → DuplicateFacilityInOuting
//    ・利用の途中(到着後、売上の前)に、次へ歩き出した → MoveWhileVisiting / 帰宅した → ReturnWhileVisiting(行動テストの強制は、除く)
//    ・立ち寄りの回数が、設定の上限を超えた     → StopCountExceeded
//    ・売上が、負・NaN/Inf                      → NegativeRevenue
//    ・売上を確定した施設が、もうオブジェクトとして無い → SettledOnMissingFacility(取り出す時に、DemandSystem が調べる)
struct SimFlowViolation
{
    SimAnomaly kind = SimAnomaly::None;
    uint32_t residentId = 0;
    uint32_t facilityId = 0;
    float value = 0.0f;         // 問題の値(立ち寄りの何回目か・売上など)
};

class SimInvariantMonitor : public ISimRecordObserver
{
public:
    // 立ち寄りの上限(設定の値)。DemandSystem が、取り出す前に渡す。
    void SetMaxStopovers(int maxStopovers) { m_maxStopovers = maxStopovers; }

    void OnEvent(const SimEvent& event) override;
    void OnDetail(const SimDecisionDetail& /*detail*/) override {}
    void OnOutingRoll(int /*week*/, OutingChanceBranch /*branch*/, float /*chance*/, bool /*naturalHit*/) override {}
    void OnWorldReset() override { Clear(); }

    // 溜まった違反と、売上を確定した(住民, 施設)を取り出す(取り出すと、空になる)。
    void Drain(std::vector<SimFlowViolation>& violations, std::vector<std::pair<uint32_t, uint32_t>>& settled);
    bool HasPending() const { return !m_violations.empty() || !m_settled.empty(); }

    uint64_t TotalViolations() const { return m_totalViolations; }
    // 監視が使っているメモリの概算(バイト)。メモリのパネル用。
    size_t EstimateBytes() const;
    void Clear();

private:
    struct Flow
    {
        uint32_t outingId = 0;
        bool moveStarted = false;           // 歩き始めて、まだ到着していない
        bool visitOpen = false;             // 到着して、まだ売上を確定していない
        std::vector<uint32_t> visited;      // この外出で、売上を確定した施設
    };

    void Raise(SimAnomaly kind, const SimEvent& event, float value);

    int m_maxStopovers = 2;
    std::unordered_map<uint32_t, Flow> m_flows;
    std::vector<SimFlowViolation> m_violations;
    std::vector<std::pair<uint32_t, uint32_t>> m_settled;
    uint64_t m_totalViolations = 0;
};
