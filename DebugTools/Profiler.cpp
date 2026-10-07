#include <Windows.h>
#include <Psapi.h>
#pragma comment(lib, "psapi.lib")

#include "Profiler.h"
#include "GameObject.h"
#include "RoadSystem.h"
#include "IOccupancyGrid.h"
#include "DemandSystem.h"
#include "RoadSegment.h"
#include "RoadNode.h"
#include "GridCoord.h"
#include "Vertex.h"
#include "imgui.h"

std::unordered_map<std::string, float> Profiler::s_sectionTimesMs;

void Profiler::BeginFrame()
{
    s_sectionTimesMs.clear();
}

void Profiler::RecordSection(const char* name, float milliseconds)
{
    s_sectionTimesMs[name] = milliseconds;
}

void Profiler::DrawDebugUI(
    const std::vector<GameObject>& objects,
    const RoadSystem& roadSystem,
    const IOccupancyGrid& occupancy,
    const DemandSystem& demand)
{
    // Debug/Hierarchy/Inspector/Editor Settings(既存パネル)と重ならない中央上部に配置する
    ImGui::SetNextWindowPos(ImVec2(310.0f, 350.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Profiler");

    ImGui::Text("-- Section times (ms) --");
    for (const auto& pair : s_sectionTimesMs)
    {
        ImGui::Text("%s: %.3f ms", pair.first.c_str(), pair.second);
    }

    ImGui::Separator();
    ImGui::Text("-- Object counts --");

    int envCount = 0;
    int buildingCount = 0;
    int roadCount = 0;
    int agentCount = 0;

    for (const GameObject& obj : objects)
    {
        switch (obj.kind)
        {
        case ObjectKind::Environment: envCount++;      break;
        case ObjectKind::Building:    buildingCount++; break;
        case ObjectKind::Road:        roadCount++;     break;
        case ObjectKind::Agent:       agentCount++;    break;
        }
    }

    ImGui::Text("Total: %zu", objects.size());
    ImGui::Text("Environment: %d", envCount);
    ImGui::Text("Building: %d", buildingCount);
    ImGui::Text("Road: %d", roadCount);
    ImGui::Text("Agent: %d", agentCount);

    ImGui::Separator();
    ImGui::Text("-- Memory --");

    PROCESS_MEMORY_COUNTERS memCounters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memCounters, sizeof(memCounters)))
    {
        float workingSetMB = static_cast<float>(memCounters.WorkingSetSize) / (1024.0f * 1024.0f);
        ImGui::Text("Working Set: %.2f MB", workingSetMB);
    }

    ImGui::Separator();
    ImGui::Text("-- Memory breakdown (estimate) --");
    ImGui::TextWrapped(
        "OSが実際に確保しているバイト数の正確な計測ではなく、"
        "「要素数 x 構造体のサイズ」で算出した推定値です。");

    // GameObjects: 建物・環境物・エージェントの見た目用オブジェクト一覧そのもののサイズ
    // (Model本体やテクスチャなど、GameObjectが指すだけのデータは含まない)。
    const size_t gameObjectBytes = objects.size() * sizeof(GameObject);

    // Roads: 配置済み区間・交差点ノード・道路メッシュの頂点データ。
    const std::vector<RoadSegment>& segments = roadSystem.GetSegments();
    const std::vector<RoadNode>& nodes = roadSystem.GetNodes();
    size_t nodeConnectionBytes = 0;
    for (const RoadNode& node : nodes)
    {
        nodeConnectionBytes += node.connectedSegmentIndices.size() * sizeof(size_t);
    }
    const size_t roadSegmentBytes = segments.size() * sizeof(RoadSegment);
    const size_t roadNodeBytes = nodes.size() * sizeof(RoadNode) + nodeConnectionBytes;
    const size_t roadMeshBytes = roadSystem.GetMeshVertexCount() * sizeof(Vertex);
    const size_t roadTotalBytes = roadSegmentBytes + roadNodeBytes + roadMeshBytes;

    // Field: 占有グリッドが覚えている占有セルの数。
    const size_t fieldBytes = occupancy.GetOccupiedCellCount() * sizeof(GridCoord);

    // Pathfinding/Demand: 経路探索グラフの規模 + 代表住民とその移動経路(waypoints)。
    DemandMemoryStats demandStats = demand.GetMemoryStats();
    const size_t demandAgentBytes = static_cast<size_t>(demandStats.agentCount) * sizeof(ResidentAgent);
    const size_t demandWaypointBytes = demandStats.totalWaypoints * sizeof(DirectX::XMFLOAT3);
    const size_t demandGraphBytes =
        static_cast<size_t>(demandStats.roadGraphNodeCount) * sizeof(int) +
        static_cast<size_t>(demandStats.roadGraphEdgeCount) * sizeof(int) * 2;
    const size_t demandTotalBytes = demandAgentBytes + demandWaypointBytes + demandGraphBytes;

    auto toKB = [](size_t bytes) { return static_cast<float>(bytes) / 1024.0f; };

    ImGui::Text("GameObjects: %.2f KB (%zu個)", toKB(gameObjectBytes), objects.size());
    ImGui::Text("Roads: %.2f KB (区間%zu, 交差点%zu, 頂点%zu)",
        toKB(roadTotalBytes), segments.size(), nodes.size(), roadSystem.GetMeshVertexCount());
    ImGui::Text("Field (occupancy): %.2f KB (占有セル%zu)",
        toKB(fieldBytes), occupancy.GetOccupiedCellCount());
    ImGui::Text("Pathfinding/Demand: %.2f KB (エージェント%d, waypoint%zu, グラフノード%d/エッジ%d)",
        toKB(demandTotalBytes), demandStats.agentCount, demandStats.totalWaypoints,
        demandStats.roadGraphNodeCount, demandStats.roadGraphEdgeCount);

    ImGui::End();
}

ScopedProfileSection::ScopedProfileSection(const char* name)
    : m_name(name)
    , m_start(std::chrono::steady_clock::now())
{
}

ScopedProfileSection::~ScopedProfileSection()
{
    auto end = std::chrono::steady_clock::now();
    float ms = std::chrono::duration<float, std::milli>(end - m_start).count();
    Profiler::RecordSection(m_name, ms);
}
