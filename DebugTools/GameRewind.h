#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "SimDebugRecorder.h"
#include "SimTest.h"

class DemandSystem;
class GameObject;
class GameClock;
class ScenarioManager;

// タイムライン(段階5の続き): プレイ中のゲーム内時間を、シークバーで巻き戻し・進められるようにする。
//
//  しくみ(キーフレーム方式):
//   ・「ON」の間、ゲームを固定刻み(1/60秒)で進める(フレームレートに結果が依存しない。重いフレームでは、1フレームに複数ステップ進める)。
//   ・一定ステップごと(既定600)に、街の保存データ(キーフレーム)をメモリに撮る。建物・道路・入居などの変更があった時にも撮る。
//   ・行動テストの指示と、時間の倍率の変更を、ステップ番号つきで記録する。
//   ・シーク: 目的のステップの直前のキーフレームを読み込み(乱数の状態も復元)、そこから、記録した指示・倍率を入れて、画面を描かずに、目的のステップまで進める
//     (1フレームの持ち時間を決めて、分けて進める。進み具合を出す)。
//   ・巻き戻した後は、時間が止まる(過去を見ている)。「ここから再開」で、そこから新しい未来が始まる(それより後の記録は、捨てる)。
//   ・遡れる長さ(既定: 実時間10分)と、メモリの上限(既定256MB。仮)で、古いキーフレームから捨てる。
//   ・整合性の検査: 各キーフレームから次のキーフレームまで進め直して、乱数の状態・時刻が一致するかを確かめる(保存していない状態が、結果に効いていないかを調べる)。
//
//  限界(正直に):
//   ・完全な「世界の巻き戻し」ではなく、「セーブデータの範囲を戻して、決定論的に進め直す」物。保存していない状態が結果に効くと、ずれる(整合性の検査で分かる)。
//   ・巻き戻した時点の、住民デバッグの記録・行動統計は、そのキーフレームから数え直しになる(その前の分は、残らない)。音・カメラ・画面の状態は、戻さない。
//   ・Editing(Stop)の間の操作(ギズモでの移動など)は、記録しない。Playingに戻った時に、新しいキーフレームを撮る。
//   ・再現セッション(記録・再生)とは、同時に使えない(どちらも、時間の進め方を決めるため)。
enum class GameRewindState : uint8_t
{
    Off,        // 使っていない
    Live,       // 記録しながら、ふだんどおり進む
    Seeking,    // 目的のステップまで、進め直している
    Past        // 過去のステップで止まっている(見ているだけ。「ここから再開」で、新しい未来が始まる)
};

// 街の保存データ1つ(あるステップの直後の状態)。
struct GameRewindKeyframe
{
    uint64_t step = 0;          // 進めたステップの数(0から)
    std::string bytes;          // 街の保存データ(ScenarioManager::CaptureBytes)
    bool structural = false;    // 建物・道路などの変更の直後に撮った物か
    uint32_t rngState = 0;      // 整合性の検査に使う、その時の乱数の状態
    int week = 0;
    float hour = 0.0f;
};

struct GameRewindScaleEntry
{
    uint64_t atStep = 0;        // このステップを進める前に、入れる
    float scale = 1.0f;
};

struct GameRewindCommandEntry
{
    uint64_t atStep = 0;
    SimTestCommand command;
    uint32_t selectedResidentId = 0;
};

// 調整できる設定(仮の既定値)。
struct GameRewindSettings
{
    int windowMinutes = 10;             // 遡れる長さ(実時間の分)
    int memoryCapMB = 256;              // キーフレームに使うメモリの上限(MB)。仮
    int keyframeInterval = 600;         // 何ステップごとに、キーフレームを撮るか(1/60秒なので、600=10秒)
    int stepBudgetMilliseconds = 8;     // シークで、1フレームに進め直す持ち時間(ミリ秒)
};

// 整合性の検査の結果。
struct GameRewindVerifyResult
{
    bool running = false;
    int checked = 0;            // 検査した区間の数
    int total = 0;
    int mismatched = 0;         // 一致しなかった区間の数
    uint64_t firstMismatchStep = 0;   // 最初に一致しなかった区間の、終わりのステップ(mismatchedが0なら意味なし)
    bool finished = false;
};

class GameRewind : public ISimRecordObserver
{
public:
    static constexpr float kStepSeconds = 1.0f / 60.0f;     // 1ステップの、実時間(秒)
    static constexpr uint64_t kStepsPerMinute = 3600;

    // stepFunction(秒, headless): 時計と住民を1ステップ進める。headless==true は、画面を描かないシーク中(メニューの一時停止などの、画面の処理は、しない)。
    // signatureFunction: 建物・道路の数の指紋(変わったら、キーフレームを撮る)。
    using StepFunction = std::function<void(float, bool)>;
    using SignatureFunction = std::function<uint64_t()>;

    void Initialize(DemandSystem* demand, ScenarioManager* scenario, GameClock* clock, std::vector<GameObject>* objects,
                    StepFunction stepFunction, SignatureFunction signatureFunction);

    // ---- ON/OFF ----
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled; }
    // ONの間、時計と住民の更新は、このクラスが決める(Game は、ふだんの更新をしない)。
    bool IsDriving() const { return m_enabled; }

    // ---- 毎フレーム(ゲームシーンで、住民の更新をする場所で呼ぶ) ----
    // simulationActive: Playingか(Editingでは、時間を進めない)。signature: 今の建物・道路の指紋。
    void Update(float realDeltaSeconds, bool simulationActive);

    // 世界が入れ替わった(ロードなど)時に呼ぶ。自分が起こした読み込み以外なら、記録を捨てて、最初からやり直す。
    void NotifyWorldReplaced();

    // ---- 画面からの要求(次の Update で行う) ----
    void RequestSeek(uint64_t step) { m_requestSeek = true; m_requestSeekStep = step; }
    void RequestResumeFromHere() { m_requestResume = true; }
    void RequestGoLatest() { m_requestLatest = true; }
    void RequestVerify() { m_requestVerify = true; }

    // ---- 設定 ----
    GameRewindSettings& Settings() { return m_settings; }
    const GameRewindSettings& Settings() const { return m_settings; }

    // ---- 読み取り(画面用) ----
    GameRewindState GetState() const { return m_state; }
    uint64_t GetCurrentStep() const { return m_stepsDone; }
    uint64_t GetOldestStep() const { return m_keyframes.empty() ? 0 : m_keyframes.front().step; }
    uint64_t GetLatestStep() const { return m_state == GameRewindState::Live ? m_stepsDone : m_latestStep; }
    uint64_t GetSeekTarget() const { return m_seekTarget; }
    size_t GetKeyframeCount() const { return m_keyframes.size(); }
    const std::vector<GameRewindKeyframe>& GetKeyframes() const { return m_keyframes; }
    size_t GetBytesUsed() const { return m_bytesUsed; }
    const std::string& GetMessage() const { return m_message; }
    const GameRewindVerifyResult& GetVerifyResult() const { return m_verify; }
    // 最新へ戻った時に、乱数・時刻が、記録した時と一致しなかったか(true=ずれた)。
    bool LatestMismatched() const { return m_latestMismatch; }
    size_t GetScaleEntryCount() const { return m_scaleEntries.size(); }
    size_t GetCommandEntryCount() const { return m_commandEntries.size(); }
    uint64_t GetKeyframesTaken() const { return m_keyframesTaken; }
    double GetLastKeyframeMilliseconds() const { return m_lastKeyframeMs; }
    double GetLastSeekMilliseconds() const { return m_lastSeekMs; }

    // ---- 記録部の観察者(入居の選択を、構造の変更として扱うため) ----
    void OnEvent(const SimEvent& event) override;
    void OnDetail(const SimDecisionDetail& /*detail*/) override {}
    void OnOutingRoll(int /*week*/, OutingChanceBranch /*branch*/, float /*chance*/, bool /*naturalHit*/) override {}
    void OnWorldReset() override {}

private:
    void StartRecording();
    void StopAndClear();
    bool CaptureKeyframe(bool structural);
    bool LoadKeyframe(const GameRewindKeyframe& keyframe);
    void EnforceLimits();
    float ScaleBefore(uint64_t step) const;
    int FindKeyframeAtOrBefore(uint64_t step) const;
    void BeginSeek(uint64_t target, int keyframeIndex, bool resumeAfter);
    void RunSeekSlice();
    void FinishSeek();
    void ResumeLive();
    void LeaveLive();
    void ApplyInputsAt(uint64_t step);
    void BeginVerifyStep();
    void FinishVerifyStep();

    DemandSystem* m_demand = nullptr;
    ScenarioManager* m_scenario = nullptr;
    GameClock* m_clock = nullptr;
    std::vector<GameObject>* m_objects = nullptr;
    StepFunction m_stepFunction;
    SignatureFunction m_signatureFunction;

    GameRewindSettings m_settings;
    bool m_enabled = false;
    bool m_needStart = false;           // ONにして、まだ、最初のキーフレームを撮っていない
    GameRewindState m_state = GameRewindState::Off;

    uint64_t m_stepsDone = 0;           // 進めたステップの数
    uint64_t m_latestStep = 0;          // Live を離れた時の、最新のステップ(Live の間は、m_stepsDone と同じ)
    float m_accumulator = 0.0f;         // 実時間の端数(1ステップに満たない分)
    uint64_t m_lastSignature = 0;
    bool m_structuralDirty = false;     // 入居の選択など、構造の変更が有った(次の Update で、キーフレームを撮る)
    bool m_wasEditing = false;
    float m_lastLoggedScale = -1.0f;
    float m_userScale = 1.0f;           // Live を離れた時の、時間の倍率(再開した時に、戻す)
    bool m_expectedLoad = false;

    std::vector<GameRewindKeyframe> m_keyframes;
    std::vector<GameRewindScaleEntry> m_scaleEntries;
    std::vector<GameRewindCommandEntry> m_commandEntries;
    size_t m_bytesUsed = 0;
    uint64_t m_keyframesTaken = 0;
    double m_lastKeyframeMs = 0.0;
    double m_lastSeekMs = 0.0;

    // シークの途中の状態。
    uint64_t m_seekTarget = 0;
    bool m_resumeAfterSeek = false;
    size_t m_scaleCursor = 0;
    size_t m_commandCursor = 0;
    uint64_t m_seekStartTick = 0;       // シークを始めた時刻(ミリ秒)
    std::chrono::steady_clock::time_point m_seekStartClock;
    bool m_hasLatestFingerprint = false;
    uint32_t m_latestRng = 0;
    int m_latestWeek = 0;
    float m_latestHour = 0.0f;
    bool m_latestMismatch = false;

    // 整合性の検査。
    bool m_verifyActive = false;
    int m_verifyIndex = 0;
    GameRewindVerifyResult m_verify;

    bool m_requestSeek = false;
    uint64_t m_requestSeekStep = 0;
    bool m_requestResume = false;
    bool m_requestLatest = false;
    bool m_requestVerify = false;

    std::string m_message;
};
