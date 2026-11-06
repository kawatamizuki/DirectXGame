#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "SimDebugRecorder.h"
#include "SimRunDigest.h"

class DemandSystem;
class GameClock;
class ScenarioManager;

// 耐久試験・決定論テスト(段階6)。今の街を「テスト世界」にして、画面を描かずに、長い期間(例: 26週)を、一気に進める。
//
//  ・始める時に、今の街を保存して(基点)、その保存から読み込み直して始める。終わったら、基点へ戻す(プレイ中の街を、壊さない)。
//    異常で止めた時は、その状態を残して見られる(「元の世界へ戻す」で戻る)。
//  ・1ステップは、ふだんのプレイと同じ呼び方(時計 → 住民)の、固定刻み(1/60秒)。時計の倍率で、速く進める
//    (大きな時間を1回で渡すのではなく、ふだんと同じ順番・細かさで、ステップを重ねる。週末・需要の判定・移動・滞在の順番が、ふだんと一致する)。
//  ・処理予算: 1フレームの持ち時間(既定12ms)で分けて進める。時間切れ(実時間の上限)・異常での停止・中止が有る。
//  ・計測: 実行時間・ステップ/秒・ゲーム内時間の速さ・1ステップの平均・最も重かった1フレームの分・メモリ(ワーキングセット)の増え方・
//    記録の使用量・異常の数(種類別)・最初の異常(ステップ・週・住民・値)。結果は、CSVに保存する(Saves/endurance/)。
//  ・決定論テスト: 同じ基点・同じ乱数の状態から、同じステップ数を2回進めて、イベントの指紋が一致するかを調べる。
//  ・異常は、記録の観察者として数える(記録の容量で、古いイベントを捨てても、数え落とさない)。記録がONの間だけ(Editorのビルド)。
//  ・「プレイヤーの操作」は入れない(同じ状態から、同じ時間を進めるだけ)。
enum class SimEnduranceState : uint8_t
{
    Idle,       // 何もしていない
    Running,    // 実行中
    Held        // 異常で止まって、その状態を見られる(元へ戻す / このまま続ける、を選ぶ)
};

enum class SimEnduranceMode : uint8_t
{
    Endurance,      // 耐久試験: 指定の週数を進めて、異常・負荷を調べる
    Determinism     // 決定論テスト: 同じ条件で2回進めて、指紋が一致するかを調べる
};

struct SimEnduranceSettings
{
    int weeks = 26;                     // 進める週数(ゲーム内)
    float timeScale = 20.0f;            // 時計の倍率(大きいほど、1ステップで進むゲーム内時間が長い)
    int budgetMilliseconds = 12;        // 1フレームの持ち時間(ミリ秒)
    int wallLimitSeconds = 180;         // 実時間の上限(秒)。超えたら、止める
    bool stopOnAnomaly = true;          // 異常が出たら、止める(その状態を残す)
    int sampleIntervalSteps = 600;      // 何ステップごとに、計測を記録するか
};

// 途中の計測(1行)。
struct SimEnduranceSample
{
    uint64_t step = 0;
    int weeksDone = 0;
    double wallSeconds = 0.0;
    double workingSetMB = 0.0;
    uint64_t events = 0;                // 記録したイベントの総数
    uint64_t anomalies = 0;
};

// 最初の異常。
struct SimEnduranceAnomaly
{
    bool valid = false;
    uint64_t step = 0;
    int week = 0;
    float hour = 0.0f;
    uint32_t residentId = 0;
    SimAnomaly kind = SimAnomaly::None;
    float value = 0.0f;
};

// 1回の試験の結果。
struct SimEnduranceResult
{
    bool valid = false;
    SimEnduranceMode mode = SimEnduranceMode::Endurance;
    std::string stopReason;             // 終わった理由(指定の週数に達した・異常で止めた・時間切れ・中止)
    bool completed = false;             // 指定の週数まで進めたか
    uint64_t steps = 0;
    int weeksDone = 0;
    double wallSeconds = 0.0;
    double stepsPerSecond = 0.0;
    double gameSecondsPerRealSecond = 0.0;  // ゲーム内の時間が、実時間の何倍で進んだか(倍率を含む)
    double averageStepMilliseconds = 0.0;
    double maxFrameMilliseconds = 0.0;      // 最も重かった1フレームの、進めた時間
    double workingSetStartMB = 0.0;
    double workingSetEndMB = 0.0;
    double workingSetPeakMB = 0.0;
    uint64_t events = 0;
    size_t recorderBytes = 0;               // 記録の使用量(概算)
    uint64_t anomalies = 0;
    std::array<uint64_t, 32> anomalyByKind{};
    SimEnduranceAnomaly firstAnomaly;
    uint32_t seed = 0;                  // 試験の乱数の種(基点の保存から復元した種)
    SimRunDigestResult digest;          // イベント・抽選の指紋(1回目)
    bool determinismChecked = false;
    bool determinismMatched = false;
    SimRunDigestResult digestSecond;    // 決定論テストの、2回目の指紋
    std::string csvPath;
};

class SimEndurance : public ISimRecordObserver
{
public:
    using StepFunction = std::function<void(float, bool)>;
    static constexpr float kStepSeconds = 1.0f / 60.0f;
    static constexpr const char* kOutputDirectory = "Saves/endurance";

    void Initialize(DemandSystem* demand, ScenarioManager* scenario, GameClock* clock, StepFunction stepFunction);

    // ---- 要求(画面から。次の Update で始める) ----
    void RequestStart(SimEnduranceMode mode) { m_requestStart = true; m_requestMode = mode; }
    void RequestCancel() { m_requestCancel = true; }
    void RequestKeepWorld() { m_requestKeep = true; }       // Held: この状態のまま、ふだんのプレイに戻る
    void RequestRestoreWorld() { m_requestRestore = true; } // Held: 元の世界へ戻す

    // 実行中・止まっている間は、時計と住民の更新を、このクラスが決める。
    bool IsDriving() const { return m_state != SimEnduranceState::Idle; }
    void Update(float realDeltaSeconds);
    void NotifyWorldReplaced();

    SimEnduranceSettings& Settings() { return m_settings; }
    SimEnduranceState GetState() const { return m_state; }
    SimEnduranceMode GetRunningMode() const { return m_mode; }
    bool IsSecondRun() const { return m_secondRun; }
    uint64_t GetSteps() const { return m_steps; }
    uint64_t GetTargetSteps() const { return m_secondRun ? m_firstRunSteps : 0; }
    int GetWeeksDone() const;
    double GetWallSeconds() const;
    double GetWorkingSetMB() const;
    uint64_t GetAnomalyCount() const { return m_anomalyCount; }
    const SimEnduranceResult& GetResult() const { return m_result; }
    const std::vector<SimEnduranceSample>& GetSamples() const { return m_samples; }
    const std::string& GetMessage() const { return m_message; }
    // 基点の保存データ・計測の表が使っているメモリの概算(バイト)。メモリのパネル用。
    size_t EstimateBytes() const { return m_baseBytes.capacity() + m_samples.capacity() * sizeof(SimEnduranceSample); }

    // ---- 記録の観察者(異常を数える) ----
    void OnEvent(const SimEvent& event) override;
    void OnDetail(const SimDecisionDetail& /*detail*/) override {}
    void OnOutingRoll(int /*week*/, OutingChanceBranch /*branch*/, float /*chance*/, bool /*naturalHit*/) override {}
    void OnWorldReset() override {}

private:
    bool Begin(SimEnduranceMode mode);
    bool LoadBase();
    void StartRun();                // 基点を読み込んで、計測を始める
    void RunSlice();
    void FinishRun(const std::string& reason, bool completed);
    void Conclude();
    void Restore();
    void WriteCsv();
    void Sample();

    DemandSystem* m_demand = nullptr;
    ScenarioManager* m_scenario = nullptr;
    GameClock* m_clock = nullptr;
    StepFunction m_stepFunction;

    SimEnduranceSettings m_settings;
    SimEnduranceState m_state = SimEnduranceState::Idle;
    SimEnduranceMode m_mode = SimEnduranceMode::Endurance;

    std::string m_baseBytes;            // 基点(試験を始める時の街)
    float m_userScale = 1.0f;           // 試験を始める前の、時間の倍率
    int m_startWeek = 0;                // 試験の最初の週
    bool m_secondRun = false;           // 決定論テストの2回目か
    uint64_t m_firstRunSteps = 0;       // 1回目に進めたステップ数(2回目は、これだけ進める)
    uint64_t m_steps = 0;
    std::chrono::steady_clock::time_point m_runStart;
    double m_wallBefore = 0.0;          // 1回目に使った実時間(2回目の、合計の上限の計算に使う)
    double m_maxFrameMs = 0.0;
    double m_workingSetStart = 0.0;
    double m_workingSetPeak = 0.0;
    uint64_t m_anomalyCount = 0;
    std::array<uint64_t, 32> m_anomalyByKind{};
    SimEnduranceAnomaly m_firstAnomaly;
    SimRunDigestResult m_firstDigest;
    bool m_expectedLoad = false;

    std::vector<SimEnduranceSample> m_samples;
    SimEnduranceResult m_result;
    std::string m_message;

    bool m_requestStart = false;
    SimEnduranceMode m_requestMode = SimEnduranceMode::Endurance;
    bool m_requestCancel = false;
    bool m_requestKeep = false;
    bool m_requestRestore = false;
};
