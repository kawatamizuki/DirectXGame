#include <cfloat>
#include <algorithm>
#include <sstream>
#include "DemandSystem.h"
#include "GameContext.h"
#include "GameClock.h"
#include "GameObjectFactory.h"
#include "BuildingType.h"
#include "RoadGraph.h"
#include "IPathfinder.h"
#include "RoadProximity.h"
#include "DebugRenderer.h"
#include "Profiler.h"
#include "imgui.h"

using namespace DirectX;

namespace
{
    // 建物から道路までの距離がこれを超える場合、「道路に接続していない」とみなす。
    // 「隣接している道だけを使う」という意図に近づけるため、グラフノードまでの距離ではなく
    // 道路区間そのものまでの距離(FindNearestPointOnRoad)に対して使う、狭めのしきい値。
    constexpr float kAdjacentDistance = 1.5f;

    // Houseの位置とidだけを持つ軽量なコピー。
    // GameObject*のまま保持すると、途中でGameObjectFactory::Spawnが
    // *context.objectsを再確保した際にダングリングポインタになるため、
    // 必要な値だけをティック開始時にコピーしておく。
    struct BuildingSnapshot
    {
        uint32_t id;                         // 建物のGameObject::id
        XMFLOAT3 position;                   // 建物の位置
        bool hasRoadConnection = false;      // 道路に接続できているか(kAdjacentDistance以内に道路があるか)
        XMFLOAT3 connectionPoint{};          // 接続先の道路上の最近接点(入口/私道の視覚化に使う)
        int nearestNode = -1;                // 経路探索用の最寄りグラフノード(hasRoadConnection==trueの時だけ有効)
    };

    // buildingの位置から一番近い道路区間上の点を探し、接続情報を組み立てる。
    // House/Office両方の建物リスト作成時に使う共通処理。
    BuildingSnapshot MakeBuildingSnapshot(uint32_t id, const XMFLOAT3& position, const std::vector<RoadSegment>& segments, const RoadGraph& graph)
    {
        BuildingSnapshot snapshot;
        snapshot.id = id;
        snapshot.position = position;

        std::optional<NearestRoadPoint> nearestRoad = FindNearestPointOnRoad(segments, position, kAdjacentDistance);
        if (!nearestRoad.has_value())
        {
            return snapshot; // hasRoadConnection == falseのまま
        }

        snapshot.hasRoadConnection = true;
        snapshot.connectionPoint = nearestRoad->point;

        // 接続先の区間の、position側に近い方の端点をそのまま経路探索用のノードとする(簡略化)。
        const RoadSegment& segment = segments[nearestRoad->segmentIndex];
        XMVECTOR posVec = XMLoadFloat3(&position);
        float distToStart = XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&segment.start) - posVec));
        float distToEnd = XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&segment.end) - posVec));
        const XMFLOAT3& nearEndpoint = (distToStart <= distToEnd) ? segment.start : segment.end;

        snapshot.nearestNode = graph.FindNearestNode(nearEndpoint);

        return snapshot;
    }
}

void DemandSystem::Initialize(IPathfinder* pathfinder, Model* agentModel)
{
    m_pathfinder = pathfinder;
    m_agentModel = agentModel;
}

void DemandSystem::Update(float deltaTime, GameContext& context, std::vector<RoadSegment>& segments)
{
    UpdateAgentMovement(deltaTime, context);

    m_tickTimer += deltaTime;
    if (m_tickTimer < kTickInterval)
    {
        return;
    }
    m_tickTimer = 0.0f;

    RunDemandTick(context, segments);
}

void DemandSystem::StartCommute(ResidentAgent& agent, bool toOffice, GameObject* obj) const
{
    // 経路(waypoints)は自宅→職場の向きで作られている。進む向きに合わせて必要な時だけ反転する。
    if (agent.waypointsHomeToOffice != toOffice)
    {
        std::reverse(agent.waypoints.begin(), agent.waypoints.end());
        agent.waypointsHomeToOffice = toOffice;
    }

    agent.currentWaypointIndex = 0;
    agent.state = toOffice ? ResidentAgentState::Commuting : ResidentAgentState::CommutingHome;

    // 出発地点(自宅または職場の位置)から歩き始める。
    if (obj && !agent.waypoints.empty())
    {
        obj->transform.position = agent.waypoints.front();
    }
}

void DemandSystem::UpdateAgentMovement(float deltaTime, GameContext& context)
{
    // 時計が無い場合(起動順の都合など)は、昼の時刻として扱って出勤中の状態にしておく。
    float hours = context.clock ? context.clock->GetTimeOfDayHours() : 12.0f;

    for (ResidentAgent& agent : m_agents)
    {
        GameObject* obj = GameObjectFactory::FindById(*context.objects, agent.objectId);
        if (!obj)
        {
            continue;
        }

        // 予定表に従って、出勤・退勤を始める。
        // 職が決まっている(経路がある)住民だけが対象。夜に新築した家の住民などは、
        // 次の出勤時刻まで家で待つ。
        if (agent.hasOffice && !agent.waypoints.empty())
        {
            ScheduledActivity activity = m_schedule.ActivityAt(hours, agent.scheduleOffsetHours);
            if (agent.state == ResidentAgentState::AtHome && activity == ScheduledActivity::Work)
            {
                StartCommute(agent, true, obj);
            }
            else if (agent.state == ResidentAgentState::AtWork && activity == ScheduledActivity::Home)
            {
                StartCommute(agent, false, obj);
            }
        }

        // House/Officeの中にいる間(AtHome/AtWork)はモデルを表示しない。
        // 道路を移動中(出勤・帰宅)の時だけ表示する。
        bool isCommuting =
            agent.state == ResidentAgentState::Commuting ||
            agent.state == ResidentAgentState::CommutingHome;
        obj->visible = isCommuting;

        if (!isCommuting)
        {
            continue;
        }

        // 到着した時の状態: 出勤中なら職場、帰宅中なら自宅。
        ResidentAgentState arrivalState = (agent.state == ResidentAgentState::Commuting)
            ? ResidentAgentState::AtWork
            : ResidentAgentState::AtHome;

        if (agent.currentWaypointIndex + 1 >= agent.waypoints.size())
        {
            agent.state = arrivalState;
            obj->visible = false;
            continue;
        }

        // 1フレームで進める距離を、waypoint1個分ごとに使い切ってから次のwaypointへ持ち越す。
        // 曲線道路を細かい直線で近似すると1区間の長さが短くなるため、これをやらないと
        // (以前は1フレームにつきwaypointを1つしか消化できなかったため)速い移動体ほど
        // 見た目の速度がその区間の長さに頭打ちになり、カクついて見えてしまう。
        float remainingMove = agent.speed * deltaTime;

        while (remainingMove > 0.0f && agent.currentWaypointIndex + 1 < agent.waypoints.size())
        {
            const XMFLOAT3& targetPos = agent.waypoints[agent.currentWaypointIndex + 1];

            XMVECTOR current = XMLoadFloat3(&obj->transform.position);
            XMVECTOR target = XMLoadFloat3(&targetPos);
            XMVECTOR toTarget = target - current;
            float distance = XMVectorGetX(XMVector3Length(toTarget));

            if (distance <= remainingMove || distance < 0.0001f)
            {
                // このwaypointに到達。余った移動距離は次のwaypointへの移動に持ち越す。
                obj->transform.position = targetPos;
                remainingMove -= distance;
                agent.currentWaypointIndex++;
            }
            else
            {
                XMVECTOR direction = XMVector3Normalize(toTarget);
                XMVECTOR newPos = current + direction * remainingMove;
                XMStoreFloat3(&obj->transform.position, newPos);
                remainingMove = 0.0f;
            }
        }

        if (agent.currentWaypointIndex + 1 >= agent.waypoints.size())
        {
            agent.state = arrivalState;
        }
    }
}

void DemandSystem::RunDemandTick(GameContext& context, std::vector<RoadSegment>& segments)
{
    PROFILE_SCOPE("DemandSystem::RunDemandTick");

    RoadGraph graph = RoadGraph::BuildFromSegments(segments);

    // Profilerのメモリ内訳表示用に、直近の経路探索グラフの規模を覚えておく
    // (graph自体はここ限りのローカル変数のため、個数だけコピーして残す)。
    m_lastRoadGraphNodeCount = static_cast<int>(graph.GetNodeCount());
    m_lastRoadGraphEdgeCount = static_cast<int>(graph.GetEdgeCount());

    // 混雑表示をリセットしてから、現在通勤中のエージェントの経路で作り直す
    for (RoadSegment& segment : segments)
    {
        segment.debugLoad = 0.0f;
    }

    // GameObject*ではなくid+座標+接続情報だけをコピーする(Spawnによる再確保でポインタが無効化されるため)。
    std::vector<BuildingSnapshot> houses;
    std::vector<BuildingSnapshot> offices;

    for (const GameObject& obj : *context.objects)
    {
        if (obj.kind != ObjectKind::Building)
        {
            continue;
        }
        if (obj.buildingType == BuildingType::House)
        {
            houses.push_back(MakeBuildingSnapshot(obj.id, obj.transform.position, segments, graph));
        }
        else if (obj.buildingType == BuildingType::Office)
        {
            offices.push_back(MakeBuildingSnapshot(obj.id, obj.transform.position, segments, graph));
        }
    }

    // 建物-道路の接続線(視覚化用)を更新する
    m_connectionLines.clear();
    for (const BuildingSnapshot& house : houses)
    {
        if (house.hasRoadConnection)
        {
            m_connectionLines.push_back({ house.position, house.connectionPoint });
        }
    }
    for (const BuildingSnapshot& office : offices)
    {
        if (office.hasRoadConnection)
        {
            m_connectionLines.push_back({ office.position, office.connectionPoint });
        }
    }

    const BuildingDefinition& houseDef = GetBuildingDefinition(BuildingType::House);
    const BuildingDefinition& officeDef = GetBuildingDefinition(BuildingType::Office);

    // 1. 建物の規模に対して代表住民が不足していれば生成する
    for (const BuildingSnapshot& house : houses)
    {
        int existing = 0;
        for (const ResidentAgent& agent : m_agents)
        {
            if (agent.homeObjectId == house.id)
            {
                existing++;
            }
        }

        int needed = houseDef.representativeAgentCount - existing;
        for (int i = 0; i < needed; ++i)
        {
            Transform markerTransform;
            markerTransform.position = house.position;
            // House/Officeを実寸に合わせて1マスに収まるよう自動スケールする形に直した結果、
            // 建物の高さは0.6〜0.9m程度になった。キャラクターモデルの実測の高さは0.67mなので、
            // 建物より大きく見えないよう控えめな0.5倍にしている(見た目は後で調整可能)。
            markerTransform.scale = { 0.5f, 0.5f, 0.5f };

            GameObject& marker = GameObjectFactory::Spawn(*context.objects, m_agentModel, markerTransform, ObjectKind::Agent);
            marker.visible = false; // 自宅内にいる扱いなので、通勤を始めるまでは表示しない

            ResidentAgent agent;
            agent.objectId = marker.id;
            agent.homeObjectId = house.id;
            agent.state = ResidentAgentState::AtHome;
            agent.scheduleOffsetHours = m_schedule.OffsetForAgent(marker.id);
            m_agents.push_back(agent);
        }
    }

    // 2. まだ職の無い代表住民に、経路が繋がっていて空きのある一番近い職場を割り当てる
    for (ResidentAgent& agent : m_agents)
    {
        if (agent.state != ResidentAgentState::AtHome || agent.hasOffice)
        {
            continue;
        }

        const BuildingSnapshot* home = nullptr;
        for (const BuildingSnapshot& house : houses)
        {
            if (house.id == agent.homeObjectId)
            {
                home = &house;
                break;
            }
        }
        if (!home)
        {
            // 自宅が見つからない(将来削除に対応する時はここで職を失う処理を入れる)
            continue;
        }
        if (!home->hasRoadConnection)
        {
            continue; // 自宅の近くに道路が無い(隣接した道路が無ければ通勤できない)
        }

        XMFLOAT3 homePos = home->position;
        int startNode = home->nearestNode;

        int bestOfficeIndex = -1;
        float bestCost = FLT_MAX;
        std::optional<PathResult> bestPath;

        for (size_t officeIdx = 0; officeIdx < offices.size(); ++officeIdx)
        {
            const BuildingSnapshot& office = offices[officeIdx];

            int used = 0;
            for (const ResidentAgent& other : m_agents)
            {
                if (other.hasOffice && other.officeObjectId == office.id)
                {
                    used++;
                }
            }
            if (used >= officeDef.capacity)
            {
                continue;
            }

            if (!office.hasRoadConnection)
            {
                continue; // この職場の近くに道路が無い(隣接した道路が無ければアクセスできない)
            }

            int endNode = office.nearestNode;

            std::optional<PathResult> path = m_pathfinder->FindPath(graph, startNode, endNode);
            if (!path.has_value())
            {
                continue;
            }

            if (path->totalCost < bestCost)
            {
                bestCost = path->totalCost;
                bestOfficeIndex = static_cast<int>(officeIdx);
                bestPath = path;
            }
        }

        if (bestOfficeIndex >= 0 && bestPath.has_value())
        {
            // 職場と経路だけを決める。状態はAtHomeのままで、実際に出発するかどうかは
            // 予定表(m_schedule)と現在の時刻で決まる(UpdateAgentMovementが出勤を始めさせる)。
            agent.hasOffice = true;
            agent.officeObjectId = offices[bestOfficeIndex].id;
            agent.currentWaypointIndex = 0;
            agent.waypointsHomeToOffice = true;

            agent.waypoints.clear();
            agent.waypoints.push_back(homePos);
            for (const XMFLOAT3& wp : bestPath->waypoints)
            {
                agent.waypoints.push_back(wp);
            }
            agent.waypoints.push_back(offices[bestOfficeIndex].position);

            agent.segmentIndices = bestPath->segmentIndices;
        }
    }

    // 3. 通勤中(出勤・帰宅どちらも)のエージェント全員の経路をdebugLoadへ反映する(混雑表示用)
    for (const ResidentAgent& agent : m_agents)
    {
        if (agent.state != ResidentAgentState::Commuting &&
            agent.state != ResidentAgentState::CommutingHome)
        {
            continue;
        }
        for (size_t segIndex : agent.segmentIndices)
        {
            if (segIndex < segments.size())
            {
                segments[segIndex].debugLoad += 1.0f;
            }
        }
    }

    // 4. 統計を更新する
    m_lastStats = DemandStats{};
    m_lastStats.houseCount = static_cast<int>(houses.size());
    m_lastStats.officeCount = static_cast<int>(offices.size());
    m_lastStats.populationTotal = static_cast<int>(houses.size()) * houseDef.populationRepresented;
    m_lastStats.agentCount = static_cast<int>(m_agents.size());
    m_lastStats.jobCapacityTotal = static_cast<int>(offices.size()) * officeDef.capacity;
    for (const ResidentAgent& agent : m_agents)
    {
        if (agent.hasOffice)
        {
            m_lastStats.jobsFilled++;
        }
        else
        {
            m_lastStats.unmetDemand++;
        }
    }
}

void DemandSystem::DrawDebugUI() const
{
    // Debug/Hierarchy/Inspector/Editor Settings(既存パネル)と重ならない中央上部に配置する
    ImGui::SetNextWindowPos(ImVec2(310.0f, 200.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("City Stats");
    ImGui::Text("Houses: %d", m_lastStats.houseCount);
    ImGui::Text("Offices: %d", m_lastStats.officeCount);
    ImGui::Text("Population (represented): %d", m_lastStats.populationTotal);
    ImGui::Text("Resident agents: %d", m_lastStats.agentCount);
    ImGui::Text("Jobs filled: %d / %d", m_lastStats.jobsFilled, m_lastStats.jobCapacityTotal);
    ImGui::Text("Unmet demand: %d", m_lastStats.unmetDemand);
    ImGui::End();
}

void DemandSystem::Draw(DebugRenderer& debugRenderer) const
{
    // 建物-道路の接続線(入口/私道)を描画する。線が無い建物は道路に接続できていない。
    XMFLOAT4 connectionColor(0.4f, 1.0f, 0.9f, 1.0f);
    for (const RoadConnectionLine& line : m_connectionLines)
    {
        debugRenderer.AddLine(line.buildingPosition, line.connectionPoint, connectionColor);
    }
}

std::string DemandSystem::GetAgentDebugSummary(uint32_t objectId) const
{
    for (const ResidentAgent& agent : m_agents)
    {
        if (agent.objectId != objectId)
        {
            continue;
        }

        std::ostringstream oss;

        oss << "State: ";
        switch (agent.state)
        {
        case ResidentAgentState::AtHome:    oss << "AtHome";    break;
        case ResidentAgentState::Commuting: oss << "Commuting"; break;
        case ResidentAgentState::AtWork:    oss << "AtWork";    break;
        case ResidentAgentState::CommutingHome: oss << "CommutingHome"; break;
        }

        oss << "\nHome: #" << agent.homeObjectId;
        if (agent.hasOffice)
        {
            oss << "\nOffice: #" << agent.officeObjectId;
        }
        else
        {
            oss << "\nOffice: (none)";
        }

        return oss.str();
    }

    return "";
}

DemandMemoryStats DemandSystem::GetMemoryStats() const
{
    DemandMemoryStats stats;
    stats.agentCount = static_cast<int>(m_agents.size());
    stats.roadGraphNodeCount = m_lastRoadGraphNodeCount;
    stats.roadGraphEdgeCount = m_lastRoadGraphEdgeCount;

    for (const ResidentAgent& agent : m_agents)
    {
        stats.totalWaypoints += agent.waypoints.size();
    }

    return stats;
}
