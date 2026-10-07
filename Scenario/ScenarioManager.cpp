#include "ScenarioManager.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "GameContext.h"
#include "Field.h"
#include "RoadSystem.h"
#include "BuildController.h"
#include "IOccupancyGrid.h"
#include "GridOrientationRegistry.h"
#include "GameObjectFactory.h"
#include "JsonFormat.h"
#include "ScenarioFile.h"
#include "Debug.h"
#include "imgui.h"

ScenarioManager::ScenarioManager()
    : m_defaultFormat(std::make_unique<JsonFormat>())
{
    m_format = m_defaultFormat.get();
    std::snprintf(m_pathBuffer, sizeof(m_pathBuffer), "%s", kQuickSavePath);
}

ScenarioManager::~ScenarioManager() = default;

void ScenarioManager::Initialize(
    GameContext* context, Field* field, RoadSystem* roadSystem, BuildController* buildController,
    IOccupancyGrid* occupancy, GridOrientationRegistry* orientationRegistry)
{
    m_context = context;
    m_field = field;
    m_roadSystem = roadSystem;
    m_buildController = buildController;
    m_occupancy = occupancy;
    m_orientationRegistry = orientationRegistry;
}

void ScenarioManager::RegisterSaveable(ISaveable* saveable)
{
    if (saveable)
    {
        m_saveables.push_back(saveable);
    }
}

void ScenarioManager::SetFormat(const ISnapshotFormat* format)
{
    m_format = format ? format : m_defaultFormat.get();
}

void ScenarioManager::CaptureSnapshot(WorldSnapshot& snapshot) const
{
    snapshot = WorldSnapshot();
    if (!m_context || !m_roadSystem || !m_orientationRegistry)
    {
        return;
    }

    // ---- 道路: 今の区間をそのままコピー(混雑の表示用のdebugLoadは保存しない) ----
    snapshot.roads = m_roadSystem->GetSegments();
    for (RoadSegment& road : snapshot.roads)
    {
        road.debugLoad = 0.0f;
    }

    // ---- 建物: 配列の添字がそのままuid(他の保存データが建物を指す時に使う) ----
    SaveContext saveContext;
    saveContext.game = m_context;
    if (m_context->objects)
    {
        int unsupportedCount = 0;
        for (const GameObject& obj : *m_context->objects)
        {
            if (obj.kind == ObjectKind::Building)
            {
                SnapshotBuilding building;
                building.type = obj.buildingType;
                building.transform = obj.transform;
                building.placement = obj.placement;

                saveContext.buildingUidByObjectId[obj.id] = static_cast<int>(snapshot.buildings.size());
                snapshot.buildings.push_back(building);
            }
            else if (obj.kind != ObjectKind::Environment && obj.kind != ObjectKind::Agent)
            {
                // 保存の対象に入っていない種類のオブジェクト。黙って欠落させないよう警告する
                // (新しい種類のオブジェクトを足した時に、保存処理を足し忘れていることに気づけるように)。
                unsupportedCount++;
            }
        }
        if (unsupportedCount > 0)
        {
            Debug::Warning("ScenarioManager: セーブ対象外の種類のオブジェクトが " + std::to_string(unsupportedCount) +
                           " 個あり、保存されません(新しい種類のオブジェクトの保存処理を足し忘れていませんか)");
        }
    }

    // ---- 青マス: 登録順(=優先順)のまま ----
    for (const GridOrientationRegistry::OrientedCell& cell : m_orientationRegistry->GetCells())
    {
        SnapshotOrientationCell entry;
        entry.origin = cell.origin;
        entry.yaw = cell.yaw;
        entry.cellX = cell.localCellX;
        entry.cellZ = cell.localCellZ;
        snapshot.orientationCells.push_back(entry);
    }

    // ---- システムごとの状態の区画 ----
    for (const ISaveable* saveable : m_saveables)
    {
        Json section = Json::MakeObject();
        section.Set("version", saveable->SaveVersion());
        saveable->Save(section, saveContext);
        snapshot.state.Set(saveable->SaveKey(), std::move(section));
    }
}

void ScenarioManager::ClearWorld()
{
    if (m_context && m_context->objects)
    {
        GameObjectFactory::DespawnAllOfKind(*m_context->objects, ObjectKind::Building);
        GameObjectFactory::DespawnAllOfKind(*m_context->objects, ObjectKind::Agent);
    }
    if (m_roadSystem)
    {
        m_roadSystem->Clear();
    }
    if (m_occupancy)
    {
        m_occupancy->Clear();
    }
    if (m_orientationRegistry)
    {
        m_orientationRegistry->Clear();
    }
    for (ISaveable* saveable : m_saveables)
    {
        saveable->ResetToDefault();
    }
}

bool ScenarioManager::ApplySnapshot(const WorldSnapshot& snapshot, std::string& message)
{
    if (!m_context || !m_field || !m_roadSystem || !m_buildController || !m_occupancy || !m_orientationRegistry)
    {
        message = "ScenarioManager が初期化されていません";
        return false;
    }

    ClearWorld();

    LoadContext loadContext;
    loadContext.game = m_context;

    // ---- 建物: そのまま生成し、占有に登録する(青マスは保存した一覧を別に戻すので登録しない) ----
    int failedBuildings = 0;
    for (const SnapshotBuilding& building : snapshot.buildings)
    {
        uint32_t objectId = m_buildController->SpawnBuilding(building.type, building.transform, building.placement, false);
        if (objectId == 0)
        {
            failedBuildings++;
        }
        // 失敗しても添字(uid)を保つため、0(未割当)を入れておく。
        loadContext.buildingObjectIds.push_back(objectId);
    }

    // ---- 道路: 区間をそのまま設定(検証・再生はしない)。ノード・占有・メッシュは作り直される ----
    m_roadSystem->RestoreSegments(snapshot.roads);

    // ---- 青マス: 保存した一覧(優先順)をそのまま戻す ----
    std::vector<GridOrientationRegistry::OrientedCell> cells;
    cells.reserve(snapshot.orientationCells.size());
    for (const SnapshotOrientationCell& entry : snapshot.orientationCells)
    {
        GridOrientationRegistry::OrientedCell cell;
        cell.origin = entry.origin;
        cell.yaw = entry.yaw;
        cell.localCellX = entry.cellX;
        cell.localCellZ = entry.cellZ;
        cells.push_back(cell);
    }
    m_orientationRegistry->ImportCells(cells, m_field->GetCellSize());

    // ---- システムごとの状態(建物・道路が復元された後に行う) ----
    for (ISaveable* saveable : m_saveables)
    {
        const Json* section = snapshot.state.Find(saveable->SaveKey());
        if (section && section->IsObject())
        {
            saveable->Load(*section, section->GetInt("version", 1), loadContext);
        }
        else
        {
            saveable->ResetToDefault(); // 古いセーブでこの区画が無い場合は、初期状態にする
        }
    }

    // 保存データに、登録されていないシステムの区画があれば知らせる(無視される)。
    for (const auto& member : snapshot.state.Members())
    {
        bool known = false;
        for (const ISaveable* saveable : m_saveables)
        {
            if (member.first == saveable->SaveKey())
            {
                known = true;
                break;
            }
        }
        if (!known)
        {
            Debug::Warning("ScenarioManager: 保存データの状態区画 \"" + member.first + "\" は対応するシステムが無いため無視しました");
        }
    }

    if (m_onWorldReplaced)
    {
        m_onWorldReplaced();
    }

    message = "道路 " + std::to_string(snapshot.roads.size()) +
              " / 建物 " + std::to_string(snapshot.buildings.size()) +
              " / 青マス " + std::to_string(snapshot.orientationCells.size());
    if (failedBuildings > 0)
    {
        message += " (建物 " + std::to_string(failedBuildings) + " 件の生成に失敗)";
    }
    return true;
}

bool ScenarioManager::ReadFileBytes(const std::string& path, std::string& bytes, std::string& error) const
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "ファイルを開けません: " + path;
        return false;
    }
    bytes.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad())
    {
        error = "ファイルの読み込み中にエラーが起きました: " + path;
        return false;
    }
    return true;
}

bool ScenarioManager::Save(const std::string& path, std::string& message)
{
    WorldSnapshot snapshot;
    CaptureSnapshot(snapshot);

    Json root = ScenarioFile::SnapshotToJson(snapshot);

    std::string bytes;
    std::string error;
    if (!m_format->Encode(root, bytes, error))
    {
        message = "保存に失敗しました: " + error;
        m_lastMessage = message;
        return false;
    }

    // 一時ファイルに書き切ってから置き換える(書き込み中に落ちても、前のセーブが壊れない)。
    std::error_code ec;
    std::filesystem::path target(path);
    if (target.has_parent_path())
    {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec)
        {
            message = "保存に失敗しました: フォルダを作れません: " + target.parent_path().string();
            m_lastMessage = message;
            return false;
        }
    }

    std::string tempPath = path + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            message = "保存に失敗しました: 一時ファイルを作れません: " + tempPath;
            m_lastMessage = message;
            return false;
        }
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        if (!file)
        {
            message = "保存に失敗しました: 書き込みに失敗しました(ディスクの空きを確認してください)";
            m_lastMessage = message;
            file.close();
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }

    std::filesystem::rename(tempPath, target, ec);
    if (ec)
    {
        message = "保存に失敗しました: ファイルを置き換えられません: " + ec.message();
        m_lastMessage = message;
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    message = "保存しました: " + path + " (道路 " + std::to_string(snapshot.roads.size()) +
              " / 建物 " + std::to_string(snapshot.buildings.size()) +
              " / 青マス " + std::to_string(snapshot.orientationCells.size()) +
              "、" + std::to_string(bytes.size()) + " バイト)";
    m_lastMessage = message;
    Debug::Log("ScenarioManager: " + message);
    return true;
}

bool ScenarioManager::Load(const std::string& path, std::string& message)
{
    auto startTime = std::chrono::steady_clock::now();

    // ---- 1. 読み込み・検証(ここまでは今の街に一切触れない) ----
    std::string bytes;
    std::string error;
    if (!ReadFileBytes(path, bytes, error))
    {
        message = "読み込めません: " + error;
        m_lastMessage = message;
        return false;
    }

    if (!m_format->Matches(bytes))
    {
        message = "読み込めません: " + path + " は対応している形式のセーブデータではありません";
        m_lastMessage = message;
        return false;
    }

    Json root;
    if (!m_format->Decode(bytes, root, error))
    {
        message = "読み込めません: " + path + " の内容が壊れています(" + error + ")";
        m_lastMessage = message;
        return false;
    }

    WorldSnapshot snapshot;
    std::vector<std::string> warnings;
    if (!ScenarioFile::JsonToSnapshot(root, snapshot, warnings, error))
    {
        message = "読み込めません: " + path + " (" + error + ")";
        m_lastMessage = message;
        return false;
    }
    for (const std::string& warning : warnings)
    {
        Debug::Warning("ScenarioManager: " + warning);
    }

    // ---- 2. 検証が通ったので、街を置き換える ----
    std::string summary;
    if (!ApplySnapshot(snapshot, summary))
    {
        message = "読み込めません: " + summary;
        m_lastMessage = message;
        return false;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime);
    message = "読み込みました: " + path + " (" + summary + "、" + std::to_string(elapsed.count()) + " ms";
    if (!warnings.empty())
    {
        message += "、警告 " + std::to_string(warnings.size()) + " 件";
    }
    message += ")";
    m_lastMessage = message;
    Debug::Log("ScenarioManager: " + message);
    return true;
}

bool ScenarioManager::LoadStartScenario()
{
    std::error_code ec;
    if (!std::filesystem::exists(kStartScenarioPath, ec))
    {
        return false; // 初期マップが無ければ、空の街のまま始める
    }

    std::string message;
    return Load(kStartScenarioPath, message);
}

void ScenarioManager::DrawDebugUI()
{
    // 他の常設パネル(Time/Build/City Stats/Profiler/Log)と重ならない位置に置く
    ImGui::SetNextWindowPos(ImVec2(740.0f, 72.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scenario");

    ImGui::InputText("Path", m_pathBuffer, sizeof(m_pathBuffer));

    std::string message;
    if (ImGui::Button("Save"))
    {
        Save(m_pathBuffer, message);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load"))
    {
        Load(m_pathBuffer, message);
    }

    // 今の街を、ゲーム開始時の初期マップ(Git管理する)として保存する/読み込む。
    if (ImGui::Button("Save as tutorial map"))
    {
        Save(kStartScenarioPath, message);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load tutorial map"))
    {
        Load(kStartScenarioPath, message);
    }

    ImGui::TextDisabled("F5: quick save / F9: quick load (%s)", kQuickSavePath);
    if (!m_lastMessage.empty())
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_lastMessage.c_str());
    }

    ImGui::End();
}
