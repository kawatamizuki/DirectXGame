#include "SimEndurance.h"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <windows.h>
#include <psapi.h>
#include "DemandSystem.h"
#include "GameClock.h"
#include "ScenarioManager.h"
#include "Debug.h"

#pragma comment(lib, "psapi.lib")

namespace
{
    double ElapsedSeconds(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    double ElapsedMilliseconds(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    // このプロセスが使っているメモリ(ワーキングセット)。MB。
    double CurrentWorkingSetMB()
    {
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        {
            return 0.0;
        }
        return static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
    }

    const char* AnomalyKey(SimAnomaly kind)
    {
        switch (kind)
        {
        case SimAnomaly::NonFiniteValue:            return "NonFiniteValue";
        case SimAnomaly::StaminaOutOfRange:         return "StaminaOutOfRange";
        case SimAnomaly::NeedOutOfRange:            return "NeedOutOfRange";
        case SimAnomaly::SatisfactionOutOfRange:    return "SatisfactionOutOfRange";
        case SimAnomaly::PenaltyOutOfRange:         return "PenaltyOutOfRange";
        case SimAnomaly::StopCountNegative:         return "StopCountNegative";
        case SimAnomaly::WaypointIndexOutOfRange:   return "WaypointIndexOutOfRange";
        case SimAnomaly::MissingTargetFacility:     return "MissingTargetFacility";
        case SimAnomaly::MissingHome:               return "MissingHome";
        case SimAnomaly::HomelessWithHome:          return "HomelessWithHome";
        case SimAnomaly::StopCountExceeded:         return "StopCountExceeded";
        case SimAnomaly::ArrivedWithoutMove:        return "ArrivedWithoutMove";
        case SimAnomaly::SettleWithoutArrival:      return "SettleWithoutArrival";
        case SimAnomaly::DuplicateFacilityInOuting: return "DuplicateFacilityInOuting";
        case SimAnomaly::MoveWhileVisiting:         return "MoveWhileVisiting";
        case SimAnomaly::ReturnWhileVisiting:       return "ReturnWhileVisiting";
        case SimAnomaly::NegativeRevenue:           return "NegativeRevenue";
        case SimAnomaly::SettledOnMissingFacility:  return "SettledOnMissingFacility";
        default:                                    return "Unknown";
        }
    }
}

void SimEndurance::Initialize(DemandSystem* demand, ScenarioManager* scenario, GameClock* clock, StepFunction stepFunction)
{
    m_demand = demand;
    m_scenario = scenario;
    m_clock = clock;
    m_stepFunction = std::move(stepFunction);
    m_demand->GetDebugRecorder().AddObserver(this);
}

int SimEndurance::GetWeeksDone() const
{
    return m_state == SimEnduranceState::Idle ? m_result.weeksDone : (m_clock->GetWeekCount() - m_startWeek);
}

double SimEndurance::GetWallSeconds() const
{
    return m_state == SimEnduranceState::Running ? ElapsedSeconds(m_runStart) : m_result.wallSeconds;
}

double SimEndurance::GetWorkingSetMB() const
{
    return CurrentWorkingSetMB();
}

void SimEndurance::OnEvent(const SimEvent& event)
{
    if (m_state != SimEnduranceState::Running || event.kind != SimEventKind::Anomaly)
    {
        return;
    }
    ++m_anomalyCount;
    const size_t index = static_cast<size_t>(event.anomaly);
    if (index < m_anomalyByKind.size())
    {
        ++m_anomalyByKind[index];
    }
    if (!m_firstAnomaly.valid)
    {
        m_firstAnomaly.valid = true;
        m_firstAnomaly.step = m_steps;
        m_firstAnomaly.week = event.week;
        m_firstAnomaly.hour = event.hour;
        m_firstAnomaly.residentId = event.residentId;
        m_firstAnomaly.kind = event.anomaly;
        m_firstAnomaly.value = event.a;
    }
}

void SimEndurance::NotifyWorldReplaced()
{
    if (m_expectedLoad || m_state == SimEnduranceState::Idle)
    {
        return;
    }
    m_state = SimEnduranceState::Idle;
    RestoreSeedSetting();   // 自動検証の途中なら、設定の種を、実行前の値へ戻す(読み込まれた街は、そのまま)
    m_message = "中止しました: 試験の途中で、街が読み込み直されました(ロードなど)";
}

bool SimEndurance::LoadBase()
{
    const SimRandomMode previous = m_demand->GetRandomMode();
    m_demand->SetRandomMode(SimRandomMode::RestoreState);
    std::string message;
    m_expectedLoad = true;
    const bool ok = m_scenario->LoadFromBytes(m_baseBytes, "endurance", message, true);
    m_expectedLoad = false;
    m_demand->SetRandomMode(previous);
    if (!ok)
    {
        m_message = "基点の街を読み込めません: " + message;
    }
    return ok;
}

bool SimEndurance::Begin(SimEnduranceMode mode)
{
    if (!m_demand->GetDebugRecorder().IsEnabled())
    {
        m_message = "記録がOFFです(住民デバッグの「記録する」をONにしてください。異常の検出は、記録の仕組みを使います)";
        return false;
    }
    std::string message;
    if (!m_scenario->CaptureBytes(m_baseBytes, message))
    {
        m_message = "基点を保存できません: " + message;
        return false;
    }
    m_userScale = m_clock->GetTimeScale();
    m_mode = mode;
    m_secondRun = false;
    m_firstRunSteps = 0;
    m_result = SimEnduranceResult{};
    m_result.mode = mode;
    m_samples.clear();
    m_wallBefore = 0.0;
    m_state = SimEnduranceState::Running;
    StartRun();
    return m_state == SimEnduranceState::Running;
}

void SimEndurance::StartRun()
{
    if (!LoadBase())
    {
        m_state = SimEnduranceState::Idle;
        return;
    }
    m_clock->SetTimeScale((std::max)(0.1f, m_settings.timeScale));
    m_steps = 0;
    m_startWeek = m_clock->GetWeekCount();
    m_anomalyCount = 0;
    m_anomalyByKind.fill(0);
    m_firstAnomaly = SimEnduranceAnomaly{};
    m_maxFrameMs = 0.0;
    m_workingSetStart = CurrentWorkingSetMB();
    m_workingSetPeak = m_workingSetStart;
    m_runStart = std::chrono::steady_clock::now();
    m_message = m_secondRun ? "決定論テスト: 2回目を進めています" : (m_mode == SimEnduranceMode::Determinism ? "決定論テスト: 1回目を進めています" : "耐久試験を進めています");
    if (!m_secondRun)
    {
        m_samples.clear();
    }
    Sample();
}

void SimEndurance::Sample()
{
    SimEnduranceSample sample;
    sample.step = m_steps;
    sample.weeksDone = m_clock->GetWeekCount() - m_startWeek;
    sample.wallSeconds = ElapsedSeconds(m_runStart);
    sample.workingSetMB = CurrentWorkingSetMB();
    sample.events = m_demand->GetDebugRecorder().GetStats().eventsRecordedTotal;
    sample.anomalies = m_anomalyCount;
    m_workingSetPeak = (std::max)(m_workingSetPeak, sample.workingSetMB);
    if (!m_secondRun)
    {
        m_samples.push_back(sample);
    }
}

void SimEndurance::Update(float /*realDeltaSeconds*/)
{
    if (m_requestCancel)
    {
        m_requestCancel = false;
        if (m_state == SimEnduranceState::Running)
        {
            if (m_mode == SimEnduranceMode::TestMapTrials)
            {
                RecordTrial("中止しました", false);
                ConcludeTrials("中止しました");
            }
            else
            {
                FinishRun("中止しました", false);
            }
        }
    }
    if (m_requestKeep)
    {
        m_requestKeep = false;
        if (m_state == SimEnduranceState::Held)
        {
            m_clock->SetTimeScale(m_userScale > 0.0f ? m_userScale : 1.0f);
            m_state = SimEnduranceState::Idle;
            m_message = "異常が出た状態のまま、ふだんのプレイに戻りました(時間は、試験の前の速度で進みます)";
        }
    }
    if (m_requestRestore)
    {
        m_requestRestore = false;
        if (m_state == SimEnduranceState::Held)
        {
            Restore();
        }
    }
    if (m_requestStart)
    {
        m_requestStart = false;
        if (m_state == SimEnduranceState::Idle)
        {
            Begin(m_requestMode);
        }
    }
    if (m_requestTrials)
    {
        m_requestTrials = false;
        if (m_state == SimEnduranceState::Idle)
        {
            BeginTrials();
        }
    }
    if (m_state == SimEnduranceState::Running)
    {
        RunSlice();
    }
}

void SimEndurance::RunSlice()
{
    const auto frameStart = std::chrono::steady_clock::now();
    const double budget = static_cast<double>((std::max)(1, m_settings.budgetMilliseconds));
    m_clock->SetTimeScale(ActiveTimeScale());
    while (true)
    {
        const int weeksDone = m_clock->GetWeekCount() - m_startWeek;
        if (m_mode == SimEnduranceMode::TestMapTrials)
        {
            // 自動検証: 1回の試行の終わり。記録して、次の試行へ(続きは、次のフレームで)。
            if (weeksDone >= m_trialConditions.weeks)
            {
                FinishTrial("指定の週数に達しました", true);
                return;
            }
            if (m_anomalyCount > 0 && m_trialConditions.stopTrialOnAnomaly)
            {
                FinishTrial("異常が出たので、この試行を止めました", false);
                return;
            }
            if (ElapsedSeconds(m_runStart) >= static_cast<double>(m_trialConditions.wallLimitSecondsPerTrial))
            {
                FinishTrial("実時間の上限に達したので、この試行を止めました", false);
                return;
            }
        }
        else if (m_secondRun)
        {
            if (m_steps >= m_firstRunSteps)
            {
                FinishRun("2回目を、1回目と同じステップ数まで進めました", true);
                return;
            }
        }
        else
        {
            if (weeksDone >= (std::max)(1, m_settings.weeks))
            {
                FinishRun("指定の週数に達しました", true);
                return;
            }
            if (m_anomalyCount > 0 && m_settings.stopOnAnomaly)
            {
                FinishRun("異常が出たので、止めました", false);
                return;
            }
        }
        if (m_mode != SimEnduranceMode::TestMapTrials &&
            ElapsedSeconds(m_runStart) + m_wallBefore >= static_cast<double>((std::max)(1, m_settings.wallLimitSeconds)))
        {
            FinishRun("実時間の上限に達したので、止めました", false);
            return;
        }

        m_stepFunction(kStepSeconds, true);
        ++m_steps;
        if (m_mode != SimEnduranceMode::TestMapTrials && m_settings.sampleIntervalSteps > 0 && (m_steps % static_cast<uint64_t>(m_settings.sampleIntervalSteps)) == 0)
        {
            Sample();
        }
        const double frameMs = ElapsedMilliseconds(frameStart);
        if (frameMs >= budget)
        {
            m_maxFrameMs = (std::max)(m_maxFrameMs, frameMs);
            return;     // 続きは、次のフレームで
        }
    }
}

void SimEndurance::FinishRun(const std::string& reason, bool completed)
{
    Sample();
    const double wall = ElapsedSeconds(m_runStart);

    if (!m_secondRun)
    {
        m_result.valid = true;
        m_result.mode = m_mode;
        m_result.stopReason = reason;
        m_result.completed = completed;
        m_result.steps = m_steps;
        m_result.weeksDone = m_clock->GetWeekCount() - m_startWeek;
        m_result.wallSeconds = wall;
        m_result.stepsPerSecond = wall > 0.0 ? static_cast<double>(m_steps) / wall : 0.0;
        m_result.gameSecondsPerRealSecond = wall > 0.0 ? static_cast<double>(m_steps) * kStepSeconds * m_settings.timeScale / wall : 0.0;
        m_result.averageStepMilliseconds = m_steps > 0 ? wall * 1000.0 / static_cast<double>(m_steps) : 0.0;
        m_result.maxFrameMilliseconds = m_maxFrameMs;
        m_result.workingSetStartMB = m_workingSetStart;
        m_result.workingSetEndMB = CurrentWorkingSetMB();
        m_result.workingSetPeakMB = (std::max)(m_workingSetPeak, m_result.workingSetEndMB);
        const SimRecorderStats recorder = m_demand->GetDebugRecorder().GetStats();
        m_result.events = recorder.eventsRecordedTotal;
        m_result.recorderBytes = recorder.estimatedBytes;
        m_result.anomalies = m_anomalyCount;
        m_result.anomalyByKind = m_anomalyByKind;
        m_result.firstAnomaly = m_firstAnomaly;
        m_result.seed = m_demand->GetActiveSeed();
        m_result.digest = m_demand->GetRunDigest();
        m_firstDigest = m_result.digest;

        if (m_mode == SimEnduranceMode::Determinism && completed)
        {
            // 1回目が終わった。同じ基点・同じ乱数の状態から、同じステップ数を、もう1回進める。
            m_firstRunSteps = m_steps;
            m_wallBefore = wall;
            m_secondRun = true;
            StartRun();
            return;
        }
    }
    else
    {
        m_result.determinismChecked = true;
        m_result.digestSecond = m_demand->GetRunDigest();
        m_result.determinismMatched = (m_result.digestSecond == m_firstDigest);
        if (!completed)
        {
            m_result.stopReason += "(2回目: " + reason + ")";
            m_result.determinismChecked = false;    // 2回目が最後まで進まなかったので、比べられない
        }
    }
    Conclude();
}

void SimEndurance::Conclude()
{
    WriteCsv();
    const bool keepAnomalyWorld = m_settings.stopOnAnomaly && m_result.anomalies > 0 && !m_result.completed && !m_secondRun;
    if (keepAnomalyWorld)
    {
        // 異常が出た状態を残して、見られるようにする(時間は止まっている)。
        m_state = SimEnduranceState::Held;
        m_message = "異常が出たので止めました。その状態を、住民デバッグ・行動統計で見られます(時間は止まっています)。「元の世界へ戻す」か「この状態で続ける」を選んでください";
        return;
    }
    Restore();
    if (m_result.determinismChecked)
    {
        m_message = m_result.determinismMatched ? "決定論テスト: 2回とも、指紋が一致しました(同じ条件で、同じ結果)" : "決定論テスト: 2回の指紋が一致しませんでした(同じ条件のはずなのに、結果が違います)";
    }
    else
    {
        m_message = "試験が終わりました: " + m_result.stopReason + "(街は、試験の前の状態へ戻しました)";
    }
}

bool SimEndurance::Restore()
{
    const bool ok = LoadBase();
    m_clock->SetTimeScale(m_userScale);
    m_state = SimEnduranceState::Idle;
    if (ok && (m_message.empty() || m_message.find("異常が出たので止めました") == 0))
    {
        m_message = "元の世界へ戻しました(試験の前の状態・時間の速度)";
    }
    return ok;
}

float SimEndurance::ActiveTimeScale() const
{
    const float scale = (m_mode == SimEnduranceMode::TestMapTrials) ? m_trialConditions.timeScale : m_settings.timeScale;
    return (std::max)(0.1f, scale);
}

// ---- テストマップの自動検証 ----

bool SimEndurance::RequestTrials(const std::string& trialBaseBytes, const SimTrialConditions& conditions, std::string& message)
{
    if (m_state != SimEnduranceState::Idle || m_requestStart || m_requestTrials)
    {
        message = "耐久試験・自動検証が動いている間は、始められません";
        return false;
    }
    const std::string problem = ValidateTrialConditions(conditions);
    if (!problem.empty())
    {
        message = "始められません: " + problem;
        return false;
    }
    if (trialBaseBytes.empty())
    {
        message = "始められません: テストマップの中身が空です";
        return false;
    }
    m_pendingTrialBytes = trialBaseBytes;
    m_pendingConditions = conditions;
    m_requestTrials = true;
    message = "自動検証を始めます";
    return true;
}

bool SimEndurance::BeginTrials()
{
    if (!m_demand->GetDebugRecorder().IsEnabled())
    {
        m_message = "記録がOFFです(住民デバッグの「記録する」をONにしてください。異常の検出と指紋は、記録の仕組みを使います)";
        return false;
    }
    // 実行前の街(終わったら戻す)を、メモリに控える。ファイルには書かない(通常のセーブを、変えない)。
    std::string message;
    if (!m_scenario->CaptureBytes(m_baseBytes, message))
    {
        m_message = "実行前の街を控えられません: " + message;
        return false;
    }
    m_trialBaseBytes = std::move(m_pendingTrialBytes);
    m_pendingTrialBytes.clear();
    m_trialConditions = m_pendingConditions;
    m_trialConditions.settings = m_demand->GetSettings();      // 使った設定値(記録だけ)
    m_trialSeeds = BuildTrialSeeds(m_trialConditions);
    m_trialIndex = 0;
    m_trialResults.clear();
    m_trialsValid = true;
    m_userScale = m_clock->GetTimeScale();
    m_mode = SimEnduranceMode::TestMapTrials;
    m_secondRun = false;
    m_wallBefore = 0.0;
    // 試行の間だけ、設定の種を、試行の種で置き換える(新しく生まれる住民の性格も、この種で決まる)。終わったら戻す。
    m_savedSettingsSeed = m_demand->GetSettings().seed;
    m_seedOverridden = true;
    m_state = SimEnduranceState::Running;
    if (!StartTrial())
    {
        ConcludeTrials("テストマップを読み込めませんでした");
        return false;
    }
    return true;
}

bool SimEndurance::LoadTrialBase(uint32_t seed)
{
    // 原本を、指定の種(FixedSeed)で読み込む。セーブに残っている乱数の状態は使わない(種だけで、乱数の列が決まる)。
    m_demand->GetSettings().seed = seed;
    const SimRandomMode previous = m_demand->GetRandomMode();
    m_demand->SetRandomMode(SimRandomMode::FixedSeed);
    std::string message;
    m_expectedLoad = true;
    const bool ok = m_scenario->LoadFromBytes(m_trialBaseBytes, "testmap-trial", message, true);
    m_expectedLoad = false;
    m_demand->SetRandomMode(previous);
    if (!ok)
    {
        m_message = "テストマップを読み込めません: " + message;
    }
    return ok;
}

bool SimEndurance::StartTrial()
{
    if (m_trialIndex >= m_trialSeeds.size() || !LoadTrialBase(m_trialSeeds[m_trialIndex]))
    {
        return false;
    }
    m_clock->SetTimeScale(ActiveTimeScale());
    m_steps = 0;
    m_startWeek = m_clock->GetWeekCount();
    m_anomalyCount = 0;
    m_anomalyByKind.fill(0);
    m_firstAnomaly = SimEnduranceAnomaly{};
    m_maxFrameMs = 0.0;
    m_runStart = std::chrono::steady_clock::now();
    m_message = "自動検証: " + std::to_string(m_trialIndex + 1) + " / " + std::to_string(m_trialSeeds.size()) + " 回目を進めています";
    return true;
}

void SimEndurance::RecordTrial(const std::string& reason, bool completed)
{
    SimTrialResult result;
    result.index = static_cast<int>(m_trialIndex);
    result.seed = m_trialIndex < m_trialSeeds.size() ? m_trialSeeds[m_trialIndex] : 0;
    result.determinismRun = m_trialConditions.determinismCheck && m_trialIndex + 1 == m_trialSeeds.size() && m_trialSeeds.size() >= 2;
    result.completed = completed;
    result.stopReason = reason;
    result.steps = m_steps;
    result.weeksDone = m_clock->GetWeekCount() - m_startWeek;
    result.wallSeconds = ElapsedSeconds(m_runStart);
    result.anomalies = m_anomalyCount;
    result.digest = m_demand->GetRunDigest();
    if (m_indicatorFunction)
    {
        result.indicators = m_indicatorFunction();
    }
    m_trialResults.push_back(result);
}

void SimEndurance::FinishTrial(const std::string& reason, bool completed)
{
    RecordTrial(reason, completed);
    ++m_trialIndex;
    if (m_trialIndex >= m_trialSeeds.size())
    {
        ConcludeTrials("全ての試行が終わりました");
        return;
    }
    if (!StartTrial())
    {
        ConcludeTrials("次の試行の、テストマップを読み込めませんでした");
    }
}

void SimEndurance::RestoreSeedSetting()
{
    if (m_seedOverridden)
    {
        m_demand->GetSettings().seed = m_savedSettingsSeed;
        m_seedOverridden = false;
    }
}

void SimEndurance::ConcludeTrials(const std::string& reason)
{
    // 先に、設定の種を戻す(実行前の街の、新しく生まれる住民の性格が、試行の種で決まらないように)。
    // 次に、実行前の街を、控えから戻す(乱数の状態・時間の倍率も、実行前のまま)。
    RestoreSeedSetting();
    const std::string failure = m_message;
    const bool restored = Restore();
    if (!restored)
    {
        m_message = "自動検証を終えましたが、実行前の街へ戻せませんでした(" + m_message + ")。F9などで、セーブを読み込み直してください";
        return;
    }
    std::string text = "自動検証: " + reason + "(" + std::to_string(m_trialResults.size()) + " 回)。街は、実行前の状態へ戻しました";
    if (reason.find("読み込") != std::string::npos && !failure.empty())
    {
        text += "。" + failure;
    }
    const SimTrialDeterminism determinism = EvaluateDeterminism(m_trialResults);
    if (determinism.checked)
    {
        text += determinism.matched ? "。決定論の確認: 同じ種の2回の指紋が、一致しました" : "。決定論の確認: 同じ種の2回の指紋が、一致しませんでした";
    }
    m_message = text;
}

void SimEndurance::WriteCsv()
{
    std::error_code ec;
    std::filesystem::create_directories(kOutputDirectory, ec);
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local);
    const std::string path = std::string(kOutputDirectory) + "/" + (m_mode == SimEnduranceMode::Determinism ? "determinism_" : "endurance_") + stamp + ".csv";
    std::ofstream out(path);
    if (!out)
    {
        m_message = "結果を保存できません: " + path;
        return;
    }
    const SimEnduranceResult& r = m_result;
    out << "key,value\n";
    out << "mode," << (r.mode == SimEnduranceMode::Determinism ? "determinism" : "endurance") << "\n";
    out << "stop_reason," << r.stopReason << "\n";
    out << "completed," << (r.completed ? 1 : 0) << "\n";
    out << "weeks_requested," << m_settings.weeks << "\n";
    out << "time_scale," << m_settings.timeScale << "\n";
    out << "random_seed," << r.seed << "\n";
    out << "steps," << r.steps << "\n";
    out << "weeks_done," << r.weeksDone << "\n";
    out << "wall_seconds," << r.wallSeconds << "\n";
    out << "steps_per_second," << r.stepsPerSecond << "\n";
    out << "game_seconds_per_real_second," << r.gameSecondsPerRealSecond << "\n";
    out << "average_step_ms," << r.averageStepMilliseconds << "\n";
    out << "max_frame_ms," << r.maxFrameMilliseconds << "\n";
    out << "working_set_start_mb," << r.workingSetStartMB << "\n";
    out << "working_set_end_mb," << r.workingSetEndMB << "\n";
    out << "working_set_peak_mb," << r.workingSetPeakMB << "\n";
    out << "events_recorded," << r.events << "\n";
    out << "recorder_bytes," << r.recorderBytes << "\n";
    out << "anomalies," << r.anomalies << "\n";
    for (size_t i = 1; i < r.anomalyByKind.size(); ++i)
    {
        if (r.anomalyByKind[i] > 0)
        {
            out << "anomaly_" << AnomalyKey(static_cast<SimAnomaly>(i)) << "," << r.anomalyByKind[i] << "\n";
        }
    }
    if (r.firstAnomaly.valid)
    {
        out << "first_anomaly_kind," << AnomalyKey(r.firstAnomaly.kind) << "\n";
        out << "first_anomaly_step," << r.firstAnomaly.step << "\n";
        out << "first_anomaly_week," << r.firstAnomaly.week << "\n";
        out << "first_anomaly_hour," << r.firstAnomaly.hour << "\n";
        out << "first_anomaly_resident," << r.firstAnomaly.residentId << "\n";
        out << "first_anomaly_value," << r.firstAnomaly.value << "\n";
        out << "reproduce,同じ基点・同じ乱数の種・同じ倍率で、先頭から first_anomaly_step ステップ進めると、同じ異常が出る\n";
    }
    char hex[32];
    std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(r.digest.hash));
    out << "digest," << hex << "\n";
    if (r.determinismChecked)
    {
        std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(r.digestSecond.hash));
        out << "digest_second," << hex << "\n";
        out << "determinism_matched," << (r.determinismMatched ? 1 : 0) << "\n";
    }
    out << "\nstep,weeks_done,wall_seconds,working_set_mb,events,anomalies\n";
    for (const SimEnduranceSample& s : m_samples)
    {
        out << s.step << "," << s.weeksDone << "," << s.wallSeconds << "," << s.workingSetMB << "," << s.events << "," << s.anomalies << "\n";
    }
    out.close();
    m_result.csvPath = path;
}
