#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "SimRunDigest.h"
#include "SimTest.h"

class DemandSystem;
class GameClock;
class GameObject;
class ScenarioManager;

// 再現セッション(段階5)。「同じテスト条件で、同じイベントの列になる」ことを確かめるための、記録と再生。
//
//  記録: ① 今の街を、スナップショット(セーブ)に保存し、すぐ読み込み直す(乱数の内部状態も復元する) → ② 以降、固定刻みで進める
//        → ③ 行動テストの指示と、時間の倍率の変更を、フレーム番号つきで記録する → ④ 止めた時の結果の「指紋」(SimRunDigest)を残す。
//  再生: スナップショットを読み込み直し(乱数の状態を復元)、同じフレーム番号で、同じ指示・倍率を入れて進める → 同じフレーム数で止めて、指紋を比べる。
//        「再抽選して再生」は、乱数の種を新しくする(結果が変わることの確認用)。
//
//  ・刻み: 記録中・再生中は、実時間のフレーム間隔ではなく、**固定の刻み(1/60秒)**で時計を進める。フレームの重さ(PCの速さ)に結果が依存しないため。
//    ステップ数(フレーム数)が違うと、結果がずれる。これは仕様(再生は、記録と同じフレーム数だけ進める)。
//  ・記録中・再生中は、建物・道路の変更・ロード・ゲームシーン/Playingモードの切り替えをしない(指示と倍率以外の操作は、記録しないため)。変わったら、中止する。
//  ・画面(ImGui)には依存しない(表示は DebugTools/SimReproPanel)。ENABLE_EDITOR に関わらずビルドされるが、操作は Editor のパネルからだけ。
//  ・完全な「世界の巻き戻し」ではない。保存できる物(セーブデータの範囲)を戻す。保存していない状態が結果に効くなら、再現できない(指紋が一致しない)ので、そこで分かる。
enum class SimReproState : uint8_t
{
    Idle,           // 何もしていない
    Recording,      // 記録中
    Replaying,      // 再生中
    Held            // 再生が終わって、最終フレームで止まっている(「続きを実行」を押すまで、時間は進まない)
};

// 再生の乱数の取り方。
enum class SimReplayRandom : uint8_t
{
    SameState,      // スナップショットの乱数の状態を復元する(同じ結果になるはず)
    Reseed          // 新しい種で始める(結果が変わるはず。「再抽選」の確認用)
};

// 記録した、行動テストの指示。
struct SimReproCommand
{
    uint64_t frame = 0;                 // この番号のフレームの、更新の前に入れる(画面で押した次のフレーム)
    SimTestCommand command;
    uint32_t selectedResidentId = 0;
};

// 記録した、時間の倍率の変更。
struct SimReproScaleChange
{
    uint64_t frame = 0;
    float scale = 1.0f;
};

// 1回の実行(記録または再生)の結果。
struct SimReproRunSummary
{
    bool valid = false;
    uint64_t frames = 0;                // 進めたフレーム数
    SimRunDigestResult digest;          // イベント・抽選の指紋
    int week = 0;                       // 終わった時の、ゲーム内の週・時刻
    float hour = 0.0f;
    uint32_t seed = 0;                  // その世界の乱数の種
    uint32_t randomState = 0;           // 終わった時の乱数の内部状態
    std::vector<SimDigestTraceEntry> trace;   // イベント・抽選ごとの途中経過(最初にずれた位置を探すのに使う)
};

class SimReproController
{
public:
    static constexpr const char* kSnapshotPath = "Saves/repro_snapshot.json";
    static constexpr float kFixedStepSeconds = 1.0f / 60.0f;   // 記録中・再生中の、1フレームの刻み(実時間の秒)

    void Initialize(DemandSystem* demand, ScenarioManager* scenario, GameClock* clock, std::vector<GameObject>* objects);

    // ---- 画面からの要求(次の PrepareFrame で実行する。ロード中の描画・更新と、ぶつからないように) ----
    void RequestBeginRecording() { m_requestBegin = true; }
    void RequestStopRecording() { m_requestStop = true; }
    void RequestReplay(SimReplayRandom random) { m_requestReplay = true; m_requestReplayRandom = random; }
    void RequestCancel() { m_requestCancel = true; }
    // 再生が終わって止まっている時に、続きを実行する(再生を始める前の速度で、ふだんのプレイに戻る)。
    void RequestResume() { m_requestResume = true; }

    // フレームの最初に呼ぶ。時計に渡す時間(秒)を返す(記録中・再生中は固定刻み、そうでなければ realDeltaSeconds)。
    // simulationActive: 住民のシミュレーションが動く状態か(ゲームシーン かつ Playing)。structureSignature: 建物・道路の数の指紋(変わったら中止する)。
    float PrepareFrame(float realDeltaSeconds, bool simulationActive, uint64_t structureSignature);

    // 世界が入れ替わった(ロード)時に呼ぶ。自分が起こした読み込み以外なら、実行中の記録・再生を中止する。
    void NotifyWorldReplaced();

    // ---- 読み取り(画面用) ----
    SimReproState GetState() const { return m_state; }
    // 時間の倍率を、再生が決めている間か(この間は、メニューの一時停止などが、倍率に触らない)。
    bool OwnsTimeScale() const { return m_state == SimReproState::Replaying || m_state == SimReproState::Held; }
    // メニューなどで、ゲームの時間を止める状態か(Game が毎フレーム伝える)。再生中にこれが true の間は、再生のフレームも進めない(ふだんのプレイと同じく、時間が止まる)。
    void SetUserPaused(bool paused) { m_userPaused = paused; }
    uint64_t GetFrame() const { return m_frame; }
    uint64_t GetEndFrame() const { return m_endFrame; }                         // 再生で、止めるフレーム数
    const std::vector<SimReproCommand>& GetCommands() const { return m_commands; }
    const std::vector<SimReproScaleChange>& GetScaleChanges() const { return m_scaleChanges; }
    const SimReproRunSummary& GetRecorded() const { return m_recorded; }
    const SimReproRunSummary& GetReplayed() const { return m_replayed; }
    bool HasSnapshot() const { return m_hasSnapshot; }
    const std::string& GetMessage() const { return m_message; }
    SimReplayRandom GetLastReplayRandom() const { return m_lastReplayRandom; }
    // 記録した区間(スナップショット + 指示・倍率 + 結果)を、ファイルに保存・読み込む(Saves/repro/<名前>.json)。止まっている間(Idle)だけ。
    // 読み込んだ記録は、再生・比較に使える(途中経過は保存しないので、ずれた位置は出せない)。
    bool SaveSegment(const std::string& name, std::string& message);
    bool LoadSegment(const std::string& name, std::string& message);
    std::vector<std::string> ListSegments() const;
    // 記録に、途中経過(最初にずれた位置を探すため)が有るか。ファイルから読み込んだ記録には、無い。
    bool RecordedHasTrace() const { return !m_recorded.trace.empty(); }
    static constexpr const char* kSegmentDirectory = "Saves/repro";

    // 記録と再生の指紋が一致したか(両方の結果が有る時だけ意味を持つ)。
    bool Matches() const;
    // 記録と再生で、最初に違ったイベント・抽選の位置(0から)。同じなら -1。両方の結果が無ければ -1。
    int FirstDivergence() const;

private:
    bool LoadSnapshotWithRandomMode(int mode);   // 0=復元 1=再抽選。読み込めたらtrue
    SimReproRunSummary Capture() const;
    void Stop(const std::string& message);

    DemandSystem* m_demand = nullptr;
    ScenarioManager* m_scenario = nullptr;
    GameClock* m_clock = nullptr;
    std::vector<GameObject>* m_objects = nullptr;

    SimReproState m_state = SimReproState::Idle;
    uint64_t m_frame = 0;               // 今、進めているフレーム(記録・再生の開始から数えた番号。1から)
    uint64_t m_endFrame = 0;
    uint64_t m_signature = 0;
    float m_lastScale = 1.0f;
    float m_scaleBeforeReplay = 1.0f;   // 再生を始める前の、時間の倍率(再生が終わったら・中止したら、この値に戻す)
    size_t m_nextCommand = 0;
    size_t m_nextScale = 0;
    bool m_userPaused = false;          // メニューなどで、時間を止めている間(再生を、その間だけ止める)
    bool m_expectedLoad = false;        // 自分が起こした読み込みの間だけ true
    bool m_hasSnapshot = false;

    bool m_requestBegin = false;
    bool m_requestStop = false;
    bool m_requestReplay = false;
    bool m_requestCancel = false;
    bool m_requestResume = false;
    SimReplayRandom m_requestReplayRandom = SimReplayRandom::SameState;
    SimReplayRandom m_lastReplayRandom = SimReplayRandom::SameState;

    std::vector<SimReproCommand> m_commands;
    std::vector<SimReproScaleChange> m_scaleChanges;
    SimReproRunSummary m_recorded;
    SimReproRunSummary m_replayed;
    std::string m_message;
};
