#include "GameRewind.h"
#include <algorithm>
#include <chrono>
#include "DemandSystem.h"
#include "GameClock.h"
#include "GameObject.h"
#include "ScenarioManager.h"
#include "Debug.h"

namespace
{
    uint64_t NowMilliseconds()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    double ElapsedMs(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
}

void GameRewind::Initialize(DemandSystem* demand, ScenarioManager* scenario, GameClock* clock, std::vector<GameObject>* objects,
                             StepFunction stepFunction, SignatureFunction signatureFunction)
{
    m_demand = demand;
    m_scenario = scenario;
    m_clock = clock;
    m_objects = objects;
    m_stepFunction = std::move(stepFunction);
    m_signatureFunction = std::move(signatureFunction);

    m_demand->GetDebugRecorder().AddObserver(this);
    // 画面から出した行動テストの指示を、ステップ番号つきで残す。過去を見ている間に出したら、そこから新しい未来を始める。
    m_demand->AddOnTestCommandSubmitted([this](const SimTestCommand& command, uint32_t selectedResidentId)
    {
        if (!m_enabled || m_needStart)
        {
            return;
        }
        if (m_state == GameRewindState::Past)
        {
            ResumeLive();
        }
        if (m_state == GameRewindState::Live)
        {
            GameRewindCommandEntry entry;
            entry.atStep = m_stepsDone;
            entry.command = command;
            entry.selectedResidentId = selectedResidentId;
            m_commandEntries.push_back(entry);
        }
    });
}

void GameRewind::OnEvent(const SimEvent& event)
{
    // 入居の選択は、建物・道路の数が変わらないが、街の状態を変えるので、構造の変更として扱う(Live の間だけ。進め直しの最中は、無視する)。
    if (m_state == GameRewindState::Live && event.kind == SimEventKind::MovedIn)
    {
        m_structuralDirty = true;
    }
}

void GameRewind::SetEnabled(bool enabled)
{
    if (enabled == m_enabled)
    {
        return;
    }
    if (enabled)
    {
        m_enabled = true;
        m_needStart = true;
        m_state = GameRewindState::Live;
        m_message = "タイムラインを始めます(最初のキーフレームを撮ります)";
    }
    else
    {
        StopAndClear();
        m_enabled = false;
        m_state = GameRewindState::Off;
        m_message = "タイムラインをOFFにしました(記録を捨てました)";
    }
}

void GameRewind::StopAndClear()
{
    m_keyframes.clear();
    m_scaleEntries.clear();
    m_commandEntries.clear();
    m_bytesUsed = 0;
    m_stepsDone = 0;
    m_latestStep = 0;
    m_accumulator = 0.0f;
    m_structuralDirty = false;
    m_wasEditing = false;
    m_lastLoggedScale = -1.0f;
    m_verifyActive = false;
    m_verify = GameRewindVerifyResult{};
    m_hasLatestFingerprint = false;
    m_latestMismatch = false;
    m_requestSeek = m_requestResume = m_requestLatest = m_requestVerify = false;
}

void GameRewind::NotifyWorldReplaced()
{
    if (!m_enabled || m_expectedLoad)
    {
        return;
    }
    StopAndClear();
    m_needStart = true;
    m_state = GameRewindState::Live;
    m_message = "街が読み込み直されたので、タイムラインを最初からやり直します";
}

void GameRewind::StartRecording()
{
    StopAndClear();
    m_lastSignature = m_signatureFunction();
    m_lastLoggedScale = m_clock->GetTimeScale();
    m_scaleEntries.push_back({ 0, m_lastLoggedScale });
    CaptureKeyframe(false);
    m_needStart = false;
    m_state = GameRewindState::Live;
    m_message = "タイムラインを記録しています";
}

bool GameRewind::CaptureKeyframe(bool structural)
{
    const auto start = std::chrono::steady_clock::now();
    GameRewindKeyframe keyframe;
    keyframe.step = m_stepsDone;
    keyframe.structural = structural;
    std::string message;
    if (!m_scenario->CaptureBytes(keyframe.bytes, message))
    {
        m_message = "キーフレームを撮れませんでした: " + message;
        return false;
    }
    keyframe.rngState = m_demand->GetRandomState();
    keyframe.week = m_clock->GetWeekCount();
    keyframe.hour = m_clock->GetTimeOfDayHours();

    if (!m_keyframes.empty() && m_keyframes.back().step == m_stepsDone)
    {
        // 同じステップに、もう1つ有る(最初の物の後に、建物を置いた、など)。新しい状態で置き換える。
        keyframe.structural = structural || m_keyframes.back().structural;
        m_bytesUsed -= m_keyframes.back().bytes.size();
        m_keyframes.back() = std::move(keyframe);
        m_bytesUsed += m_keyframes.back().bytes.size();
    }
    else
    {
        m_bytesUsed += keyframe.bytes.size();
        m_keyframes.push_back(std::move(keyframe));
    }
    ++m_keyframesTaken;
    m_lastKeyframeMs = ElapsedMs(start);
    EnforceLimits();
    return true;
}

void GameRewind::EnforceLimits()
{
    const uint64_t window = static_cast<uint64_t>((std::max)(1, m_settings.windowMinutes)) * kStepsPerMinute;
    const uint64_t latest = (m_state == GameRewindState::Live) ? m_stepsDone : m_latestStep;
    // 遡れる長さを超えた古いキーフレームを捨てる(窓の始まりを含む1つは、残す)。
    while (m_keyframes.size() >= 2 && latest >= window && m_keyframes[1].step <= latest - window)
    {
        m_bytesUsed -= m_keyframes.front().bytes.size();
        m_keyframes.erase(m_keyframes.begin());
    }
    // メモリの上限を超えたら、古い物から捨てる(遡れる長さが、短くなる)。
    const size_t cap = static_cast<size_t>((std::max)(1, m_settings.memoryCapMB)) * 1024u * 1024u;
    while (m_keyframes.size() >= 2 && m_bytesUsed > cap)
    {
        m_bytesUsed -= m_keyframes.front().bytes.size();
        m_keyframes.erase(m_keyframes.begin());
        m_message = "メモリの上限(" + std::to_string(m_settings.memoryCapMB) + "MB)を超えたので、古いキーフレームを捨てました(遡れる長さが短くなりました)";
    }
    // 古い記録(最初のキーフレームより前の指示・倍率)を捨てる。倍率は、最初のキーフレームの時点の値を決めるために、1つ手前まで残す。
    if (!m_keyframes.empty())
    {
        const uint64_t front = m_keyframes.front().step;
        while (m_scaleEntries.size() >= 2 && m_scaleEntries[1].atStep <= front)
        {
            m_scaleEntries.erase(m_scaleEntries.begin());
        }
        m_commandEntries.erase(std::remove_if(m_commandEntries.begin(), m_commandEntries.end(),
            [front](const GameRewindCommandEntry& entry) { return entry.atStep < front; }), m_commandEntries.end());
    }
}

float GameRewind::ScaleBefore(uint64_t step) const
{
    float scale = 1.0f;
    for (const GameRewindScaleEntry& entry : m_scaleEntries)
    {
        if (entry.atStep < step)
        {
            scale = entry.scale;
        }
        else
        {
            break;
        }
    }
    return scale;
}

int GameRewind::FindKeyframeAtOrBefore(uint64_t step) const
{
    int found = -1;
    for (size_t i = 0; i < m_keyframes.size(); ++i)
    {
        if (m_keyframes[i].step <= step)
        {
            found = static_cast<int>(i);
        }
        else
        {
            break;
        }
    }
    return found;
}

bool GameRewind::LoadKeyframe(const GameRewindKeyframe& keyframe)
{
    const SimRandomMode previous = m_demand->GetRandomMode();
    m_demand->SetRandomMode(SimRandomMode::RestoreState);
    std::string message;
    m_expectedLoad = true;
    const bool ok = m_scenario->LoadFromBytes(keyframe.bytes, "timeline", message, true);
    m_expectedLoad = false;
    m_demand->SetRandomMode(previous);
    if (!ok)
    {
        m_message = "キーフレームを読み込めません: " + message;
        return false;
    }
    m_lastSignature = m_signatureFunction();
    return true;
}

void GameRewind::LeaveLive()
{
    // Live を離れる時に、最新のステップのキーフレームを撮る(後で、最新へ戻れるように)。
    if (m_keyframes.empty() || m_keyframes.back().step != m_stepsDone)
    {
        CaptureKeyframe(false);
    }
    m_latestStep = m_stepsDone;
    m_userScale = m_clock->GetTimeScale();
    m_hasLatestFingerprint = true;
    m_latestRng = m_demand->GetRandomState();
    m_latestWeek = m_clock->GetWeekCount();
    m_latestHour = m_clock->GetTimeOfDayHours();
    m_latestMismatch = false;
    m_state = GameRewindState::Past;
}

void GameRewind::BeginSeek(uint64_t target, int keyframeIndex, bool resumeAfter)
{
    if (m_state == GameRewindState::Live)
    {
        LeaveLive();
    }
    m_seekTarget = (std::min)(target, m_latestStep);
    m_resumeAfterSeek = resumeAfter;
    m_seekStartTick = NowMilliseconds();
    m_seekStartClock = std::chrono::steady_clock::now();

    // 今の世界が、目的より手前の、確かな状態(進め直しが終わって止まっている)なら、読み込み直さずに、そこから進める。
    bool continueFromHere = (m_state == GameRewindState::Past) && !m_verifyActive && keyframeIndex < 0 && m_seekTarget >= m_stepsDone;
    if (continueFromHere)
    {
        // ただし、今から目的までの間に、構造の変更(建物・道路・入居など)のキーフレームがあれば、そこから進めても、その変更が入らない
        // (変更は、操作の記録ではなく、変更直後の状態のキーフレームで残しているため)。その場合は、キーフレームを読み込む。
        for (const GameRewindKeyframe& keyframe : m_keyframes)
        {
            if (keyframe.structural && keyframe.step >= m_stepsDone && keyframe.step <= m_seekTarget)
            {
                continueFromHere = false;
                break;
            }
        }
    }
    if (!continueFromHere)
    {
        int index = keyframeIndex >= 0 ? keyframeIndex : FindKeyframeAtOrBefore(m_seekTarget);
        if (index < 0)
        {
            m_message = "その時点は、もう残っていません(遡れる範囲の外です)";
            m_state = GameRewindState::Past;
            m_verifyActive = false;
            return;
        }
        // コピーしてから読み込む(読み込みの途中で、キーフレームの並びが変わっても、安全なように)。
        const GameRewindKeyframe keyframe = m_keyframes[static_cast<size_t>(index)];
        if (!LoadKeyframe(keyframe))
        {
            m_state = GameRewindState::Past;
            m_verifyActive = false;
            return;
        }
        m_stepsDone = keyframe.step;
        m_clock->SetTimeScale(ScaleBefore(keyframe.step));
    }
    // 入れる指示・倍率の位置を、今のステップに合わせる。
    m_scaleCursor = 0;
    while (m_scaleCursor < m_scaleEntries.size() && m_scaleEntries[m_scaleCursor].atStep < m_stepsDone)
    {
        ++m_scaleCursor;
    }
    m_commandCursor = 0;
    while (m_commandCursor < m_commandEntries.size() && m_commandEntries[m_commandCursor].atStep < m_stepsDone)
    {
        ++m_commandCursor;
    }
    m_state = GameRewindState::Seeking;
}

void GameRewind::ApplyInputsAt(uint64_t step)
{
    while (m_scaleCursor < m_scaleEntries.size() && m_scaleEntries[m_scaleCursor].atStep <= step)
    {
        m_clock->SetTimeScale(m_scaleEntries[m_scaleCursor].scale);
        ++m_scaleCursor;
    }
    while (m_commandCursor < m_commandEntries.size() && m_commandEntries[m_commandCursor].atStep <= step)
    {
        const GameRewindCommandEntry& entry = m_commandEntries[m_commandCursor];
        m_demand->SubmitTestCommandSilently(entry.command, entry.selectedResidentId, *m_objects);
        ++m_commandCursor;
    }
}

void GameRewind::RunSeekSlice()
{
    const auto start = std::chrono::steady_clock::now();
    const double budget = static_cast<double>((std::max)(1, m_settings.stepBudgetMilliseconds));
    while (true)
    {
        if (m_stepsDone >= m_seekTarget)
        {
            FinishSeek();
            return;
        }
        ApplyInputsAt(m_stepsDone);
        if (m_clock->GetTimeScale() <= 0.0f)
        {
            // 記録では、倍率が0の間は、ステップを進めていない。ここで0のまま進めないのは、記録との食い違い。
            m_message = "進め直しが、途中で止まりました(記録した倍率と食い違っています)";
            m_state = GameRewindState::Past;
            m_verifyActive = false;
            return;
        }
        m_stepFunction(kStepSeconds, true);
        ++m_stepsDone;
        if (ElapsedMs(start) >= budget)
        {
            return; // 続きは、次のフレームで
        }
    }
}

void GameRewind::FinishSeek()
{
    m_lastSeekMs = ElapsedMs(m_seekStartClock);
    if (m_verifyActive)
    {
        FinishVerifyStep();
        return;
    }
    m_state = GameRewindState::Past;
    if (m_resumeAfterSeek && m_stepsDone == m_latestStep)
    {
        // 最新へ戻った。記録した時の、乱数の状態・時刻と、一致するかを確かめてから、続きを始める。
        if (m_hasLatestFingerprint)
        {
            m_latestMismatch = !(m_demand->GetRandomState() == m_latestRng && m_clock->GetWeekCount() == m_latestWeek && m_clock->GetTimeOfDayHours() == m_latestHour);
        }
        ResumeLive();
        m_message = m_latestMismatch
            ? "最新へ戻りました。ただし、記録した時と、乱数の状態・時刻が一致しません(保存していない状態が、結果に効いている可能性があります)"
            : "最新へ戻りました(記録した時と、乱数の状態・時刻が一致しました)";
        return;
    }
    m_message = "過去(ステップ " + std::to_string(m_stepsDone) + ")で止まっています。「ここから再開」で、そこから新しい未来が始まります";
}

void GameRewind::ResumeLive()
{
    if (m_state != GameRewindState::Past)
    {
        return;
    }
    const uint64_t point = m_stepsDone;

    // それより後の、キーフレーム・指示・倍率を捨てる(新しい未来が始まる)。
    while (!m_keyframes.empty() && m_keyframes.back().step > point)
    {
        m_bytesUsed -= m_keyframes.back().bytes.size();
        m_keyframes.pop_back();
    }
    const float scaleAtPoint = (point == m_latestStep) ? m_userScale : ScaleBefore(point);
    m_scaleEntries.erase(std::remove_if(m_scaleEntries.begin(), m_scaleEntries.end(),
        [point](const GameRewindScaleEntry& entry) { return entry.atStep >= point; }), m_scaleEntries.end());
    m_commandEntries.erase(std::remove_if(m_commandEntries.begin(), m_commandEntries.end(),
        [point](const GameRewindCommandEntry& entry) { return entry.atStep >= point; }), m_commandEntries.end());

    float scale = scaleAtPoint;
    if (!(scale > 0.0f))
    {
        scale = m_userScale > 0.0f ? m_userScale : 1.0f;    // 止まった状態で、始まらないように
    }
    m_clock->SetTimeScale(scale);
    m_scaleEntries.push_back({ point, scale });
    m_lastLoggedScale = scale;
    m_latestStep = point;
    m_accumulator = 0.0f;
    m_structuralDirty = false;
    m_state = GameRewindState::Live;
    m_message = "ステップ " + std::to_string(point) + " から再開しました";
}

void GameRewind::BeginVerifyStep()
{
    // 区間 [i, i+1]: i番目のキーフレームを読み込み、i+1番目のステップまで進め直す。
    const GameRewindKeyframe& next = m_keyframes[static_cast<size_t>(m_verifyIndex) + 1];
    BeginSeek(next.step, m_verifyIndex, false);
    if (m_state != GameRewindState::Seeking)
    {
        m_verifyActive = false;
        m_verify.running = false;
    }
}

void GameRewind::FinishVerifyStep()
{
    const GameRewindKeyframe& next = m_keyframes[static_cast<size_t>(m_verifyIndex) + 1];
    const bool same = m_demand->GetRandomState() == next.rngState && m_clock->GetWeekCount() == next.week && m_clock->GetTimeOfDayHours() == next.hour;
    ++m_verify.checked;
    if (!same)
    {
        if (m_verify.mismatched == 0)
        {
            m_verify.firstMismatchStep = next.step;
        }
        ++m_verify.mismatched;
    }
    ++m_verifyIndex;
    if (static_cast<size_t>(m_verifyIndex) + 1 < m_keyframes.size())
    {
        BeginVerifyStep();
        return;
    }
    m_verifyActive = false;
    m_verify.running = false;
    m_verify.finished = true;
    m_state = GameRewindState::Past;
    m_message = m_verify.mismatched == 0
        ? "整合性の検査: " + std::to_string(m_verify.checked) + " 区間すべて、乱数の状態・時刻が一致しました"
        : "整合性の検査: " + std::to_string(m_verify.checked) + " 区間のうち、" + std::to_string(m_verify.mismatched) + " 区間が一致しませんでした(最初は、ステップ " + std::to_string(m_verify.firstMismatchStep) + " まで)";
}

void GameRewind::Update(float realDeltaSeconds, bool simulationActive)
{
    if (!m_enabled)
    {
        return;
    }
    if (m_needStart)
    {
        if (!simulationActive)
        {
            return;
        }
        StartRecording();
    }

    // ---- 画面からの要求 ----
    if (m_requestVerify)
    {
        m_requestVerify = false;
        if (m_keyframes.size() < 2)
        {
            m_message = "検査できるキーフレームが、まだ2つ以上ありません";
        }
        else if (!m_verifyActive)
        {
            if (m_state == GameRewindState::Live)
            {
                LeaveLive();
            }
            m_verify = GameRewindVerifyResult{};
            m_verify.running = true;
            m_verify.total = static_cast<int>(m_keyframes.size()) - 1;
            m_verifyActive = true;
            m_verifyIndex = 0;
            BeginVerifyStep();
        }
    }
    if (m_requestSeek)
    {
        m_requestSeek = false;
        if (!m_keyframes.empty() && !m_verifyActive)
        {
            BeginSeek(m_requestSeekStep, -1, false);
        }
    }
    if (m_requestLatest)
    {
        m_requestLatest = false;
        if (m_state == GameRewindState::Past || m_state == GameRewindState::Seeking)
        {
            if (!m_verifyActive)
            {
                // 最新へ: 最新のステップまで進め直して、そこから再開する。
                BeginSeek(m_latestStep, -1, true);
            }
        }
    }
    if (m_requestResume)
    {
        m_requestResume = false;
        if (m_state == GameRewindState::Past)
        {
            ResumeLive();
        }
    }

    if (m_state == GameRewindState::Seeking)
    {
        RunSeekSlice();
        return;
    }
    if (m_state == GameRewindState::Past)
    {
        // 過去を見ている間に、建物・道路を変えたら、そこから新しい未来を始める。
        if (simulationActive && m_signatureFunction() != m_lastSignature)
        {
            ResumeLive();
            m_structuralDirty = true;
        }
        else
        {
            return;
        }
    }

    // ---- Live ----
    if (!simulationActive)
    {
        m_wasEditing = true;    // Editing(Stop)の間は、時間を進めない。戻った時に、キーフレームを撮り直す
        return;
    }
    const uint64_t signature = m_signatureFunction();
    if (m_wasEditing)
    {
        m_structuralDirty = true;
        m_wasEditing = false;
    }
    if (signature != m_lastSignature || m_structuralDirty)
    {
        CaptureKeyframe(true);
        m_lastSignature = signature;
        m_structuralDirty = false;
    }

    float scale = m_clock->GetTimeScale();
    if (scale != m_lastLoggedScale)
    {
        m_scaleEntries.push_back({ m_stepsDone, scale });
        m_lastLoggedScale = scale;
    }
    if (!(scale > 0.0f))
    {
        m_accumulator = 0.0f;   // 時間が止まっている間は、ステップを進めない(止まっている時間で、遡れる長さを使わない)
        return;
    }

    m_accumulator += (std::min)(realDeltaSeconds, 0.1f);
    const int interval = (std::max)(1, m_settings.keyframeInterval);
    while (m_accumulator >= kStepSeconds)
    {
        m_accumulator -= kStepSeconds;
        m_stepFunction(kStepSeconds, false);
        ++m_stepsDone;
        if (m_stepsDone % static_cast<uint64_t>(interval) == 0)
        {
            CaptureKeyframe(false);
        }
        const float now = m_clock->GetTimeScale();
        if (now != m_lastLoggedScale)
        {
            // ステップの途中で、倍率が変わった(デバッグの一時停止の条件など)。記録して、止まったら、このフレームは、ここまで。
            m_scaleEntries.push_back({ m_stepsDone, now });
            m_lastLoggedScale = now;
            if (!(now > 0.0f))
            {
                m_accumulator = 0.0f;
                break;
            }
        }
    }
}
