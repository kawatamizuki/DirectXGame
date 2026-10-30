#include "SimReproController.h"
#include "DemandSystem.h"
#include "GameClock.h"
#include "GameObject.h"
#include "ScenarioManager.h"
#include "Debug.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "Json.h"

void SimReproController::Initialize(DemandSystem* demand, ScenarioManager* scenario, GameClock* clock, std::vector<GameObject>* objects)
{
    m_demand = demand;
    m_scenario = scenario;
    m_clock = clock;
    m_objects = objects;

    // 画面からの行動テストの指示を、記録中なら、フレーム番号つきで残す。再生中の指示は、SubmitTestCommandSilently なので、ここには来ない。
    m_demand->SetOnTestCommandSubmitted([this](const SimTestCommand& command, uint32_t selectedResidentId)
    {
        if (m_state == SimReproState::Recording)
        {
            SimReproCommand entry;
            entry.frame = m_frame + 1;      // 押した時点の次のフレームの更新の前に、入る
            entry.command = command;
            entry.selectedResidentId = selectedResidentId;
            m_commands.push_back(entry);
        }
    });
}

bool SimReproController::Matches() const
{
    return m_recorded.valid && m_replayed.valid && m_recorded.frames == m_replayed.frames && m_recorded.digest == m_replayed.digest;
}

int SimReproController::FirstDivergence() const
{
    if (!m_recorded.valid || !m_replayed.valid)
    {
        return -1;
    }
    return SimFirstDivergence(m_recorded.trace, m_replayed.trace);
}

SimReproRunSummary SimReproController::Capture() const
{
    SimReproRunSummary summary;
    summary.valid = true;
    summary.frames = m_frame;
    summary.digest = m_demand->GetRunDigest();
    summary.week = m_clock->GetWeekCount();
    summary.hour = m_clock->GetTimeOfDayHours();
    summary.seed = m_demand->GetActiveSeed();
    summary.randomState = m_demand->GetRandomState();
    summary.trace = m_demand->GetRunDigestTrace();
    return summary;
}

void SimReproController::Stop(const std::string& message)
{
    if ((m_state == SimReproState::Replaying || m_state == SimReproState::Held) && m_clock)
    {
        // 再生が決めていた倍率を、再生を始める前の値に戻す(0のまま残ると、時間が動かず、ゲームが止まったままになる)。
        m_clock->SetTimeScale(m_scaleBeforeReplay);
    }
    m_state = SimReproState::Idle;
    m_userPaused = false;
    m_message = message;
    Debug::Info("SimRepro: " + message);
}

bool SimReproController::LoadSnapshotWithRandomMode(int mode)
{
    const SimRandomMode previous = m_demand->GetRandomMode();
    m_demand->SetRandomMode(mode == 0 ? SimRandomMode::RestoreState : SimRandomMode::Reseed);
    std::string message;
    m_expectedLoad = true;
    const bool ok = m_scenario->Load(kSnapshotPath, message);
    m_expectedLoad = false;
    m_demand->SetRandomMode(previous);
    if (!ok)
    {
        m_message = "スナップショットを読み込めません: " + message;
    }
    return ok;
}

void SimReproController::NotifyWorldReplaced()
{
    if (m_state != SimReproState::Idle && !m_expectedLoad)
    {
        Stop("中止しました: 記録・再生の途中で、街が読み込み直されました(ロードなど)");
    }
}

float SimReproController::PrepareFrame(float realDeltaSeconds, bool simulationActive, uint64_t structureSignature)
{
    if (!m_demand)
    {
        return realDeltaSeconds;
    }

    // ---- 画面からの要求 ----
    if (m_requestCancel)
    {
        m_requestCancel = false;
        if (m_state != SimReproState::Idle)
        {
            Stop("中止しました");
        }
    }
    if (m_requestResume)
    {
        m_requestResume = false;
        if (m_state == SimReproState::Held)
        {
            Stop("続きを実行します(再生を始める前の速度で、ふだんのプレイに戻りました)");
            return realDeltaSeconds;
        }
    }
    if (m_requestStop)
    {
        m_requestStop = false;
        if (m_state == SimReproState::Recording)
        {
            m_recorded = Capture();
            Stop("記録を止めました(" + std::to_string(m_frame) + " フレーム。指示 " + std::to_string(m_commands.size()) + " 件)");
            return realDeltaSeconds;
        }
    }
    if (m_requestBegin)
    {
        m_requestBegin = false;
        if (m_state != SimReproState::Idle)
        {
            m_message = "実行中の記録・再生を、先に止めてください";
        }
        else if (!simulationActive)
        {
            m_message = "ゲームシーンで、Playingモードの時だけ、記録できます";
        }
        else
        {
            std::string saveMessage;
            if (!m_scenario->Save(kSnapshotPath, saveMessage))
            {
                m_message = "スナップショットを保存できません: " + saveMessage;
            }
            else if (LoadSnapshotWithRandomMode(0))
            {
                // 保存した状態を読み込み直してから始める(記録する実行も、再生する実行も、同じ「読み込んだ状態」から始まる)。
                m_hasSnapshot = true;
                m_state = SimReproState::Recording;
                m_frame = 0;
                m_signature = structureSignature;
                m_commands.clear();
                m_scaleChanges.clear();
                m_recorded = SimReproRunSummary{};
                m_replayed = SimReproRunSummary{};
                m_lastScale = -1.0f;        // 最初のフレームで、必ず倍率を記録する
                m_message = "記録を始めました(スナップショットを保存して、読み込み直しました)";
            }
        }
    }
    if (m_requestReplay)
    {
        m_requestReplay = false;
        if (m_state != SimReproState::Idle)
        {
            m_message = "実行中の記録・再生を、先に止めてください";
        }
        else if (!m_hasSnapshot || !m_recorded.valid)
        {
            m_message = "再生できる記録がありません(先に、記録を始めて、止めてください)";
        }
        else if ((m_scaleBeforeReplay = (m_clock->GetTimeScale() > 0.0f ? m_clock->GetTimeScale() : 1.0f)) > 0.0f // 読み込みの前の倍率を覚える。止まっていても、再生の後は、動く状態に戻す
                 && LoadSnapshotWithRandomMode(m_requestReplayRandom == SimReplayRandom::SameState ? 0 : 1))
        {
            m_state = SimReproState::Replaying;
            m_lastReplayRandom = m_requestReplayRandom;
            m_frame = 0;
            m_endFrame = m_recorded.frames;
            m_signature = structureSignature;
            m_nextCommand = 0;
            m_nextScale = 0;
            m_replayed = SimReproRunSummary{};
            m_message = "再生を始めました(スナップショットへ戻しました)";
        }
    }

    if (m_state == SimReproState::Idle)
    {
        return realDeltaSeconds;
    }

    if (m_state == SimReproState::Held)
    {
        // 最終フレームで止まっている間は、時計に渡す時間を0にする(時間が進まない)。操作(建物・道路・モード切り替え)をしたら、止めるのをやめる。
        if (!simulationActive || structureSignature != m_signature)
        {
            Stop("再生の停止を解除しました(街が変わったので、ふだんのプレイに戻りました)");
            return realDeltaSeconds;
        }
        return 0.0f;
    }

    // ---- 記録・再生の最中 ----
    if (!simulationActive)
    {
        Stop("中止しました: ゲームシーンのPlayingモードでなくなりました");
        return realDeltaSeconds;
    }
    if (structureSignature != m_signature)
    {
        Stop("中止しました: 建物・道路が変わりました(記録していない操作が入ると、再現できません)");
        return realDeltaSeconds;
    }

    if (m_state == SimReproState::Replaying && m_userPaused)
    {
        // 再生中に、メニューなどで時間を止めている間は、再生も止まる(ふだんのプレイと同じ。フレームは進めず、時計に渡す時間も0)。
        return 0.0f;
    }

    if (m_state == SimReproState::Replaying && m_frame >= m_endFrame)
    {
        // 記録と同じフレーム数を進めた。結果の指紋を取って、止める(時間も止める。結果を見られるように)。
        // 記録と同じフレーム数を進めた。結果の指紋を取って、**最終フレームで止まる**(結果を見られるように)。「続きを実行」で、続きを遊べる。
        m_replayed = Capture();
        m_state = SimReproState::Held;
        m_userPaused = false;
        m_message = Matches() ? "再生が終わりました: 記録と同じ結果になりました。最終フレームで止まっています" : "再生が終わりました: 記録と違う結果になりました。最終フレームで止まっています";
        Debug::Info("SimRepro: " + m_message);
        return 0.0f;
    }

    ++m_frame;
    if (m_state == SimReproState::Recording)
    {
        const float scale = m_clock->GetTimeScale();
        if (scale != m_lastScale)
        {
            m_scaleChanges.push_back({ m_frame, scale });
            m_lastScale = scale;
        }
    }
    else
    {
        // 再生: このフレームの更新の前に、記録した倍率・指示を入れる。
        while (m_nextScale < m_scaleChanges.size() && m_scaleChanges[m_nextScale].frame <= m_frame)
        {
            m_clock->SetTimeScale(m_scaleChanges[m_nextScale].scale);
            ++m_nextScale;
        }
        while (m_nextCommand < m_commands.size() && m_commands[m_nextCommand].frame <= m_frame)
        {
            const SimReproCommand& entry = m_commands[m_nextCommand];
            m_demand->SubmitTestCommandSilently(entry.command, entry.selectedResidentId, *m_objects);
            ++m_nextCommand;
        }
    }
    return kFixedStepSeconds;
}

// ---------------------------------------------------------------------------
// 記録した区間の保存・読み込み
// ---------------------------------------------------------------------------
namespace
{
    std::string SegmentPath(const std::string& name)
    {
        // ファイル名に使えない文字は、_ にする(フォルダを抜ける名前・記号を、入れさせない)。
        std::string safe;
        for (char c : name)
        {
            const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
            safe += ok ? c : '_';
        }
        if (safe.empty())
        {
            safe = "segment";
        }
        return std::string(SimReproController::kSegmentDirectory) + "/" + safe + ".json";
    }

    Json CommandToJson(const SimReproCommand& entry)
    {
        const SimTestCommand& c = entry.command;
        Json item = Json::MakeObject();
        item.Set("frame", static_cast<double>(entry.frame));
        item.Set("selectedResidentId", static_cast<double>(entry.selectedResidentId));
        item.Set("action", static_cast<int>(c.action));
        item.Set("mode", static_cast<int>(c.mode));
        item.Set("target", static_cast<int>(c.target));
        item.Set("rectMinX", c.rectMinX);
        item.Set("rectMinZ", c.rectMinZ);
        item.Set("rectMaxX", c.rectMaxX);
        item.Set("rectMaxZ", c.rectMaxZ);
        item.Set("maxCount", c.maxCount);
        item.Set("facilityId", static_cast<double>(c.facilityId));
        item.Set("forcedStopovers", c.forcedStopovers);
        item.Set("staminaValue", c.staminaValue);
        return item;
    }

    SimReproCommand CommandFromJson(const Json& item)
    {
        SimReproCommand entry;
        entry.frame = static_cast<uint64_t>(item.GetNumber("frame", 0.0));
        entry.selectedResidentId = static_cast<uint32_t>(item.GetNumber("selectedResidentId", 0.0));
        SimTestCommand& c = entry.command;
        c.action = static_cast<SimTestAction>(item.GetInt("action", 0));
        c.mode = static_cast<SimTestMode>(item.GetInt("mode", 0));
        c.target = static_cast<SimTestTarget>(item.GetInt("target", 0));
        c.rectMinX = item.GetFloat("rectMinX", 0.0f);
        c.rectMinZ = item.GetFloat("rectMinZ", 0.0f);
        c.rectMaxX = item.GetFloat("rectMaxX", 0.0f);
        c.rectMaxZ = item.GetFloat("rectMaxZ", 0.0f);
        c.maxCount = item.GetInt("maxCount", 50);
        c.facilityId = static_cast<uint32_t>(item.GetNumber("facilityId", 0.0));
        c.forcedStopovers = item.GetInt("forcedStopovers", 0);
        c.staminaValue = item.GetFloat("staminaValue", 100.0f);
        return entry;
    }

    Json SummaryToJson(const SimReproRunSummary& s)
    {
        Json item = Json::MakeObject();
        item.Set("frames", static_cast<double>(s.frames));
        item.Set("events", static_cast<double>(s.digest.events));
        item.Set("rolls", static_cast<double>(s.digest.rolls));
        char hex[32];
        std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(s.digest.hash));
        item.Set("hash", std::string(hex)); // 64bitは、数値(double)に収まらないので、文字列で持つ
        item.Set("week", s.week);
        item.Set("hour", s.hour);
        item.Set("seed", static_cast<double>(s.seed));
        item.Set("randomState", static_cast<double>(s.randomState));
        return item;
    }

    SimReproRunSummary SummaryFromJson(const Json& item)
    {
        SimReproRunSummary s;
        s.valid = true;
        s.frames = static_cast<uint64_t>(item.GetNumber("frames", 0.0));
        s.digest.events = static_cast<uint64_t>(item.GetNumber("events", 0.0));
        s.digest.rolls = static_cast<uint64_t>(item.GetNumber("rolls", 0.0));
        s.digest.hash = std::strtoull(item.GetString("hash", "0").c_str(), nullptr, 16);
        s.week = item.GetInt("week", 0);
        s.hour = item.GetFloat("hour", 0.0f);
        s.seed = static_cast<uint32_t>(item.GetNumber("seed", 0.0));
        s.randomState = static_cast<uint32_t>(item.GetNumber("randomState", 0.0));
        return s;
    }
}

bool SimReproController::SaveSegment(const std::string& name, std::string& message)
{
    if (m_state != SimReproState::Idle && m_state != SimReproState::Held)
    {
        message = "記録中・再生中は、保存できません";
        return false;
    }
    if (!m_hasSnapshot || !m_recorded.valid)
    {
        message = "保存できる記録がありません(先に、記録を始めて、止めてください)";
        return false;
    }
    std::ifstream snapshotFile(kSnapshotPath, std::ios::binary);
    if (!snapshotFile)
    {
        message = std::string("スナップショットを読めません: ") + kSnapshotPath;
        return false;
    }
    std::stringstream snapshotText;
    snapshotText << snapshotFile.rdbuf();

    Json root = Json::MakeObject();
    root.Set("version", 1);
    root.Set("name", name);
    root.Set("frames", static_cast<double>(m_recorded.frames));
    root.Set("recorded", SummaryToJson(m_recorded));
    Json scales = Json::MakeArray();
    for (const SimReproScaleChange& change : m_scaleChanges)
    {
        Json item = Json::MakeObject();
        item.Set("frame", static_cast<double>(change.frame));
        item.Set("scale", change.scale);
        scales.Push(item);
    }
    root.Set("scaleChanges", scales);
    Json commands = Json::MakeArray();
    for (const SimReproCommand& entry : m_commands)
    {
        commands.Push(CommandToJson(entry));
    }
    root.Set("commands", commands);
    root.Set("snapshot", snapshotText.str());   // スナップショット(セーブデータ)を、文字列のまま入れる

    std::error_code ec;
    std::filesystem::create_directories(kSegmentDirectory, ec);
    const std::string path = SegmentPath(name);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        message = "保存できません: " + path;
        return false;
    }
    const std::string text = root.Dump(true);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    message = "区間を保存しました: " + path + "(" + std::to_string(m_recorded.frames) + " フレーム、指示 " + std::to_string(m_commands.size()) + " 件)";
    return true;
}

bool SimReproController::LoadSegment(const std::string& name, std::string& message)
{
    if (m_state != SimReproState::Idle && m_state != SimReproState::Held)
    {
        message = "記録中・再生中は、読み込めません";
        return false;
    }
    const std::string path = SegmentPath(name);
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        message = "読み込めません: " + path;
        return false;
    }
    std::stringstream text;
    text << in.rdbuf();
    Json root;
    std::string error;
    if (!Json::Parse(text.str(), root, error) || !root.IsObject())
    {
        message = "読み込めません: " + path + " の内容が壊れています(" + error + ")";
        return false;
    }
    const Json* snapshot = root.Find("snapshot");
    const Json* recorded = root.Find("recorded");
    if (!snapshot || !snapshot->IsString() || snapshot->AsString().empty() || !recorded || !recorded->IsObject())
    {
        message = "読み込めません: " + path + " には、スナップショットまたは結果がありません";
        return false;
    }

    // スナップショットを、再生で読み込むファイルへ書き出す。
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(kSnapshotPath).parent_path(), ec);
    std::ofstream snapshotOut(kSnapshotPath, std::ios::binary | std::ios::trunc);
    if (!snapshotOut)
    {
        message = std::string("スナップショットを書き出せません: ") + kSnapshotPath;
        return false;
    }
    snapshotOut.write(snapshot->AsString().data(), static_cast<std::streamsize>(snapshot->AsString().size()));
    snapshotOut.close();

    m_commands.clear();
    m_scaleChanges.clear();
    if (const Json* scales = root.Find("scaleChanges"))
    {
        for (const Json& item : scales->Items())
        {
            SimReproScaleChange change;
            change.frame = static_cast<uint64_t>(item.GetNumber("frame", 0.0));
            change.scale = item.GetFloat("scale", 1.0f);
            m_scaleChanges.push_back(change);
        }
    }
    if (const Json* commands = root.Find("commands"))
    {
        for (const Json& item : commands->Items())
        {
            m_commands.push_back(CommandFromJson(item));
        }
    }
    // 古い順に並べておく(再生は、フレーム番号の小さい順に入れる)。
    std::stable_sort(m_commands.begin(), m_commands.end(), [](const SimReproCommand& a, const SimReproCommand& b) { return a.frame < b.frame; });
    std::stable_sort(m_scaleChanges.begin(), m_scaleChanges.end(), [](const SimReproScaleChange& a, const SimReproScaleChange& b) { return a.frame < b.frame; });
    m_recorded = SummaryFromJson(*recorded);
    m_replayed = SimReproRunSummary{};
    m_hasSnapshot = true;
    message = "区間を読み込みました: " + path + "(" + std::to_string(m_recorded.frames) + " フレーム、指示 " + std::to_string(m_commands.size()) + " 件)";
    m_message = message;
    return true;
}

std::vector<std::string> SimReproController::ListSegments() const
{
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(kSegmentDirectory, ec))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}
