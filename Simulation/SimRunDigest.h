#pragma once
#include <cstdint>
#include <vector>
#include "SimDebugRecorder.h"

// 実行結果の「指紋」(ダイジェスト)。記録されたイベントと外出判定の抽選を、順番どおりにハッシュ(混ぜた数)にする。
// 同じスナップショット・同じ乱数の状態・同じ指示・同じ刻み(フレーム数)で動かしたら、同じ指紋になる(= 再現できた)。
// 乱数の取り方や、判断が変わると、指紋が変わる(= 再現できなかった / 結果が変わった)。
//
//  ・SimDebugRecorder の観察者として、記録と同時に混ぜる(記録の容量で古いイベントを捨てても、指紋には全て含まれる)。
//  ・記録の通し番号(eventId・outingId)・テスト番号の値は、混ぜない(世界を作り直すたびに進むので、同じ結果でも違う値になる)。
//    住民・施設のidは、混ぜる(同じセーブを読み直すと、同じidが付く: GameObjectFactory::ResetIdCounter)。
//  ・新しい世界(ロード・リセット)が始まると、0に戻る。
struct SimRunDigestResult
{
    uint64_t hash = 0;          // 混ぜた値
    uint64_t events = 0;        // 混ぜたイベントの数
    uint64_t rolls = 0;         // 混ぜた外出判定の抽選の数

    bool operator==(const SimRunDigestResult& other) const { return hash == other.hash && events == other.events && rolls == other.rolls; }
    bool operator!=(const SimRunDigestResult& other) const { return !(*this == other); }
};

// イベント1件ぶんの、途中経過(そのイベントまでの指紋と、何のイベントか)。結果が違った時に、「最初にずれたイベント」を見つけるために残す。
struct SimDigestTraceEntry
{
    uint64_t hashAfter = 0;     // このイベントまでを混ぜた指紋
    uint8_t kind = 0;           // SimEventKind(外出判定の抽選は 255)
    uint32_t residentId = 0;
    int week = 0;
    float hour = 0.0f;
    float a = 0.0f;             // イベントの数値a(抽選なら、確率)
};

// 2つの途中経過を比べて、最初に違うイベントの位置(0から)を返す。全て同じなら、-1。片方が短い時は、短い方の長さ(続きが無い位置)。
int SimFirstDivergence(const std::vector<SimDigestTraceEntry>& a, const std::vector<SimDigestTraceEntry>& b);

class SimRunDigest : public ISimRecordObserver
{
public:
    SimRunDigest() { Reset(); }

    void OnEvent(const SimEvent& event) override;
    void OnDetail(const SimDecisionDetail& /*detail*/) override {}  // 判断の内訳は、イベントの結果に現れるので、混ぜない
    void OnOutingRoll(int week, OutingChanceBranch branch, float chance, bool naturalHit) override;
    void OnWorldReset() override { Reset(); }

    void Reset();
    SimRunDigestResult Result() const { return m_result; }
    // 途中経過(イベント・抽選ごと。上限 kMaxTrace 件まで。上限を超えたら、それ以降は残さない)。
    const std::vector<SimDigestTraceEntry>& Trace() const { return m_trace; }
    static constexpr size_t kMaxTrace = 400000;

private:
    void MixBytes(const void* data, size_t size);
    template <typename T> void Mix(const T& value) { MixBytes(&value, sizeof(T)); }

    void PushTrace(uint8_t kind, uint32_t residentId, int week, float hour, float a);

    SimRunDigestResult m_result;
    std::vector<SimDigestTraceEntry> m_trace;
};
