#include <cfloat>
#include <cmath>
#include <algorithm>
#include <map>
#include <sstream>
#include "DemandSystem.h"
#include "GameContext.h"
#include "GameClock.h"
#include "GameEconomy.h"
#include "GameObjectFactory.h"
#include "BuildingType.h"
#include "FacilityData.h"
#include "RoadGraph.h"
#include "IPathfinder.h"
#include "RoadProximity.h"
#include "RoutePlanner.h"
#include "DebugRenderer.h"
#include "Profiler.h"
#include "WorldScale.h"
#include "ScenarioFile.h"
#include "Debug.h"

using namespace DirectX;

namespace
{
    // 建物から道路までの距離がこれを超える場合、「道路に接続していない」とみなす。
    // 「隣接している道だけを使う」という意図に近づけるため、グラフノードまでの距離ではなく
    // 道路区間そのものまでの距離(FindNearestPointOnRoad)に対して使う、狭めのしきい値。
    constexpr float kAdjacentDistance = WorldScale::FromLegacy(1.5f); // マス基準(マスが1.0だった頃の1.5)。マスの大きさに連動する

    // 建物の位置とidだけを持つ軽量なコピー。
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
        size_t segmentIndex = 0;             // 接続している道路区間の添字(hasRoadConnection==trueの時だけ有効)
    };

    // 施設(コンビニなど)1つ分の、建物のコピーと種類。
    struct FacilitySnapshot
    {
        BuildingSnapshot building;
        BuildingType type = BuildingType::Shop;
    };

    // buildingの位置から一番近い道路区間上の点を探し、接続情報を組み立てる。
    // 住宅・施設・職場の建物リスト作成時に使う共通処理。
    BuildingSnapshot MakeBuildingSnapshot(uint32_t id, const XMFLOAT3& position, const std::vector<RoadSegment>& segments, const RoadGraph& graph, float maxRoadDistance = kAdjacentDistance)
    {
        BuildingSnapshot snapshot;
        snapshot.id = id;
        snapshot.position = position;

        std::optional<NearestRoadPoint> nearestRoad = FindNearestPointOnRoad(segments, position, maxRoadDistance);
        if (!nearestRoad.has_value())
        {
            return snapshot; // hasRoadConnection == falseのまま
        }

        snapshot.hasRoadConnection = true;
        snapshot.connectionPoint = nearestRoad->point;
        snapshot.segmentIndex = nearestRoad->segmentIndex;

        // 接続先の区間の、position側に近い方の端点をそのまま経路探索用のノードとする(簡略化)。
        const RoadSegment& segment = segments[nearestRoad->segmentIndex];
        XMVECTOR posVec = XMLoadFloat3(&position);
        float distToStart = XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&segment.start) - posVec));
        float distToEnd = XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&segment.end) - posVec));
        const XMFLOAT3& nearEndpoint = (distToStart <= distToEnd) ? segment.start : segment.end;

        snapshot.nearestNode = graph.FindNearestNode(nearEndpoint);

        return snapshot;
    }

    // 建物のコピーから、経路を作る時の両端の情報を取り出す。
    RouteEndpoint ToEndpoint(const BuildingSnapshot& building)
    {
        RouteEndpoint endpoint;
        endpoint.position = building.position;
        endpoint.connectionPoint = building.connectionPoint;
        endpoint.segmentIndex = building.segmentIndex;
        endpoint.hasRoadConnection = building.hasRoadConnection;
        return endpoint;
    }
    // 2つの数から、住民の特徴を決める種を作る(同じ家の同じ番目の住民は、いつも同じ種)。
    uint32_t MakeResidentSeed(uint32_t globalSeed, uint32_t homeId, uint32_t index)
    {
        uint32_t h = globalSeed * 2654435761u;
        h ^= (homeId + 0x9E3779B9u) * 2246822519u;
        h ^= (index + 1u) * 3266489917u;
        h ^= h >> 15;
        return h;
    }
}

void DemandSystem::Initialize(IPathfinder* pathfinder, Model* agentModel)
{
    m_pathfinder = pathfinder;
    m_agentModel = agentModel;
    m_random.Seed(m_settings.seed);
}

void DemandSystem::Reset()
{
    m_agents.clear();
    m_tickTimer = 0.0f;
    m_lastStats = DemandStats{};
    m_connectionLines.clear();
    m_ledger.Clear();
    m_weeksEnded = 0;
    m_decisionLog.clear();
    m_random.Seed(m_settings.seed);
    m_lastRoadGraphNodeCount = 0;
    m_lastRoadGraphEdgeCount = 0;
    m_buildingsWithoutRoad.clear();
}

namespace
{
    Json Vec3ToJson(const XMFLOAT3& v)
    {
        Json array = Json::MakeArray();
        array.Push(v.x);
        array.Push(v.y);
        array.Push(v.z);
        return array;
    }

    // [x,y,z]を読む。要素数が足りない・数値でない場合はfalse。
    bool JsonToVec3(const Json& json, XMFLOAT3& out)
    {
        if (!json.IsArray() || json.Size() != 3 || !json.At(0).IsNumber() || !json.At(1).IsNumber() || !json.At(2).IsNumber())
        {
            return false;
        }
        out = XMFLOAT3(json.At(0).AsFloat(), json.At(1).AsFloat(), json.At(2).AsFloat());
        return true;
    }

    // 保存専用の安定した名前(列挙の並びを変えても古いセーブが読めるように、数値ではなく名前で保存する)。
    const char* AgentStateKey(ResidentAgentState state)
    {
        switch (state)
        {
        case ResidentAgentState::AtHome:        return "AtHome";
        case ResidentAgentState::GoingOut:      return "GoingOut";
        case ResidentAgentState::Visiting:      return "Visiting";
        case ResidentAgentState::ReturningHome: return "ReturningHome";
        case ResidentAgentState::Homeless:      return "Homeless";
        case ResidentAgentState::Stranded:      return "Stranded";
        // 停止中の仕事の状態。保存はしない(家にいる扱いで保存する)。
        case ResidentAgentState::Commuting:
        case ResidentAgentState::AtWork:
        case ResidentAgentState::CommutingHome: break;
        }
        return "AtHome";
    }

    // 知らない名前・古いセーブの仕事の状態(Commuting等)は、安全側の「家にいる」にする。
    ResidentAgentState AgentStateFromKey(const std::string& key)
    {
        if (key == "GoingOut")      { return ResidentAgentState::GoingOut; }
        if (key == "Visiting")      { return ResidentAgentState::Visiting; }
        if (key == "ReturningHome") { return ResidentAgentState::ReturningHome; }
        if (key == "Homeless")      { return ResidentAgentState::Homeless; }
        if (key == "Stranded")      { return ResidentAgentState::Stranded; }
        return ResidentAgentState::AtHome;
    }

    const char* PurposeKey(UsagePurpose purpose)
    {
        switch (purpose)
        {
        case UsagePurpose::TopUp:   return "TopUp";
        case UsagePurpose::BulkBuy: return "BulkBuy";
        case UsagePurpose::Movie:   return "Movie";
        case UsagePurpose::Light:   return "Light";
        case UsagePurpose::Count:   break;
        }
        return "TopUp";
    }

    UsagePurpose PurposeFromKey(const std::string& key)
    {
        if (key == "BulkBuy") { return UsagePurpose::BulkBuy; }
        if (key == "Movie")   { return UsagePurpose::Movie; }
        if (key == "Light")   { return UsagePurpose::Light; }
        return UsagePurpose::TopUp;
    }

    Json TallyToJson(const FacilityTally& tally)
    {
        Json json = Json::MakeObject();
        json.Set("visits", tally.visits);
        json.Set("sales", tally.sales);
        return json;
    }

    FacilityTally TallyFromJson(const Json* json)
    {
        FacilityTally tally;
        if (json && json->IsObject())
        {
            tally.visits = (std::max)(0, json->GetInt("visits", 0));
            tally.sales = (std::max)(0, json->GetInt("sales", 0));
        }
        return tally;
    }

    // 住民が動いている状態(経路を持つ状態)か。
    bool IsMoving(ResidentAgentState state)
    {
        return state == ResidentAgentState::GoingOut || state == ResidentAgentState::ReturningHome;
    }

    // 解体された施設の記録につける、仮のid。GameObjectのid(1から順に増える)と重ならないよう、大きな値から始める。
    constexpr uint32_t kRetiredFacilityIdBase = 0x80000000u;

    // 住居が無い住民を並べる時の、ずらす距離の基準(最初のマス1.0の0.2倍)。
    const float kHomelessSpacing = WorldScale::FromLegacy(0.2f);
}

void DemandSystem::Save(Json& section, const SaveContext& context) const
{
    section.Set("tickTimer", m_tickTimer);
    section.Set("weeksEnded", m_weeksEnded);

    Json agents = Json::MakeArray();
    if (context.game && context.game->objects)
    {
        const std::vector<GameObject>& objects = *context.game->objects;

        for (const ResidentAgent& agent : m_agents)
        {
            // 見た目のオブジェクトや家が見つからない住民は、保存しても復元できないので飛ばす。
            // (住んでいた住宅が解体された住民は、homeObjectIdが0。家が無くても、住民のデータは保存する)
            const GameObject* marker = GameObjectFactory::FindById(objects, agent.objectId);
            const bool orphan = (agent.homeObjectId == 0);
            int homeUid = orphan ? -1 : context.UidForBuildingObject(agent.homeObjectId);
            if (!marker || (!orphan && homeUid < 0))
            {
                continue;
            }

            // 施設が無くなった(保存できない)外出は、家にいる住民として保存する(途中の経路を引き継げない)。
            int targetUid = (agent.targetFacilityId != 0) ? context.UidForBuildingObject(agent.targetFacilityId) : -1;
            // 帰宅中は、施設がもう無くても(解体された)、歩いている途中のまま保存する。
            const bool standing = (agent.state == ResidentAgentState::Homeless || agent.state == ResidentAgentState::Stranded); // 立っている(動かない)状態
            const bool outing = (agent.state != ResidentAgentState::AtHome) && !standing
                && (targetUid >= 0 || agent.state == ResidentAgentState::ReturningHome);
            const ResidentAgentState savedState = outing ? agent.state
                : (agent.state == ResidentAgentState::Stranded ? ResidentAgentState::Stranded
                : (orphan ? ResidentAgentState::Homeless : ResidentAgentState::AtHome));

            Json entry = Json::MakeObject();
            entry.Set("home", homeUid);
            entry.Set("state", AgentStateKey(savedState));
            entry.Set("speed", agent.speed);
            // 住居なしとして保存する住民の位置は、元の住宅の位置(住居なしのまま立っている住民は、ずらした今の位置)。
            const bool savedAtHomePosition = orphan && !outing && !standing;
            entry.Set("position", Vec3ToJson(savedAtHomePosition ? agent.homePosition : marker->transform.position));
            entry.Set("scale", Vec3ToJson(marker->transform.scale));
            if (orphan)
            {
                entry.Set("homePosition", Vec3ToJson(agent.homePosition));
            }

            // 内部パラメータ。
            Json traits = Json::MakeObject();
            traits.Set("staminaCapBase", agent.traits.staminaCapBase);
            traits.Set("needDecayScale", agent.traits.needDecayScale);
            traits.Set("qualityWeight", agent.traits.qualityWeight);
            traits.Set("priceSensitivity", agent.traits.priceSensitivity);
            traits.Set("distanceTolerance", agent.traits.distanceTolerance);
            traits.Set("spontaneity", agent.traits.spontaneity);
            entry.Set("traits", std::move(traits));
            entry.Set("stamina", agent.stamina);
            entry.Set("satisfaction", agent.satisfaction);
            entry.Set("penaltyLevel", agent.penaltyLevel);
            entry.Set("lifeNeed", agent.lifeNeed);
            entry.Set("activityNeed", agent.activityNeed);
            entry.Set("unmetLifeHours", agent.unmetLifeHours);
            entry.Set("unmetActivityHours", agent.unmetActivityHours);
            entry.Set("satisfiedHours", agent.satisfiedHours);

            Json history = Json::MakeArray();
            for (const VisitRecord& record : agent.history)
            {
                int facilityUid = context.UidForBuildingObject(record.facilityObjectId);
                if (facilityUid < 0) { continue; }
                Json item = Json::MakeObject();
                item.Set("facility", facilityUid);
                item.Set("week", record.weekIndex);
                item.Set("hour", record.hour);
                history.Push(std::move(item));
            }
            entry.Set("history", std::move(history));

            // 今の外出。
            if (outing)
            {
                entry.Set("target", targetUid);
                entry.Set("purpose", PurposeKey(agent.purpose));
                entry.Set("visitHoursLeft", agent.visitHoursLeft);
                entry.Set("outingStopCount", agent.outingStopCount);
                entry.Set("usedFoodLike", agent.usedFoodLikeFacilityThisOuting);

                Json waypoints = Json::MakeArray();
                for (const XMFLOAT3& waypoint : agent.waypoints)
                {
                    waypoints.Push(Vec3ToJson(waypoint));
                }
                Json segmentIndices = Json::MakeArray();
                for (size_t index : agent.segmentIndices)
                {
                    segmentIndices.Push(static_cast<double>(index));
                }
                entry.Set("waypoints", std::move(waypoints));
                entry.Set("segmentIndices", std::move(segmentIndices));
                entry.Set("waypointIndex", static_cast<double>(agent.currentWaypointIndex));
                entry.Set("waypointsHomeToTarget", agent.waypointsHomeToTarget);
            }

            agents.Push(std::move(entry));
        }

        // 施設ごとの集計(来訪者数・売上)。
        Json facilities = Json::MakeArray();
        for (const FacilityRecord& record : m_ledger.GetRecords())
        {
            // 解体された施設の記録も、残す(過去の売上・来訪者数・種類。施設のuidは無いので-1)。
            int uid = context.UidForBuildingObject(record.objectId);
            if (uid < 0 && !record.demolished) { continue; }
            Json item = Json::MakeObject();
            item.Set("facility", uid);
            if (record.demolished)
            {
                item.Set("demolished", true);
            }
            item.Set("type", ScenarioFile::BuildingTypeKey(record.type));
            item.Set("thisWeek", TallyToJson(record.thisWeek));
            item.Set("lastWeek", TallyToJson(record.lastWeek));
            item.Set("thisMonth", TallyToJson(record.thisMonth));
            item.Set("total", TallyToJson(record.total));
            facilities.Push(std::move(item));
        }
        section.Set("facilities", std::move(facilities));
    }
    section.Set("agents", std::move(agents));
}

void DemandSystem::Load(const Json& section, int /*version*/, const LoadContext& context)
{
    Reset();

    if (!context.game || !context.game->objects)
    {
        return;
    }
    std::vector<GameObject>& objects = *context.game->objects;

    m_tickTimer = section.GetFloat("tickTimer", 0.0f);
    if (m_tickTimer < 0.0f)
    {
        m_tickTimer = 0.0f;
    }
    m_weeksEnded = (std::max)(0, section.GetInt("weeksEnded", 0));

    // 施設ごとの集計。
    uint32_t retiredCount = 0;
    if (const Json* facilities = section.Find("facilities"))
    {
        if (facilities->IsArray())
        {
            for (size_t i = 0; i < facilities->Size(); ++i)
            {
                const Json& item = facilities->At(i);
                if (!item.IsObject()) { continue; }
                // 解体済みの施設の記録は、建物が無いので、他のidと重ならない仮のid(GameObjectのidより、ずっと大きい値)を付ける。
                const bool demolished = item.GetBool("demolished", false);
                uint32_t facilityId = demolished ? (kRetiredFacilityIdBase + retiredCount++) : context.ObjectIdForBuildingUid(item.GetInt("facility", -1));
                BuildingType type = BuildingType::Shop;
                if (facilityId == 0 || !ScenarioFile::BuildingTypeFromKey(item.GetString("type"), type)) { continue; }
                FacilityRecord record;
                record.objectId = facilityId;
                record.type = type;
                record.demolished = demolished;
                record.thisWeek = TallyFromJson(item.Find("thisWeek"));
                record.lastWeek = TallyFromJson(item.Find("lastWeek"));
                record.thisMonth = TallyFromJson(item.Find("thisMonth"));
                record.total = TallyFromJson(item.Find("total"));
                m_ledger.SetRecord(record);
            }
        }
    }

    const Json* agents = section.Find("agents");
    if (!agents || !agents->IsArray())
    {
        return;
    }

    const int maxPerHome = GetBuildingDefinition(BuildingType::House).representativeAgentCount;
    int orphanCount = 0; // 住居が無い住民の数(特徴の種を、住民ごとに変えるための通し番号)
    std::map<uint32_t, int> perHome; // 家ごとに復元した住民の数(1軒に住む人数の上限を超える分は、復元しない。古いセーブは1軒に6人いた)

    for (size_t i = 0; i < agents->Size(); ++i)
    {
        const Json& entry = agents->At(i);
        if (!entry.IsObject())
        {
            continue;
        }

        // 建物のuidを、読み込みで新しく作った建物のidに結び直す。家が無い住民は復元できないので飛ばす。
        // 住居が無い住民は、"home"が-1で、元の住宅の位置("homePosition")を持つ。
        uint32_t homeId = context.ObjectIdForBuildingUid(entry.GetInt("home", -1));
        XMFLOAT3 savedHomePosition{};
        const bool orphan = (homeId == 0) && entry.GetInt("home", -1) < 0 && entry.Find("homePosition") != nullptr && JsonToVec3(*entry.Find("homePosition"), savedHomePosition);
        if (homeId == 0 && !orphan)
        {
            continue;
        }
        int indexAtHome = 0;
        if (orphan)
        {
            indexAtHome = orphanCount++;
        }
        else
        {
            indexAtHome = perHome[homeId];
            if (indexAtHome >= maxPerHome)
            {
                continue;
            }
            perHome[homeId] = indexAtHome + 1;
        }

        ResidentAgent agent;
        agent.homeObjectId = homeId;
        agent.homePosition = savedHomePosition;
        // 速さ・位置・大きさ・経路は、保存した時のマスの大きさでの値。今の大きさに合わせる(context.spaceScale)。
        agent.speed = entry.GetFloat("speed", agent.speed / context.spaceScale) * context.spaceScale;

        // 内部パラメータ。古いセーブ(無い項目)は、種から決めた初期値のまま。
        InitializeResident(agent, MakeResidentSeed(m_settings.seed, homeId, static_cast<uint32_t>(indexAtHome)), m_settings);
        if (const Json* traits = entry.Find("traits"))
        {
            if (traits->IsObject())
            {
                agent.traits.staminaCapBase = traits->GetFloat("staminaCapBase", agent.traits.staminaCapBase);
                agent.traits.needDecayScale = traits->GetFloat("needDecayScale", agent.traits.needDecayScale);
                agent.traits.qualityWeight = traits->GetFloat("qualityWeight", agent.traits.qualityWeight);
                agent.traits.priceSensitivity = traits->GetFloat("priceSensitivity", agent.traits.priceSensitivity);
                agent.traits.distanceTolerance = traits->GetFloat("distanceTolerance", agent.traits.distanceTolerance);
                agent.traits.spontaneity = traits->GetFloat("spontaneity", agent.traits.spontaneity);
            }
        }
        agent.stamina = (std::max)(0.0f, entry.GetFloat("stamina", agent.stamina));
        agent.satisfaction = std::clamp(entry.GetFloat("satisfaction", m_settings.satisfactionMax), 0.0f, m_settings.satisfactionMax);
        agent.penaltyLevel = std::clamp(entry.GetInt("penaltyLevel", agent.penaltyLevel), 0, (std::max)(0, m_settings.maxPenaltyLevel));
        agent.lifeNeed = std::clamp(entry.GetFloat("lifeNeed", agent.lifeNeed), 0.0f, 100.0f);
        agent.activityNeed = std::clamp(entry.GetFloat("activityNeed", agent.activityNeed), 0.0f, 100.0f);
        agent.unmetLifeHours = (std::max)(0.0f, entry.GetFloat("unmetLifeHours", 0.0f));
        agent.unmetActivityHours = (std::max)(0.0f, entry.GetFloat("unmetActivityHours", 0.0f));
        agent.satisfiedHours = (std::max)(0.0f, entry.GetFloat("satisfiedHours", 0.0f));

        if (const Json* history = entry.Find("history"))
        {
            if (history->IsArray())
            {
                for (size_t h = 0; h < history->Size() && agent.history.size() < ResidentAgent::kMaxHistory; ++h)
                {
                    const Json& item = history->At(h);
                    uint32_t facilityId = item.IsObject() ? context.ObjectIdForBuildingUid(item.GetInt("facility", -1)) : 0;
                    if (facilityId == 0) { continue; }
                    VisitRecord record;
                    record.facilityObjectId = facilityId;
                    record.weekIndex = item.GetInt("week", 0);
                    record.hour = item.GetFloat("hour", 0.0f);
                    agent.history.push_back(record);
                }
            }
        }

        // 今の外出。状態の名前が未知・古いセーブ(通勤)・施設が見つからない・経路が壊れている時は、安全側の「家にいる」にする。
        agent.state = AgentStateFromKey(entry.GetString("state", "AtHome"));
        uint32_t targetId = context.ObjectIdForBuildingUid(entry.GetInt("target", -1));
        if (!orphan && agent.state == ResidentAgentState::Homeless)
        {
            agent.state = ResidentAgentState::AtHome; // 家がある住民が「住居なし」と書かれていたら、矛盾なので、家にいる扱いにする
        }
        if (agent.state != ResidentAgentState::AtHome && agent.state != ResidentAgentState::Homeless && agent.state != ResidentAgentState::Stranded)
        {
            // 帰宅中は、行き先の施設が解体されていても(targetなし)、歩いている途中のまま復元する。
            if (targetId == 0 && agent.state != ResidentAgentState::ReturningHome)
            {
                agent.state = ResidentAgentState::AtHome;
            }
            else
            {
                agent.targetFacilityId = targetId;
                agent.purpose = PurposeFromKey(entry.GetString("purpose", "TopUp"));
                agent.visitHoursLeft = (std::max)(0.0f, entry.GetFloat("visitHoursLeft", 0.0f));
                agent.outingStopCount = (std::max)(0, entry.GetInt("outingStopCount", 0));
                agent.usedFoodLikeFacilityThisOuting = entry.GetBool("usedFoodLike", false);

                if (const Json* waypoints = entry.Find("waypoints"))
                {
                    if (waypoints->IsArray())
                    {
                        for (size_t w = 0; w < waypoints->Size(); ++w)
                        {
                            XMFLOAT3 point;
                            if (JsonToVec3(waypoints->At(w), point))
                            {
                                point.x *= context.spaceScale;
                                point.y *= context.spaceScale;
                                point.z *= context.spaceScale;
                                agent.waypoints.push_back(point);
                            }
                        }
                    }
                }
                if (const Json* segmentIndices = entry.Find("segmentIndices"))
                {
                    if (segmentIndices->IsArray())
                    {
                        for (size_t s = 0; s < segmentIndices->Size(); ++s)
                        {
                            int index = segmentIndices->At(s).AsInt(-1);
                            if (index >= 0)
                            {
                                agent.segmentIndices.push_back(static_cast<size_t>(index));
                            }
                        }
                    }
                }
                agent.waypointsHomeToTarget = entry.GetBool("waypointsHomeToTarget", true);
                agent.currentWaypointIndex = static_cast<size_t>((std::max)(0, entry.GetInt("waypointIndex", 0)));

                // 歩いている途中なのに、経路が壊れている時は、家にいる状態に戻す。
                if (IsMoving(agent.state) && agent.waypoints.size() < 2)
                {
                    agent.state = ResidentAgentState::AtHome;
                }
                else if (!agent.waypoints.empty() && agent.currentWaypointIndex >= agent.waypoints.size())
                {
                    agent.currentWaypointIndex = agent.waypoints.size() - 1;
                }
            }
        }
        if (orphan && agent.state == ResidentAgentState::AtHome)
        {
            agent.state = ResidentAgentState::Homeless; // 家が無いので、家にいる扱いにはできない
        }
        if (agent.state == ResidentAgentState::AtHome || agent.state == ResidentAgentState::Homeless || agent.state == ResidentAgentState::Stranded)
        {
            agent.targetFacilityId = 0;
            agent.waypoints.clear();
            agent.segmentIndices.clear();
            agent.currentWaypointIndex = 0;
            agent.visitHoursLeft = 0.0f;
        }

        // 見た目のオブジェクト(住民のマーカー)を作り直す。位置と拡大は保存した値のまま。
        Transform transform;
        JsonToVec3(entry.Find("position") ? *entry.Find("position") : Json(), transform.position);
        JsonToVec3(entry.Find("scale") ? *entry.Find("scale") : Json(), transform.scale);
        transform.position.x *= context.spaceScale;
        transform.position.y *= context.spaceScale;
        transform.position.z *= context.spaceScale;
        transform.scale.x *= context.spaceScale;
        transform.scale.y *= context.spaceScale;
        transform.scale.z *= context.spaceScale;
        GameObject& marker = GameObjectFactory::Spawn(objects, m_agentModel, transform, ObjectKind::Agent);
        marker.visible = IsMoving(agent.state) || agent.state == ResidentAgentState::Homeless || agent.state == ResidentAgentState::Stranded;
        agent.objectId = marker.id;

        m_agents.push_back(agent);
    }
}

void DemandSystem::Update(float deltaTime, GameContext& context, std::vector<RoadSegment>& segments)
{
    m_roads = &segments;
    ProcessClockEvents(context);
    AdvanceAgentTimers(context);
    UpdateAgentMovement(deltaTime, context);

    m_tickTimer += deltaTime;
    if (m_tickTimer < kTickInterval)
    {
        return;
    }
    m_tickTimer = 0.0f;

    RunDemandTick(context, segments);
}

void DemandSystem::ProcessClockEvents(GameContext& context)
{
    if (!context.clock)
    {
        return;
    }
    m_lastWeekCount = context.clock->GetWeekCount();

    // 週末(就寝に入った): ペナルティの判定 → 需要の自然減少 → 施設の週の集計を区切る。
    const int weekEnds = context.clock->ConsumeWeekEnds();
    for (int i = 0; i < weekEnds; ++i)
    {
        for (ResidentAgent& agent : m_agents)
        {
            ProcessResidentWeekEnd(agent, m_settings);
        }
        ++m_weeksEnded;
        const bool monthEnds = (m_weeksEnded % GameClock::kWeeksPerMonth) == 0;
        m_ledger.OnWeekEnd(monthEnds);
    }

    // 週の始まり(起床): 体力の回復。
    const int weekStarts = context.clock->ConsumeWeekStarts();
    for (int i = 0; i < weekStarts; ++i)
    {
        for (ResidentAgent& agent : m_agents)
        {
            ProcessResidentWeekStart(agent, m_settings);
        }
    }
}

void DemandSystem::AdvanceAgentTimers(GameContext& context)
{
    if (!context.clock)
    {
        return;
    }
    const float activeHours = context.clock->GetActiveDeltaHours();
    if (!(activeHours > 0.0f))
    {
        return;
    }
    for (ResidentAgent& agent : m_agents)
    {
        AdvanceResidentTime(agent, activeHours, m_settings);
    }
}

void DemandSystem::StartMove(ResidentAgent& agent, bool toTarget, GameObject* obj) const
{
    // 経路(waypoints)は自宅→施設の向きで作られている。進む向きに合わせて必要な時だけ反転する。
    if (agent.waypointsHomeToTarget != toTarget)
    {
        std::reverse(agent.waypoints.begin(), agent.waypoints.end());
        agent.waypointsHomeToTarget = toTarget;
    }

    agent.currentWaypointIndex = 0;
    agent.state = toTarget ? ResidentAgentState::GoingOut : ResidentAgentState::ReturningHome;

    // 出発地点(自宅または施設の位置)から歩き始める。
    if (obj && !agent.waypoints.empty())
    {
        obj->transform.position = agent.waypoints.front();
    }
}

void DemandSystem::FinishVisit(ResidentAgent& agent, GameContext& context, GameObject* obj)
{
    GameObject* facilityObj = GameObjectFactory::FindById(*context.objects, agent.targetFacilityId);
    const FacilityDefinition* facility = facilityObj ? FindFacilityDefinition(facilityObj->buildingType) : nullptr;
    if (facilityObj && facility)
    {
        const float hour = context.clock ? context.clock->GetTimeOfDayHours() : 12.0f;
        const int week = context.clock ? context.clock->GetWeekCount() : 1;
        ApplyFacilityUse(agent, *facility, facilityObj->id, week, hour, m_settings);
        agent.usedFoodLikeFacilityThisOuting = agent.usedFoodLikeFacilityThisOuting || facility->foodLike;

        // 実際に利用できた時だけ、売上を計上する。売上の一部(今は100%)を、プレイヤーの建設資金へ加える。
        m_ledger.RecordVisit(facilityObj->id, facilityObj->buildingType, facility->pricePerVisit);
        if (context.economy)
        {
            const int share = static_cast<int>(std::lround(static_cast<float>(facility->pricePerVisit) * m_settings.salesShareToFunds));
            context.economy->Earn(share);
        }

        // 立ち寄り(P1): 主目的(や前の立ち寄り)を使い終わった後、低い確率で、近くの施設に寄る。寄るなら、そこへ歩き始める。
        if (TryStartStopover(agent, context, *facilityObj))
        {
            return;
        }
    }

    // 帰宅を始める。経路が無い(壊れている)時は、その場で家にいる扱いにする。
    if (agent.waypoints.size() >= 2)
    {
        // 道路が切り取られていても、歩き抜けないように、今の道路で、帰りの経路を作り直す。帰れなければ、立ち往生。
        // 立ち寄りの後は、保存してある経路が短い(前の施設から今の施設まで)ので、迂回の判定はしない。
        if (!ReplanReturnRoute(agent, context, agent.outingStopCount == 0))
        {
            return;
        }
        StartMove(agent, false, obj);
    }
    else if (agent.homeObjectId == 0)
    {
        BecomeHomeless(agent, context); // 帰る家が、もう無い
    }
    else
    {
        agent.state = ResidentAgentState::AtHome;
        agent.targetFacilityId = 0;
    }
}

bool DemandSystem::TryStartStopover(ResidentAgent& agent, GameContext& context, const GameObject& currentFacility)
{
    if (!m_roads || m_roads->empty() || !m_pathfinder || !context.clock || !context.objects)
    {
        return false;
    }
    const float hour = context.clock->GetTimeOfDayHours();
    if (!CanConsiderStopover(agent, hour, m_settings))
    {
        return false;
    }
    const float stopChance = ComputeStopoverChance(agent, m_settings);
    if (!(m_random.Next01() < stopChance))
    {
        return false; // 寄らずに帰る(多くの外出は、ここ)
    }

    // ---- ここから先は、寄ると決めた時だけ(経路探索を使うので、重い) ----
    const RoadGraph graph = RoadGraph::BuildFromSegments(*m_roads);
    PathCache cache;

    const GameObject* home = GameObjectFactory::FindById(*context.objects, agent.homeObjectId);
    if (!home)
    {
        return false;
    }
    const RouteEndpoint homeEndpoint = ToEndpoint(MakeBuildingSnapshot(home->id, home->transform.position, *m_roads, graph));
    const RouteEndpoint hereEndpoint = ToEndpoint(MakeBuildingSnapshot(currentFacility.id, currentFacility.transform.position, *m_roads, graph));
    if (!homeEndpoint.hasRoadConnection || !hereEndpoint.hasRoadConnection)
    {
        return false;
    }

    // 目的(不足している需要があれば、その充足を優先。無ければ自発的)と、候補(今の目的のカテゴリの、他の施設)。
    const OutingPurposeChoice purpose = ChoosePurpose(agent, m_settings, m_random);
    const float maxLeg = WorldScale::FromLegacy(m_settings.stopoverMaxDistanceLegacyCells);

    std::vector<FacilityCandidate> candidates;
    std::vector<RoutePlan> legPlans;
    for (const GameObject& other : *context.objects)
    {
        if (other.kind != ObjectKind::Building || other.id == currentFacility.id)
        {
            continue;
        }
        const FacilityDefinition* definition = FindFacilityDefinition(other.buildingType);
        if (!definition || definition->category != purpose.category)
        {
            continue;
        }

        FacilityCandidate candidate;
        candidate.objectId = other.id;
        candidate.type = other.buildingType;

        const RouteEndpoint candidateEndpoint = ToEndpoint(MakeBuildingSnapshot(other.id, other.transform.position, *m_roads, graph));
        RoutePlan leg = PlanRoute(hereEndpoint, candidateEndpoint, *m_roads, graph, *m_pathfinder, cache);
        if (leg.valid)
        {
            // 立ち寄り先から家へ帰る道のりも、体力の判定に入れる(帰れなくなる立ち寄りはしない)。
            const RoutePlan back = PlanRoute(candidateEndpoint, homeEndpoint, *m_roads, graph, *m_pathfinder, cache);
            if (back.valid)
            {
                candidate.reachable = true;
                candidate.pathLength = leg.length;
                candidate.walkLength = leg.length + back.length;
                candidate.outOfRange = leg.length > maxLeg;
            }
        }
        candidates.push_back(candidate);
        legPlans.push_back(std::move(leg));
    }
    if (candidates.empty())
    {
        return false;
    }

    std::vector<CandidateEvaluation> evaluations;
    const int visitsThisOuting = agent.outingStopCount + 1; // 主目的 + これまでの立ち寄り(履歴の先頭から、この数)
    const int chosen = ChooseFacility(agent, candidates, purpose, m_settings, m_random, evaluations, visitsThisOuting);

    DecisionLogEntry entry;
    entry.homeObjectId = agent.homeObjectId;
    entry.weekCount = context.clock->GetWeekCount();
    entry.hour = hour;
    entry.needDriven = purpose.needDriven;
    entry.category = purpose.category;
    entry.purpose = purpose.purpose;
    entry.stamina = agent.stamina;
    entry.lifeNeed = agent.lifeNeed;
    entry.activityNeed = agent.activityNeed;
    entry.evaluations = evaluations;
    entry.chosenIndex = chosen;
    entry.stopover = true;
    entry.stopIndex = agent.outingStopCount + 1;
    entry.stopChance = stopChance;

    if (chosen < 0)
    {
        entry.note = "寄ろうとしたが、寄れる施設が無い(範囲外・体力不足・到達不可・使用済み)";
        PushDecisionLog(std::move(entry));
        return false;
    }

    // 食事系の連続は、ハードルを上げる(同じ外出で食事系を使った後に、別の食事系へは、めったに寄らない)。
    const FacilityDefinition* chosenDefinition = FindFacilityDefinition(candidates[static_cast<size_t>(chosen)].type);
    if (chosenDefinition)
    {
        const float accept = ComputeFoodChainAcceptChance(agent, *chosenDefinition, m_settings);
        if (accept < 1.0f && !(m_random.Next01() < accept))
        {
            entry.note = "食事系が続くので、寄らなかった(受け入れ確率 " + std::to_string(accept) + ")";
            entry.evaluations[static_cast<size_t>(chosen)].chosen = false;
            entry.chosenIndex = -1;
            PushDecisionLog(std::move(entry));
            return false;
        }
    }
    PushDecisionLog(std::move(entry));

    // 今いる施設 → 立ち寄り先 の経路で歩き始める。行き先の側が「施設」の向き(帰りは、立ち寄り先から家へ、作り直す)。
    const RoutePlan& leg = legPlans[static_cast<size_t>(chosen)];
    GameObject* marker = GameObjectFactory::FindById(*context.objects, agent.objectId);
    agent.targetFacilityId = candidates[static_cast<size_t>(chosen)].objectId;
    agent.purpose = purpose.purpose;
    agent.outingStopCount += 1;
    agent.waypoints = leg.waypoints;
    agent.segmentIndices = leg.segmentIndices;
    agent.waypointsHomeToTarget = true;
    StartMove(agent, true, marker);

    std::ostringstream note;
    note << "立ち寄り" << agent.outingStopCount << "回目 → " << (chosenDefinition ? chosenDefinition->name : "?")
         << "(" << UsagePurposeName(purpose.purpose) << "、重み " << evaluations[static_cast<size_t>(chosen)].weight << ")";
    agent.lastDecisionNote = note.str();
    return true;
}

void DemandSystem::StartReturnFromHere(ResidentAgent& agent, GameContext& context)
{
    GameObject* marker = GameObjectFactory::FindById(*context.objects, agent.objectId);
    agent.targetFacilityId = 0;
    agent.visitHoursLeft = 0.0f;
    if (!marker)
    {
        return;
    }

    if (!m_roads || m_roads->empty() || !m_pathfinder)
    {
        // 道路の情報が無い(テストなど)時は、その場で家にいる(家が無ければ、住居なし)扱いにする。
        agent.waypoints.clear();
        agent.segmentIndices.clear();
        agent.currentWaypointIndex = 0;
        if (agent.homeObjectId == 0) { BecomeHomeless(agent, context); }
        else { agent.state = ResidentAgentState::AtHome; marker->visible = false; }
        return;
    }

    XMFLOAT3 homePosition = agent.homePosition;
    if (agent.homeObjectId != 0)
    {
        const GameObject* home = GameObjectFactory::FindById(*context.objects, agent.homeObjectId);
        if (home) { homePosition = home->transform.position; }
    }

    const RoadGraph graph = RoadGraph::BuildFromSegments(*m_roads);
    PathCache cache;
    const RouteEndpoint from = ToEndpoint(MakeBuildingSnapshot(0, marker->transform.position, *m_roads, graph));
    const RouteEndpoint to = ToEndpoint(MakeBuildingSnapshot(0, homePosition, *m_roads, graph));
    const RoutePlan plan = PlanRoute(from, to, *m_roads, graph, *m_pathfinder, cache);
    if (!plan.valid)
    {
        BecomeStranded(agent, context, *m_roads);
        return;
    }

    // 位置は今のまま(経路の先頭が、今の位置)。「今の位置 → 自宅」の向き(帰りの向き)に並んでいる。
    agent.waypoints = plan.waypoints;
    agent.waypointsHomeToTarget = false;
    agent.currentWaypointIndex = 0;
    agent.segmentIndices = plan.segmentIndices;
    agent.state = ResidentAgentState::ReturningHome;
}

void DemandSystem::BecomeHomeless(ResidentAgent& agent, GameContext& context)
{
    agent.state = ResidentAgentState::Homeless;
    agent.targetFacilityId = 0;
    agent.waypoints.clear();
    agent.segmentIndices.clear();
    agent.currentWaypointIndex = 0;
    agent.visitHoursLeft = 0.0f;
    agent.outingStopCount = 0;
    agent.usedFoodLikeFacilityThisOuting = false;

    // 同じ位置に、既に立っている住居なしの住民の数だけ、ずらす(重ならないように)。
    int sameSpot = 0;
    for (const ResidentAgent& other : m_agents)
    {
        if (&other == &agent || other.state != ResidentAgentState::Homeless || other.homeObjectId != 0)
        {
            continue;
        }
        const float dx = other.homePosition.x - agent.homePosition.x;
        const float dz = other.homePosition.z - agent.homePosition.z;
        if (dx * dx + dz * dz < 0.01f * 0.01f)
        {
            ++sameSpot;
        }
    }

    GameObject* obj = GameObjectFactory::FindById(*context.objects, agent.objectId);
    if (obj)
    {
        const DirectX::XMFLOAT2 offset = HomelessOffset(sameSpot, kHomelessSpacing);
        obj->transform.position = XMFLOAT3(agent.homePosition.x + offset.x, agent.homePosition.y, agent.homePosition.z + offset.y);
        obj->visible = true;
    }
}

void DemandSystem::AbortOutingToDemolishedFacility(ResidentAgent& agent, GameContext& context)
{
    GameObject* obj = GameObjectFactory::FindById(*context.objects, agent.objectId);
    // 立ち寄りへ向かう途中・立ち寄り先の中だったか。その時の経路は「前の施設 → 立ち寄り先」なので、逆向きに戻ると、家ではなく前の施設へ着いてしまう。
    const bool onStopLeg = agent.outingStopCount > 0;
    agent.targetFacilityId = 0;                 // 向かう施設は、もう無い。利用も売上も、起きない
    agent.visitHoursLeft = 0.0f;
    agent.outingStopCount = 0;
    agent.usedFoodLikeFacilityThisOuting = false;
    agent.segmentIndices.clear();

    if (agent.state == ResidentAgentState::ReturningHome)
    {
        return; // 帰宅中は、そのまま帰る(行き先の記録を消しただけ)
    }

    if (onStopLeg)
    {
        if (agent.state == ResidentAgentState::Visiting && agent.waypoints.size() >= 2)
        {
            // 立ち寄り先の中: 施設の位置から、今の道路で、家への経路を作って帰り始める(迂回の判定はしない)。
            if (ReplanReturnRoute(agent, context, false))
            {
                StartMove(agent, false, obj);
            }
            return;
        }
        if (agent.state == ResidentAgentState::GoingOut && obj)
        {
            // 立ち寄り先へ歩いている途中: その場から、家へ帰り始める。位置は、飛ばさない。
            StartReturnFromHere(agent, context);
            return;
        }
    }

    if (agent.state == ResidentAgentState::Visiting && agent.waypoints.size() >= 2)
    {
        StartMove(agent, false, obj); // 施設の中にいた: 施設の位置から、帰り始める
        return;
    }

    if (agent.state == ResidentAgentState::GoingOut && obj && agent.waypoints.size() >= 2)
    {
        // 施設へ歩いている途中: その場で折り返す。歩いてきた道(経路の、通り過ぎた所)を、今の位置から逆向きに戻る。位置は、飛ばさない。
        const size_t passed = (std::min)(agent.currentWaypointIndex + 1, agent.waypoints.size());
        std::vector<XMFLOAT3> back(agent.waypoints.begin(), agent.waypoints.begin() + static_cast<std::ptrdiff_t>(passed));
        back.push_back(obj->transform.position);
        std::reverse(back.begin(), back.end());
        agent.waypoints = std::move(back);
        agent.waypointsHomeToTarget = false;
        agent.currentWaypointIndex = 0;
        agent.state = ResidentAgentState::ReturningHome;
        return;
    }

    // 経路が壊れている時は、その場で、家にいる(家が無ければ、住居なし)扱いにする。
    agent.waypoints.clear();
    agent.currentWaypointIndex = 0;
    if (agent.homeObjectId == 0)
    {
        BecomeHomeless(agent, context);
    }
    else
    {
        agent.state = ResidentAgentState::AtHome;
        if (obj) { obj->visible = false; }
    }
}

bool DemandSystem::ReplanReturnRoute(ResidentAgent& agent, GameContext& context, bool applyDetourPenalty)
{
    if (!m_roads || m_roads->empty() || !m_pathfinder || agent.waypoints.size() < 2)
    {
        return true; // 道路の情報が無い(テストなど)時は、元の経路のまま
    }

    // 今の経路(保存してある、行きの経路)の、施設側の端と、道のり。
    const XMFLOAT3 facilityPosition = agent.waypointsHomeToTarget ? agent.waypoints.back() : agent.waypoints.front();
    float oldLength = 0.0f;
    for (size_t i = 0; i + 1 < agent.waypoints.size(); ++i)
    {
        const float dx = agent.waypoints[i + 1].x - agent.waypoints[i].x;
        const float dz = agent.waypoints[i + 1].z - agent.waypoints[i].z;
        oldLength += std::sqrt(dx * dx + dz * dz);
    }

    XMFLOAT3 homePosition = agent.homePosition;
    if (agent.homeObjectId != 0)
    {
        const GameObject* home = GameObjectFactory::FindById(*context.objects, agent.homeObjectId);
        if (home) { homePosition = home->transform.position; }
    }

    const RoadGraph graph = RoadGraph::BuildFromSegments(*m_roads);
    PathCache cache;
    const RouteEndpoint from = ToEndpoint(MakeBuildingSnapshot(0, facilityPosition, *m_roads, graph));
    const RouteEndpoint to = ToEndpoint(MakeBuildingSnapshot(0, homePosition, *m_roads, graph));
    const RoutePlan plan = PlanRoute(from, to, *m_roads, graph, *m_pathfinder, cache);

    if (!plan.valid)
    {
        // 施設の利用が終わったが、帰れない: ここで、立ち往生する(利用が終わるまでは、立ち往生させない)。
        // 立つ場所は、施設の位置から、一番近い、残った道路の上(施設の中のままには、しない)。
        GameObject* marker = GameObjectFactory::FindById(*context.objects, agent.objectId);
        if (marker) { marker->transform.position = facilityPosition; }
        BecomeStranded(agent, context, *m_roads);
        return false;
    }

    agent.waypoints = plan.waypoints;
    agent.waypointsHomeToTarget = false;    // この経路は、「施設→自宅」の向き(帰りの向き)に並んでいる
    agent.segmentIndices = plan.segmentIndices;
    if (applyDetourPenalty && plan.length > oldLength + 1e-3f)
    {
        // 元の道のりより長い: 道路が無くなって、迂回して帰ることになった。
        agent.stamina = (std::max)(0.0f, agent.stamina - RoadLossStaminaCost(plan.length - oldLength, m_settings));
        ApplyRoadLossPenalty(agent, false, m_settings);
        agent.lastDecisionNote = "道路が無くなったので、迂回して帰る";
    }
    return true;
}

bool DemandSystem::TryRecoverStranded(ResidentAgent& agent, GameContext& context, const std::vector<RoadSegment>& segments, const RoadGraph& graph, PathCache& cache)
{
    if (!m_pathfinder || segments.empty())
    {
        return false;
    }
    GameObject* obj = GameObjectFactory::FindById(*context.objects, agent.objectId);
    if (!obj)
    {
        return false;
    }

    XMFLOAT3 homePosition = agent.homePosition;
    if (agent.homeObjectId != 0)
    {
        const GameObject* home = GameObjectFactory::FindById(*context.objects, agent.homeObjectId);
        if (home) { homePosition = home->transform.position; }
    }

    // 立ち往生した位置は、道路の上(切り取られた跡から、寄せた位置)だが、念のため、道路までの探索距離を、寄せる範囲ぶん、広げる。
    const float reach = kAdjacentDistance + WorldScale::FromLegacy(m_settings.strandedRoadSnapRadiusLegacyCells);
    const RouteEndpoint from = ToEndpoint(MakeBuildingSnapshot(0, obj->transform.position, segments, graph, reach));
    const RouteEndpoint to = ToEndpoint(MakeBuildingSnapshot(0, homePosition, segments, graph));
    const RoutePlan plan = PlanRoute(from, to, segments, graph, *m_pathfinder, cache);
    if (!plan.valid)
    {
        return false; // まだ、帰れない(道がつながっていない)
    }

    // 位置は、今の位置のまま(経路の先頭が、今の位置)。歩いて帰る。
    agent.waypoints = plan.waypoints;
    agent.waypointsHomeToTarget = false;        // この経路は、「行き先→自宅」の向き(帰りの向き)に並んでいる
    agent.currentWaypointIndex = 0;
    agent.segmentIndices = plan.segmentIndices;
    agent.state = ResidentAgentState::ReturningHome;
    agent.targetFacilityId = 0;
    agent.lastDecisionNote = "道がつながったので、帰り始めた";
    obj->visible = true;
    return true;
}

void DemandSystem::BecomeStranded(ResidentAgent& agent, GameContext& context, const std::vector<RoadSegment>& segments)
{
    GameObject* obj = GameObjectFactory::FindById(*context.objects, agent.objectId);
    agent.state = ResidentAgentState::Stranded;
    agent.targetFacilityId = 0;
    agent.waypoints.clear();
    agent.segmentIndices.clear();
    agent.currentWaypointIndex = 0;
    agent.visitHoursLeft = 0.0f;
    agent.outingStopCount = 0;
    agent.usedFoodLikeFacilityThisOuting = false;
    agent.lastDecisionNote = "道路が無くなり、帰れなくなった(立ち往生)";
    ApplyRoadLossPenalty(agent, true, m_settings);

    if (obj)
    {
        // 道路の上で立ち往生する(周りが建物だらけになっても、建物と重ならない)。今の位置が道路の上なら、そのまま。
        // 切り取られた跡や、施設の中なら、近くの、残った道路の上へ寄せる(寄せる距離の上限は、設定の数マス。遠くへ、ワープはしない)。
        const float reach = WorldScale::FromLegacy(m_settings.strandedRoadSnapRadiusLegacyCells);
        const std::optional<NearestRoadPoint> onRoad = FindNearestPointOnRoad(segments, obj->transform.position, reach);
        if (onRoad.has_value())
        {
            obj->transform.position = XMFLOAT3(onRoad->point.x, obj->transform.position.y, onRoad->point.z);
        }
        obj->visible = true;
    }
}
void DemandSystem::OnRoadsChanged(const std::vector<RemovedRoadSpan>& removed, const std::vector<RoadSegment>& segments, GameContext& context)
{
    // 区間の並びが変わったので、全員の「通る区間の添字」(混雑表示用)は、無効。空にする(混雑は、毎ティック、作り直される)。
    for (ResidentAgent& agent : m_agents)
    {
        agent.segmentIndices.clear();
    }
    if (removed.empty() || !context.objects)
    {
        return;
    }

    const RoadGraph graph = RoadGraph::BuildFromSegments(segments);
    PathCache pathCache;

    // 取り除いた部分を、細い四角にする(住民の経路は、道路の中心線をたどるので、中心線が通るかを調べる)。
    std::vector<RoadCutWindow> spanWindows;
    for (const RemovedRoadSpan& span : removed)
    {
        const float dx = span.end.x - span.start.x;
        const float dz = span.end.z - span.start.z;
        const float length = std::sqrt(dx * dx + dz * dz);
        if (length < 1e-4f) { continue; }
        RoadCutWindow window;
        window.center = XMFLOAT3((span.start.x + span.end.x) * 0.5f, span.start.y, (span.start.z + span.end.z) * 0.5f);
        window.dirX = dx / length;
        window.dirZ = dz / length;
        window.halfLength = length * 0.5f;
        window.halfWidth = 0.06f;
        spanWindows.push_back(window);
    }
    auto crossesRemoved = [&](const std::vector<XMFLOAT3>& legs)
    {
        for (size_t i = 0; i + 1 < legs.size(); ++i)
        {
            for (const RoadCutWindow& window : spanWindows)
            {
                float a = 0.0f, b = 0.0f;
                if (ClipLineToWindow(legs[i], legs[i + 1], window, a, b)) { return true; }
            }
        }
        return false;
    };

    for (ResidentAgent& agent : m_agents)
    {
        // 歩いている住民だけが対象。家・住居なし・立ち往生の住民は、道路を使っていない。
        // 施設の中にいる住民は、そのまま(利用が終わって、帰り始める時に、今の道路で帰りの経路を作り直し、帰れなければ、その時に、立ち往生する。FinishVisit)。
        if (!IsMoving(agent.state))
        {
            continue;
        }
        GameObject* obj = GameObjectFactory::FindById(*context.objects, agent.objectId);
        if (!obj || agent.waypoints.size() < 2)
        {
            continue;
        }

        // これから通る経路(位置の並び。歩く向きに)。歩いている人: 今の位置から、残りの経路。施設の中の人: 施設から自宅までの全部。
        const XMFLOAT3 startPosition = obj->transform.position;
        std::vector<XMFLOAT3> legs;
        legs.push_back(startPosition);
        for (size_t i = agent.currentWaypointIndex + 1; i < agent.waypoints.size(); ++i)
        {
            legs.push_back(agent.waypoints[i]);
        }
        if (!crossesRemoved(legs))
        {
            continue; // 通る道は、無事
        }

        // 今の外出をやめて、帰宅を優先する。今の位置の近くの、残った道路から、自宅(家が無ければ、元の家の位置)への経路を探し直す。
        XMFLOAT3 homePosition = agent.homePosition;
        if (agent.homeObjectId != 0)
        {
            const GameObject* home = GameObjectFactory::FindById(*context.objects, agent.homeObjectId);
            if (home) { homePosition = home->transform.position; }
        }
        const RouteEndpoint from = ToEndpoint(MakeBuildingSnapshot(0, startPosition, segments, graph));
        const RouteEndpoint to = ToEndpoint(MakeBuildingSnapshot(0, homePosition, segments, graph));
        const RoutePlan plan = PlanRoute(from, to, segments, graph, *m_pathfinder, pathCache);

        if (!plan.valid)
        {
            BecomeStranded(agent, context, segments);
            continue;
        }

        // 迂回して帰る。位置は、今の位置のまま(経路の先頭が、今の位置)。
        agent.waypoints = plan.waypoints;
        agent.waypointsHomeToTarget = false;        // この経路は、「行き先→自宅」の向き(帰りの向き)に並んでいる
        agent.currentWaypointIndex = 0;
        agent.segmentIndices = plan.segmentIndices;
        agent.state = ResidentAgentState::ReturningHome;
        agent.targetFacilityId = 0;
        agent.visitHoursLeft = 0.0f;
        agent.stamina = (std::max)(0.0f, agent.stamina - RoadLossStaminaCost(plan.length, m_settings));
        ApplyRoadLossPenalty(agent, false, m_settings);
        agent.lastDecisionNote = "道路が無くなったので、迂回して帰る";
    }
}

void DemandSystem::OnBuildingDemolished(const DemolishedBuilding& building, GameContext& context)
{
    if (building.type == BuildingType::House)
    {
        for (ResidentAgent& agent : m_agents)
        {
            if (agent.homeObjectId != building.id)
            {
                continue;
            }
            // 住民のデータ(需要・体力・特徴・履歴)は、消さない。家だけを失う。
            agent.homeObjectId = 0;
            agent.homePosition = building.position;
            agent.lastDecisionNote = "住んでいた住宅が解体された";
            if (agent.state == ResidentAgentState::AtHome)
            {
                BecomeHomeless(agent, context);
            }
            // 外出中の住民は、今の外出を続ける。帰り着いた時に(UpdateAgentMovementのarrive)、元の位置で住居なしになる。
        }
        return;
    }

    if (IsFacility(building.type))
    {
        // 過去の売上・来訪者数は、記録として残す(消さない)。
        m_ledger.MarkDemolished(building.id);
        for (ResidentAgent& agent : m_agents)
        {
            if (agent.targetFacilityId == building.id && agent.state != ResidentAgentState::AtHome
                && agent.state != ResidentAgentState::Homeless && agent.state != ResidentAgentState::Stranded)
            {
                AbortOutingToDemolishedFacility(agent, context);
            }
        }
    }
    // 職場(Office)は、仕事の概念が停止中なので、住民との関係が無い。
}

void DemandSystem::UpdateAgentMovement(float deltaTime, GameContext& context)
{
    const float activeHours = context.clock ? context.clock->GetActiveDeltaHours() : 0.0f;

    for (ResidentAgent& agent : m_agents)
    {
        GameObject* obj = GameObjectFactory::FindById(*context.objects, agent.objectId);
        if (!obj)
        {
            continue;
        }

        // 住居が無い住民・立ち往生している住民は、見える状態のまま立っている(動かない・施設を使わない)。
        if (agent.state == ResidentAgentState::Homeless || agent.state == ResidentAgentState::Stranded)
        {
            obj->visible = true;
            continue;
        }

#if 0
        // ---- 仕事(通勤)の概念は一旦停止中。以前の「予定表に従って出勤・退勤を始める」処理を、消さずに残してある ----
        // (hoursは、context.clock->GetTimeOfDayHours()。職場の割り当ては、RunDemandTickの#if 0のブロックにある)
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
#endif

        // ---- 施設の中にいる間: 時間が経つのを待つ(見た目は出さない) ----
        if (agent.state == ResidentAgentState::Visiting)
        {
            obj->visible = false;
            agent.visitHoursLeft -= activeHours;
            if (agent.visitHoursLeft <= 0.0f)
            {
                FinishVisit(agent, context, obj);
            }
            continue;
        }

        // 家の中・施設の中にいる間はモデルを表示しない。道路を移動中(外出・帰宅)の時だけ表示する。
        const bool moving = IsMoving(agent.state);
        obj->visible = moving;
        if (!moving)
        {
            continue;
        }

        // 経路を最後まで歩いた時の処理(到着)。外出なら、施設での利用を始める。帰宅なら、家にいる状態に戻る。
        auto arrive = [&]()
        {
            if (agent.state == ResidentAgentState::GoingOut)
            {
                GameObject* facilityObj = GameObjectFactory::FindById(*context.objects, agent.targetFacilityId);
                const FacilityDefinition* facility = facilityObj ? FindFacilityDefinition(facilityObj->buildingType) : nullptr;
                if (!facility)
                {
                    // 施設が無くなっていた(将来、削除に対応した時のため): 利用せずに帰る。
                    if (agent.outingStopCount > 0)
                    {
                        // 立ち寄りへの途中だった: 経路が「前の施設 → 立ち寄り先」なので、逆向きに戻らず、今の位置から家へ帰る。
                        agent.outingStopCount = 0;
                        StartReturnFromHere(agent, context);
                        return;
                    }
                    StartMove(agent, false, obj);
                    return;
                }
                agent.state = ResidentAgentState::Visiting;
                agent.visitHoursLeft = facility->visitDurationHours;
                obj->visible = false;
            }
            else if (agent.homeObjectId == 0)
            {
                BecomeHomeless(agent, context); // 帰り着いたが、家は解体されていた: 元の位置で、住居なしになる
            }
            else
            {
                agent.state = ResidentAgentState::AtHome;
                agent.targetFacilityId = 0;
                agent.waypoints.clear();
                agent.segmentIndices.clear();
                agent.currentWaypointIndex = 0;
                agent.outingStopCount = 0;
                agent.usedFoodLikeFacilityThisOuting = false;
                obj->visible = false;
            }
        };

        if (agent.waypoints.size() < 2 || agent.currentWaypointIndex + 1 >= agent.waypoints.size())
        {
            arrive();
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
            arrive();
        }
    }
}

void DemandSystem::PushDecisionLog(DecisionLogEntry entry)
{
    m_decisionLog.push_back(std::move(entry));
    const size_t capacity = static_cast<size_t>((std::max)(1, m_settings.decisionLogCapacity));
    while (m_decisionLog.size() > capacity)
    {
        m_decisionLog.erase(m_decisionLog.begin());
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

    // 混雑表示をリセットしてから、現在移動中の住民の経路で作り直す
    for (RoadSegment& segment : segments)
    {
        segment.debugLoad = 0.0f;
    }

    // GameObject*ではなくid+座標+接続情報だけをコピーする(Spawnによる再確保でポインタが無効化されるため)。
    std::vector<BuildingSnapshot> houses;
    std::vector<BuildingSnapshot> offices;
    std::vector<FacilitySnapshot> facilities;

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
        else if (IsFacility(obj.buildingType))
        {
            FacilitySnapshot facility;
            facility.building = MakeBuildingSnapshot(obj.id, obj.transform.position, segments, graph);
            facility.type = obj.buildingType;
            facilities.push_back(facility);
        }
    }

    // 道路に接続できていない建物(住宅・施設)を覚える(「道路がない！」の表示用)。
    m_buildingsWithoutRoad.clear();
    for (const BuildingSnapshot& house : houses)
    {
        if (!house.hasRoadConnection) { m_buildingsWithoutRoad.push_back(house.id); }
    }
    for (const FacilitySnapshot& facility : facilities)
    {
        if (!facility.building.hasRoadConnection) { m_buildingsWithoutRoad.push_back(facility.building.id); }
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
    for (const FacilitySnapshot& facility : facilities)
    {
        if (facility.building.hasRoadConnection)
        {
            m_connectionLines.push_back({ facility.building.position, facility.building.connectionPoint });
        }
    }

    const BuildingDefinition& houseDef = GetBuildingDefinition(BuildingType::House);

    // 0. 立ち往生している住民のうち、道がつながって帰れるようになった人は、帰り始める。
    {
        PathCache recoveryCache;
        for (ResidentAgent& agent : m_agents)
        {
            if (agent.state == ResidentAgentState::Stranded)
            {
                TryRecoverStranded(agent, context, segments, graph, recoveryCache);
            }
        }
    }

    // 1. 住宅に対して住民が不足していれば生成する(今は1軒に1人。内部パラメータは、住民ごとに種から決める)。
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
            const float agentScale = WorldScale::FromLegacy(0.5f); // マスの大きさに連動させる(マスが1.0の時は0.5)
            markerTransform.scale = { agentScale, agentScale, agentScale };

            GameObject& marker = GameObjectFactory::Spawn(*context.objects, m_agentModel, markerTransform, ObjectKind::Agent);
            marker.visible = false; // 自宅内にいる扱いなので、外出を始めるまでは表示しない

            ResidentAgent agent;
            agent.objectId = marker.id;
            agent.homeObjectId = house.id;
            agent.state = ResidentAgentState::AtHome;
            InitializeResident(agent, MakeResidentSeed(m_settings.seed, house.id, static_cast<uint32_t>(existing + i)), m_settings);
            m_agents.push_back(agent);
        }
    }

#if 0
    // ---- 仕事(通勤)の概念は一旦停止中。以前の「職場の割り当て」を、消さずに残してある ----
    // 2. まだ職の無い代表住民に、経路が繋がっていて空きのある一番近い職場を割り当てる
    const BuildingDefinition& officeDef = GetBuildingDefinition(BuildingType::Office);
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
            agent.waypointsHomeToTarget = true;

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
#endif

    // 2. 家にいる住民が、外出するかを判定し、するなら、施設を重み付き抽選で選んで、歩き始める。
    //    - 需要(生活・アクティビティ)のどちらかが不足していれば、その充足を優先して、高めの確率で外出を考える。
    //    - 足りていても、自発的に、低めの確率で外出する(体力に余裕がある時だけ。住民の「自発性」で、確率が変わる)。
    //    - 就寝中・遅い時間(lastDepartureHour以降)は、新しく外出しない(帰宅できなくなる外出をしない)。
    //    - 道路でつながっていない施設・体力が足りない施設は、抽選の対象にしない(利用も売上も発生しない)。
    const bool canDepart = context.clock
        && !context.clock->IsSleeping()
        && context.clock->GetTimeOfDayHours() < m_settings.lastDepartureHour;
    if (canDepart && !facilities.empty())
    {
        PathCache pathCache; // (始点のノード, 終点のノード) → 経路(このティック内の使い回し)

        for (ResidentAgent& agent : m_agents)
        {
            if (agent.state != ResidentAgentState::AtHome)
            {
                continue;
            }
            GameObject* obj = GameObjectFactory::FindById(*context.objects, agent.objectId);
            const BuildingSnapshot* home = nullptr;
            for (const BuildingSnapshot& house : houses)
            {
                if (house.id == agent.homeObjectId) { home = &house; break; }
            }
            if (!obj || !home)
            {
                continue;
            }

            // 外出を考えるか(確率)。
            const bool unmet = IsLifeNeedUnmet(agent, m_settings) || IsActivityNeedUnmet(agent, m_settings);
            float chance = 0.0f;
            if (unmet)
            {
                chance = m_settings.needDrivenChancePerTick;
            }
            else if (agent.stamina >= m_settings.spontaneousMinStamina)
            {
                chance = m_settings.spontaneousChancePerTick * agent.traits.spontaneity;
            }
            if (!(m_random.Next01() < chance))
            {
                continue;
            }

            if (!home->hasRoadConnection)
            {
                agent.lastDecisionNote = "外出したいが、自宅の近くに道路が無い";
                continue;
            }

            // 目的と、候補(今の目的のカテゴリの施設)。
            const OutingPurposeChoice purpose = ChoosePurpose(agent, m_settings, m_random);
            std::vector<FacilityCandidate> candidates;
            std::vector<RoutePlan> plans;
            for (const FacilitySnapshot& facility : facilities)
            {
                const FacilityDefinition* definition = FindFacilityDefinition(facility.type);
                if (!definition || definition->category != purpose.category)
                {
                    continue;
                }
                FacilityCandidate candidate;
                candidate.objectId = facility.building.id;
                candidate.type = facility.type;

                // 経路(建物 → 道路上の接続点 → 道路に沿って → 接続点 → 建物)。道のりは、歩く距離の全体。
                RoutePlan plan = PlanRoute(ToEndpoint(*home), ToEndpoint(facility.building), segments, graph, *m_pathfinder, pathCache);
                if (plan.valid)
                {
                    candidate.reachable = true;
                    candidate.pathLength = plan.length;
                }
                candidates.push_back(candidate);
                plans.push_back(std::move(plan));
            }

            if (candidates.empty())
            {
                agent.lastDecisionNote = std::string("外出したいが、") + FacilityCategoryName(purpose.category) + "の施設が無い";
                continue;
            }

            std::vector<CandidateEvaluation> evaluations;
            const int chosen = ChooseFacility(agent, candidates, purpose, m_settings, m_random, evaluations);

            DecisionLogEntry entry;
            entry.homeObjectId = agent.homeObjectId;
            entry.weekCount = context.clock->GetWeekCount();
            entry.hour = context.clock->GetTimeOfDayHours();
            entry.needDriven = purpose.needDriven;
            entry.category = purpose.category;
            entry.purpose = purpose.purpose;
            entry.stamina = agent.stamina;
            entry.lifeNeed = agent.lifeNeed;
            entry.activityNeed = agent.activityNeed;
            entry.evaluations = evaluations;
            entry.chosenIndex = chosen;
            PushDecisionLog(std::move(entry));

            if (chosen < 0 || !plans[static_cast<size_t>(chosen)].valid)
            {
                agent.lastDecisionNote = "外出したいが、行ける施設が無い(道路でつながっていない/体力が足りない)";
                continue;
            }

            // 選んだ施設へ、経路(自宅→…→施設)を作って歩き始める。
            const FacilitySnapshot* target = nullptr;
            for (const FacilitySnapshot& facility : facilities)
            {
                if (facility.building.id == candidates[static_cast<size_t>(chosen)].objectId) { target = &facility; break; }
            }
            if (!target)
            {
                continue;
            }
            const RoutePlan& plan = plans[static_cast<size_t>(chosen)];

            agent.targetFacilityId = target->building.id;
            agent.purpose = purpose.purpose;
            agent.outingStopCount = 0;
            agent.usedFoodLikeFacilityThisOuting = false;
            agent.waypoints = plan.waypoints;   // 自宅 → 接続点 → 道路 → 接続点 → 施設(斜めに横切らない)
            agent.segmentIndices = plan.segmentIndices;
            agent.waypointsHomeToTarget = true;
            StartMove(agent, true, obj);

            const FacilityDefinition* targetDefinition = FindFacilityDefinition(target->type);
            std::ostringstream note;
            note << (purpose.needDriven ? "需要が不足" : "自発的に") << " → " << (targetDefinition ? targetDefinition->name : "?")
                 << "(" << UsagePurposeName(purpose.purpose) << "、重み " << evaluations[static_cast<size_t>(chosen)].weight << ")";
            agent.lastDecisionNote = note.str();
        }
    }

    // 3. 移動中(外出・帰宅どちらも)の住民全員の経路をdebugLoadへ反映する(混雑表示用)
    for (const ResidentAgent& agent : m_agents)
    {
        if (!IsMoving(agent.state))
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
    m_lastStats.facilityCount = static_cast<int>(facilities.size());
    m_lastStats.populationTotal = static_cast<int>(houses.size()) * houseDef.populationRepresented;
    m_lastStats.agentCount = static_cast<int>(m_agents.size());
    for (const ResidentAgent& agent : m_agents)
    {
        if (IsLifeNeedUnmet(agent, m_settings) || IsActivityNeedUnmet(agent, m_settings))
        {
            m_lastStats.unmetDemand++;
        }
        if (agent.state == ResidentAgentState::Homeless)
        {
            m_lastStats.homelessCount++;
        }
        if (agent.state == ResidentAgentState::Stranded)
        {
            m_lastStats.strandedCount++;
        }
    }
}

DemandStats DemandSystem::GetStats() const
{
    // 建物・住民の数は、直近のRunDemandTickの値。施設の利用の集計は、常に最新の値を返す。
    DemandStats stats = m_lastStats;
    const FacilityTally thisWeek = m_ledger.SumThisWeek();
    const FacilityTally lastWeek = m_ledger.SumLastWeek();
    const FacilityTally thisMonth = m_ledger.SumThisMonth();
    stats.visitsThisWeek = thisWeek.visits;
    stats.salesThisWeek = thisWeek.sales;
    stats.visitsLastWeek = lastWeek.visits;
    stats.salesLastWeek = lastWeek.sales;
    stats.visitsThisMonth = thisMonth.visits;
    stats.salesThisMonth = thisMonth.sales;
    return stats;
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
        case ResidentAgentState::AtHome:        oss << "AtHome";        break;
        case ResidentAgentState::GoingOut:      oss << "GoingOut";      break;
        case ResidentAgentState::Visiting:      oss << "Visiting";      break;
        case ResidentAgentState::ReturningHome: oss << "ReturningHome"; break;
        case ResidentAgentState::Homeless:      oss << "Homeless";      break;
        case ResidentAgentState::Stranded:      oss << "Stranded";      break;
        default:                                oss << "(unused)";      break;
        }

        oss << "\nHome: #" << agent.homeObjectId;
        if (agent.targetFacilityId != 0)
        {
            oss << "\nTarget: #" << agent.targetFacilityId << " (" << UsagePurposeName(agent.purpose) << ")";
        }
        oss.precision(1);
        oss << std::fixed;
        oss << "\nLife need: " << agent.lifeNeed << "  Activity need: " << agent.activityNeed;
        oss << "\nStamina: " << agent.stamina << " / " << EffectiveStaminaCap(agent, m_settings) << "  (penalty " << agent.penaltyLevel << ")";
        oss << "\nUnmet hours: life " << agent.unmetLifeHours << ", activity " << agent.unmetActivityHours << "  Satisfied: " << agent.satisfiedHours;
        oss << "\nTraits: quality " << agent.traits.qualityWeight << ", price " << agent.traits.priceSensitivity
            << ", distance " << agent.traits.distanceTolerance << ", spontaneity " << agent.traits.spontaneity
            << ", decay " << agent.traits.needDecayScale;
        if (!agent.lastDecisionNote.empty())
        {
            oss << "\nLast decision: " << agent.lastDecisionNote;
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

ResidentInfo DemandSystem::MakeResidentInfo(const ResidentAgent& agent) const
{
    ResidentInfo info;
    info.found = true;
    info.homeObjectId = agent.homeObjectId;
    info.homeless = (agent.homeObjectId == 0);
    info.satisfaction = agent.satisfaction;
    info.state = agent.state;
    info.targetFacilityId = agent.targetFacilityId;
    info.purpose = agent.purpose;
    info.lifeNeed = agent.lifeNeed;
    info.activityNeed = agent.activityNeed;
    info.needThreshold = m_settings.needSatisfiedThreshold;
    info.stamina = agent.stamina;
    info.staminaCap = EffectiveStaminaCap(agent, m_settings);
    info.penaltyLevel = agent.penaltyLevel;
    info.maxPenaltyLevel = m_settings.maxPenaltyLevel;
    info.unmetLifeHours = agent.unmetLifeHours;
    info.unmetActivityHours = agent.unmetActivityHours;
    info.satisfiedHours = agent.satisfiedHours;
    info.traits = agent.traits;
    info.lastDecisionNote = agent.lastDecisionNote;
    info.history = agent.history;
    return info;
}

std::vector<uint32_t> DemandSystem::GetHomelessAgentObjectIds() const
{
    std::vector<uint32_t> ids;
    for (const ResidentAgent& agent : m_agents)
    {
        if (agent.state == ResidentAgentState::Homeless)
        {
            ids.push_back(agent.objectId);
        }
    }
    return ids;
}

std::vector<uint32_t> DemandSystem::GetStrandedAgentObjectIds() const
{
    std::vector<uint32_t> ids;
    for (const ResidentAgent& agent : m_agents)
    {
        if (agent.state == ResidentAgentState::Stranded)
        {
            ids.push_back(agent.objectId);
        }
    }
    return ids;
}

ResidentInfo DemandSystem::GetResidentInfoForHome(uint32_t homeObjectId) const
{
    if (homeObjectId == 0)
    {
        return ResidentInfo(); // 0は「家が無い住民」の印。どの住宅でもない
    }
    for (const ResidentAgent& agent : m_agents)
    {
        if (agent.homeObjectId == homeObjectId)
        {
            return MakeResidentInfo(agent);
        }
    }
    return ResidentInfo();
}

ResidentInfo DemandSystem::GetResidentInfoForAgent(uint32_t agentObjectId) const
{
    for (const ResidentAgent& agent : m_agents)
    {
        if (agent.objectId == agentObjectId)
        {
            return MakeResidentInfo(agent);
        }
    }
    return ResidentInfo();
}