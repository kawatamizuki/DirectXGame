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
#include <chrono>
#include <random>

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
    // 利用目的が、どのカテゴリの施設を使うものか(行動テストで、施設を指定した時に、目的を、その施設のカテゴリに合わせるのに使う)。
    FacilityCategory CategoryOfPurpose(UsagePurpose purpose)
    {
        return (purpose == UsagePurpose::TopUp || purpose == UsagePurpose::BulkBuy) ? FacilityCategory::Daily : FacilityCategory::Leisure;
    }
    UsagePurpose DefaultPurposeOf(FacilityCategory category)
    {
        return category == FacilityCategory::Daily ? UsagePurpose::TopUp : UsagePurpose::Light;
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

uint32_t MakeFreshRandomSeed()
{
    // OSの乱数と時刻を混ぜる(どちらかが弱くても、同じ値になりにくい)。0は乱数列が止まるので避ける。
    std::random_device device;
    const uint64_t now = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const uint32_t seed = device() ^ static_cast<uint32_t>(now) ^ (static_cast<uint32_t>(now >> 32) * 2654435761u);
    return seed != 0 ? seed : 0x9E3779B9u;
}

namespace
{
    // 新しい世界(開始・ロード・リセット)の乱数の種を、モードに従って決める。
    uint32_t ChooseWorldSeed(SimRandomMode mode, uint32_t settingsSeed)
    {
        return (mode == SimRandomMode::Reseed) ? MakeFreshRandomSeed() : settingsSeed;
    }
}

void DemandSystem::Initialize(IPathfinder* pathfinder, Model* agentModel)
{
    m_pathfinder = pathfinder;
    m_agentModel = agentModel;
    m_activeSeed = ChooseWorldSeed(m_randomMode, m_settings.seed);
    m_random.Seed(m_activeSeed);
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
    m_debugRecorder.StartNewRun(); // 住民の入れ替わり(ロード・リセット)で、古い記録は意味を失う(別の世界になる)
    m_tickIndex = 0;
    m_reservedHouses.clear();      // 入居者を選ぶ待ちの住宅も、新しい世界には持ち越さない
    for (SimTestPlan& plan : m_testPlans)
    {
        // 行動テストの待機中・実行中の計画は、住民が入れ替わるので、期限切れにする(終わった結果の一覧は、残す)。
        if (SimTestIsActive(plan.status))
        {
            plan.status = SimTestStatus::Expired;
            plan.reject = SimTestReject::WorldChanged;
            plan.finishedWeek = m_lastWeekCount;
            plan.finishedHour = m_lastClockHour;
        }
    }
    m_activeSeed = ChooseWorldSeed(m_randomMode, m_settings.seed);
    m_random.Seed(m_activeSeed);
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
    // 乱数: この世界の種と、今の内部状態。再現テスト(RestoreState)で、セーブした位置から同じ乱数列を続けるために残す。
    // 通常のプレイ(Reseed)では、読み込み時に使わない(ロードのたびに、抽選の結果が変わる)。
    section.Set("randomSeed", static_cast<double>(m_activeSeed));
    section.Set("randomState", static_cast<double>(m_random.GetState()));

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

    // 乱数: 再現テスト(RestoreState)の時だけ、セーブした内部状態を復元する。無ければ(古いセーブ)、Reset が決めた種のまま。
    if (m_randomMode == SimRandomMode::RestoreState && section.Find("randomState"))
    {
        m_activeSeed = static_cast<uint32_t>(section.GetNumber("randomSeed", static_cast<double>(m_activeSeed)));
        m_random.SetState(static_cast<uint32_t>(section.GetNumber("randomState", static_cast<double>(m_random.GetState()))));
    }

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
    ProcessImmediateTestPlans(context); // 行動テストの、すぐ消化できる指示(帰宅・行き先変更・体力)。更新の最初の、安全なタイミングで
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
    m_lastClockHour = context.clock->GetTimeOfDayHours();

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
        const float lifeBefore = agent.lifeNeed;
        const float staminaBefore = agent.stamina;
        ApplyFacilityUse(agent, *facility, facilityObj->id, week, hour, m_settings);
        agent.usedFoodLikeFacilityThisOuting = agent.usedFoodLikeFacilityThisOuting || facility->foodLike;
        // 記録: 利用が終わった(回復・売上・体力の消費。a=売上 b=生活需要の回復 c=体力の消費)。
        RecordEvent(agent, context, SimEventKind::VisitFinished, SimEventResult::Success, SimReason::None, facilityObj->id,
                    static_cast<float>(facility->pricePerVisit), agent.lifeNeed - lifeBefore, staminaBefore - agent.stamina);
        CheckAgentInvariants(agent, context);

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
    else
    {
        // 施設が無くなっていた(利用の反映なし)。普通は、解体の時に外出を中断するので、起きないはず。
        RecordEvent(agent, context, SimEventKind::VisitFinished, SimEventResult::Warning, SimReason::FacilityDemolished, agent.targetFacilityId);
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
        RecordEvent(agent, context, SimEventKind::ReturnStart, SimEventResult::Info, SimReason::None, 0,
                    m_debugRecorder.IsEnabled() ? RouteLength(agent.waypoints) : 0.0f);
    }
    else if (agent.homeObjectId == 0)
    {
        BecomeHomeless(agent, context); // 帰る家が、もう無い
    }
    else
    {
        agent.state = ResidentAgentState::AtHome;
        agent.targetFacilityId = 0;
        RecordEvent(agent, context, SimEventKind::ReturnHome, SimEventResult::Success); // 経路が無く、その場で家にいる扱い
        FinalizeTestOnOutingEnd(agent, context);
    }
}

bool DemandSystem::TryStartStopover(ResidentAgent& agent, GameContext& context, const GameObject& currentFacility)
{
    if (!m_roads || m_roads->empty() || !m_pathfinder || !context.clock || !context.objects)
    {
        return false;
    }
    const float hour = context.clock->GetTimeOfDayHours();
    const int stopIndex = agent.outingStopCount + 1; // 今から考える立ち寄りは、何回目か(記録用)

    // 行動テスト: この住民に、立ち寄りの確率を強制する指示が、残っているか(実行中で回数が残っている・待機中の立ち寄りの指示)。
    SimTestPlan* testPlan = FindActiveTestPlan(agent.objectId);
    if (testPlan && !(testPlan->forcedStopovers > 0
        && (testPlan->status == SimTestStatus::Running || (testPlan->status == SimTestStatus::Pending && testPlan->action == SimTestAction::ForceStopover))))
    {
        testPlan = nullptr;
    }

    const StopoverBlock block = ExplainStopoverBlock(agent, hour, m_settings);
    if (block != StopoverBlock::None)
    {
        if (testPlan)
        {
            // 通常ルール: 条件(回数の上限・時刻・体力・家)に合わないので、強制しない。拒否して、普通に帰る。
            FinishTestPlan(*testPlan, SimTestStatus::Rejected, SimTestReject::StopBlocked, static_cast<int>(block), &agent, context);
        }
        // 記録: 立ち寄りを考えない理由(回数の上限・時刻・体力・家)。
        SimReason reason = SimReason::StopNoHome;
        if (block == StopoverBlock::MaxStops) { reason = SimReason::StopMaxReached; }
        else if (block == StopoverBlock::TooLate) { reason = SimReason::StopTooLate; }
        else if (block == StopoverBlock::LowStamina) { reason = SimReason::StopLowStamina; }
        RecordEvent(agent, context, SimEventKind::StopoverCheck, SimEventResult::NotMet, reason, currentFacility.id, 0.0f, 0.0f, 0.0f, stopIndex);
        return false;
    }
    const float stopChance = ComputeStopoverChance(agent, m_settings);
    const float stopRoll = m_random.Next01(); // 乱数は、行動テストの強制の有無に関わらず、いつも1回引く
    if (!(stopRoll < stopChance) && !testPlan)
    {
        // 記録: 抽選に外れた(a=確率 b=乱数)。
        RecordEvent(agent, context, SimEventKind::StopoverCheck, SimEventResult::NotMet, SimReason::StopRollMiss, currentFacility.id, stopChance, stopRoll, 0.0f, stopIndex);
        return false; // 寄らずに帰る(多くの外出は、ここ)
    }
    if (testPlan && testPlan->status == SimTestStatus::Pending)
    {
        BeginTestPlan(*testPlan, agent, context, stopRoll, stopChance); // 待機中の立ち寄りの指示を、ここで消化する
    }
    // 強制した時は、本当の抽選の結果(外れ)も、この記録に残る(乱数 >= 確率なのに、当選として進んだ、とわかる)。
    RecordEvent(agent, context, SimEventKind::StopoverCheck, SimEventResult::Success, SimReason::StopRollHit, currentFacility.id, stopChance, stopRoll, 0.0f, stopIndex);

    // ---- ここから先は、寄ると決めた時だけ(経路探索を使うので、重い) ----
    const RoadGraph graph = RoadGraph::BuildFromSegments(*m_roads);
    PathCache cache;

    const GameObject* home = GameObjectFactory::FindById(*context.objects, agent.homeObjectId);
    if (!home)
    {
        RecordEvent(agent, context, SimEventKind::StopoverCheck, SimEventResult::NotMet, SimReason::StopHomeMissing, currentFacility.id, 0.0f, 0.0f, 0.0f, stopIndex);
        if (testPlan) { FinishTestPlan(*testPlan, SimTestStatus::Rejected, SimTestReject::StopHomeMissing, 0, &agent, context); }
        return false;
    }
    const RouteEndpoint homeEndpoint = ToEndpoint(MakeBuildingSnapshot(home->id, home->transform.position, *m_roads, graph));
    const RouteEndpoint hereEndpoint = ToEndpoint(MakeBuildingSnapshot(currentFacility.id, currentFacility.transform.position, *m_roads, graph));
    if (!homeEndpoint.hasRoadConnection || !hereEndpoint.hasRoadConnection)
    {
        RecordEvent(agent, context, SimEventKind::StopoverCheck, SimEventResult::NotMet, SimReason::StopNoRoad, currentFacility.id, 0.0f, 0.0f, 0.0f, stopIndex);
        if (testPlan) { FinishTestPlan(*testPlan, SimTestStatus::Rejected, SimTestReject::StopNoRoad, 0, &agent, context); }
        return false;
    }

    // 目的(不足している需要があれば、その充足を優先。無ければ自発的)と、候補(今の目的のカテゴリの、他の施設)。
    PurposeTrace purposeTrace;
    OutingPurposeChoice purpose = ChoosePurpose(agent, m_settings, m_random, &purposeTrace);
    const float maxLeg = WorldScale::FromLegacy(m_settings.stopoverMaxDistanceLegacyCells);

    // 行動テスト(施設を指定): 目的のカテゴリを、指定した施設のカテゴリに合わせる(乱数は、既に、普通に引いた後)。
    const GameObject* forcedFacility = nullptr;
    if (testPlan && testPlan->mode == SimTestMode::ForceFacility)
    {
        forcedFacility = GameObjectFactory::FindById(*context.objects, testPlan->facilityId);
        const FacilityDefinition* forcedDefinition = (forcedFacility && forcedFacility->kind == ObjectKind::Building) ? FindFacilityDefinition(forcedFacility->buildingType) : nullptr;
        if (!forcedDefinition || forcedFacility->id == currentFacility.id)
        {
            FinishTestPlan(*testPlan, SimTestStatus::Rejected, SimTestReject::FacilityNotFound, 0, &agent, context);
            return false;
        }
        purpose.category = forcedDefinition->category;
        if (CategoryOfPurpose(purpose.purpose) != forcedDefinition->category)
        {
            purpose.purpose = DefaultPurposeOf(forcedDefinition->category);
        }
    }

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
        // 記録: 目的は決めたが、同じカテゴリの他の施設が無い。
        const uint64_t detailId = RecordDecisionDetail(agent, context, purpose, purposeTrace, {}, FacilityChoiceTrace{}, -1, stopIndex, true, stopChance, stopRoll);
        RecordEvent(agent, context, SimEventKind::PurposeChoice, SimEventResult::Info, SimReason::None, 0, 0.0f, 0.0f, 0.0f, stopIndex, detailId);
        RecordEvent(agent, context, SimEventKind::StopoverCheck, SimEventResult::NotMet, SimReason::StopNoCandidates, currentFacility.id, 0.0f, 0.0f, 0.0f, stopIndex);
        if (testPlan) { FinishTestPlan(*testPlan, SimTestStatus::Rejected, SimTestReject::StopNoCandidates, 0, &agent, context); }
        return false;
    }

    std::vector<CandidateEvaluation> evaluations;
    FacilityChoiceTrace choiceTrace;
    const int visitsThisOuting = agent.outingStopCount + 1; // 主目的 + これまでの立ち寄り(履歴の先頭から、この数)
    int chosen = ChooseFacility(agent, candidates, purpose, m_settings, m_random, evaluations, visitsThisOuting, &choiceTrace);

    // 行動テスト(施設を指定): 抽選の結果を無視して、指定した施設を選ぶ。除外された施設(範囲外・体力不足・使用済み・到達不可)は、選ばない(拒否)。
    bool facilityForced = false;
    if (forcedFacility)
    {
        int forcedIndex = -1;
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (candidates[i].objectId == forcedFacility->id) { forcedIndex = static_cast<int>(i); break; }
        }
        if (forcedIndex < 0 || evaluations[static_cast<size_t>(forcedIndex)].reject != CandidateReject::None)
        {
            const uint64_t rejectedDetail = RecordDecisionDetail(agent, context, purpose, purposeTrace, evaluations, choiceTrace, -1, stopIndex, true, stopChance, stopRoll);
            RecordEvent(agent, context, SimEventKind::PurposeChoice, SimEventResult::Info, SimReason::None, 0, 0.0f, 0.0f, 0.0f, stopIndex, rejectedDetail);
            RecordEvent(agent, context, SimEventKind::FacilitySelect, SimEventResult::NotMet, SimReason::StopNoPickable, forcedFacility->id, 0.0f, 0.0f, 0.0f, stopIndex, rejectedDetail);
            FinishTestPlan(*testPlan, SimTestStatus::Rejected,
                           forcedIndex < 0 ? SimTestReject::FacilityNotFound : SimTestReject::FacilityNotEligible,
                           forcedIndex < 0 ? 0 : static_cast<int>(evaluations[static_cast<size_t>(forcedIndex)].reject), &agent, context);
            return false;
        }
        for (CandidateEvaluation& evaluation : evaluations) { evaluation.chosen = false; }
        evaluations[static_cast<size_t>(forcedIndex)].chosen = true;
        choiceTrace.pickedIndex = forcedIndex;
        chosen = forcedIndex;
        facilityForced = true;
    }

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
        const uint64_t detailId = RecordDecisionDetail(agent, context, purpose, purposeTrace, evaluations, choiceTrace, -1, stopIndex, true, stopChance, stopRoll);
        RecordEvent(agent, context, SimEventKind::PurposeChoice, SimEventResult::Info, SimReason::None, 0, 0.0f, 0.0f, 0.0f, stopIndex, detailId);
        RecordEvent(agent, context, SimEventKind::FacilitySelect, SimEventResult::NotMet, SimReason::StopNoPickable, 0, 0.0f, 0.0f, 0.0f, stopIndex, detailId);
        if (testPlan) { FinishTestPlan(*testPlan, SimTestStatus::Rejected, SimTestReject::StopNoPickable, 0, &agent, context); }
        return false;
    }

    // 食事系の連続は、ハードルを上げる(同じ外出で食事系を使った後に、別の食事系へは、めったに寄らない)。
    const FacilityDefinition* chosenDefinition = FindFacilityDefinition(candidates[static_cast<size_t>(chosen)].type);
    bool foodChecked = false;
    float foodAccept = 1.0f;
    float foodRoll = -1.0f;
    bool foodPassed = true;
    if (chosenDefinition)
    {
        foodAccept = ComputeFoodChainAcceptChance(agent, *chosenDefinition, m_settings);
        foodChecked = (foodAccept < 1.0f);
        if (foodChecked)
        {
            foodRoll = m_random.Next01();
            foodPassed = (foodRoll < foodAccept);
        }
        if (!foodPassed)
        {
            entry.note = "食事系が続くので、寄らなかった(受け入れ確率 " + std::to_string(foodAccept) + ")";
            entry.evaluations[static_cast<size_t>(chosen)].chosen = false;
            entry.chosenIndex = -1;
            PushDecisionLog(std::move(entry));
            // 記録: 抽選では選ばれたが、食事系の連続のため、取りやめた(内訳には、取りやめる前の抽選を残す)。
            const uint64_t detailId = RecordDecisionDetail(agent, context, purpose, purposeTrace, evaluations, choiceTrace, -1, stopIndex, true, stopChance, stopRoll,
                                                           foodChecked, foodAccept, foodRoll, foodPassed);
            RecordEvent(agent, context, SimEventKind::PurposeChoice, SimEventResult::Info, SimReason::None, 0, 0.0f, 0.0f, 0.0f, stopIndex, detailId);
            RecordEvent(agent, context, SimEventKind::FacilitySelect, SimEventResult::Success, SimReason::None, candidates[static_cast<size_t>(chosen)].objectId,
                        evaluations[static_cast<size_t>(chosen)].weight, evaluations[static_cast<size_t>(chosen)].chance, 0.0f, stopIndex, detailId);
            RecordEvent(agent, context, SimEventKind::StopoverCheck, SimEventResult::NotMet, SimReason::StopFoodChain, candidates[static_cast<size_t>(chosen)].objectId,
                        foodAccept, foodRoll, 0.0f, stopIndex, detailId);
            if (testPlan) { FinishTestPlan(*testPlan, SimTestStatus::Rejected, SimTestReject::StopFoodChain, 0, &agent, context); } // 食事系の連続は、強制しない確率の判定
            return false;
        }
    }
    PushDecisionLog(std::move(entry));

    // 記録: 寄ることに決めた。
    {
        const uint64_t detailId = RecordDecisionDetail(agent, context, purpose, purposeTrace, evaluations, choiceTrace, chosen, stopIndex, true, stopChance, stopRoll,
                                                       foodChecked, foodAccept, foodRoll, foodPassed, facilityForced);
        RecordEvent(agent, context, SimEventKind::PurposeChoice, SimEventResult::Info, SimReason::None, 0, 0.0f, 0.0f, 0.0f, stopIndex, detailId);
        RecordEvent(agent, context, SimEventKind::FacilitySelect, SimEventResult::Success, SimReason::StopStarted, candidates[static_cast<size_t>(chosen)].objectId,
                    evaluations[static_cast<size_t>(chosen)].weight, evaluations[static_cast<size_t>(chosen)].chance, 0.0f, stopIndex, detailId);
    }
    if (testPlan)
    {
        --testPlan->forcedStopovers; // 強制した立ち寄りを、1回、始めた
        ++testPlan->stopoversStarted;
    }

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
    RecordEvent(agent, context, SimEventKind::MoveStart, SimEventResult::Info, SimReason::StopStarted, agent.targetFacilityId, leg.length);

    std::ostringstream note;
    note << "立ち寄り" << agent.outingStopCount << "回目 → " << (chosenDefinition ? chosenDefinition->name : "?")
         << "(" << UsagePurposeName(purpose.purpose) << "、重み " << evaluations[static_cast<size_t>(chosen)].weight << ")";
    agent.lastDecisionNote = note.str();
    return true;
}

void DemandSystem::StartReturnFromHere(ResidentAgent& agent, GameContext& context, bool strandedPenalty)
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
        BecomeStranded(agent, context, *m_roads, SimReason::NoRouteHome, strandedPenalty);
        return;
    }

    // 位置は今のまま(経路の先頭が、今の位置)。「今の位置 → 自宅」の向き(帰りの向き)に並んでいる。
    agent.waypoints = plan.waypoints;
    agent.waypointsHomeToTarget = false;
    agent.currentWaypointIndex = 0;
    agent.segmentIndices = plan.segmentIndices;
    agent.state = ResidentAgentState::ReturningHome;
    RecordEvent(agent, context, SimEventKind::ReturnStart, SimEventResult::Info, SimReason::None, 0, plan.length);
}

void DemandSystem::BecomeHomeless(ResidentAgent& agent, GameContext& context)
{
    RecordEvent(agent, context, SimEventKind::BecameHomeless, SimEventResult::Info); // 状態を変える前の写し
    FinalizeTestOnOutingEnd(agent, context);
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
    if (agent.state != ResidentAgentState::ReturningHome) // 帰宅中は、行き先の記録を消すだけ(外出は中断されない)
    {
        RecordEvent(agent, context, SimEventKind::OutingAborted, SimEventResult::Warning, SimReason::FacilityDemolished, agent.targetFacilityId); // 状態を変える前の写し
    }
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
        BecomeStranded(agent, context, *m_roads, SimReason::NoRouteHome);
        return false;
    }

    agent.waypoints = plan.waypoints;
    agent.waypointsHomeToTarget = false;    // この経路は、「施設→自宅」の向き(帰りの向き)に並んでいる
    agent.segmentIndices = plan.segmentIndices;
    if (applyDetourPenalty && plan.length > oldLength + 1e-3f)
    {
        // 元の道のりより長い: 道路が無くなって、迂回して帰ることになった。
        const float staminaBefore = agent.stamina;
        agent.stamina = (std::max)(0.0f, agent.stamina - RoadLossStaminaCost(plan.length - oldLength, m_settings));
        ApplyRoadLossPenalty(agent, false, m_settings);
        agent.lastDecisionNote = "道路が無くなったので、迂回して帰る";
        RecordEvent(agent, context, SimEventKind::ReturnRerouted, SimEventResult::Warning, SimReason::DetourReturn, 0, plan.length - oldLength, staminaBefore - agent.stamina);
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
    RecordEvent(agent, context, SimEventKind::StrandedRecovered, SimEventResult::Info, SimReason::RoadReconnected, 0,
                m_debugRecorder.IsEnabled() ? RouteLength(agent.waypoints) : 0.0f);
    return true;
}

void DemandSystem::BecomeStranded(ResidentAgent& agent, GameContext& context, const std::vector<RoadSegment>& segments, SimReason reason, bool applyPenalty)
{
    GameObject* obj = GameObjectFactory::FindById(*context.objects, agent.objectId);
    RecordEvent(agent, context, SimEventKind::BecameStranded, SimEventResult::Warning, reason); // 状態を変える前の写し
    FinalizeTestOnOutingEnd(agent, context);
    agent.state = ResidentAgentState::Stranded;
    agent.targetFacilityId = 0;
    agent.waypoints.clear();
    agent.segmentIndices.clear();
    agent.currentWaypointIndex = 0;
    agent.visitHoursLeft = 0.0f;
    agent.outingStopCount = 0;
    agent.usedFoodLikeFacilityThisOuting = false;
    agent.lastDecisionNote = "道路が無くなり、帰れなくなった(立ち往生)";
    if (applyPenalty)
    {
        ApplyRoadLossPenalty(agent, true, m_settings);
    }

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
            BecomeStranded(agent, context, segments, SimReason::RoadCut);
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
        const float staminaBefore = agent.stamina;
        agent.stamina = (std::max)(0.0f, agent.stamina - RoadLossStaminaCost(plan.length, m_settings));
        ApplyRoadLossPenalty(agent, false, m_settings);
        agent.lastDecisionNote = "道路が無くなったので、迂回して帰る";
        RecordEvent(agent, context, SimEventKind::ReturnRerouted, SimEventResult::Warning, SimReason::RoadCut, 0, plan.length, staminaBefore - agent.stamina);
    }
}

void DemandSystem::OnBuildingDemolished(const DemolishedBuilding& building, GameContext& context)
{
    if (building.type == BuildingType::House)
    {
        ReleaseHouse(building.id); // 入居者を選ぶ待ちの途中で解体された住宅は、待ちから外す
        for (ResidentAgent& agent : m_agents)
        {
            if (agent.homeObjectId != building.id)
            {
                continue;
            }
            // 住民のデータ(需要・体力・特徴・履歴)は、消さない。家だけを失う。
            RecordEvent(agent, context, SimEventKind::HomeDemolished, SimEventResult::Warning, SimReason::None, building.id); // 家のidを0にする前の写し
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
                    RecordEvent(agent, context, SimEventKind::Arrived, SimEventResult::Warning, SimReason::FacilityMissingOnArrival, agent.targetFacilityId);
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
                RecordEvent(agent, context, SimEventKind::Arrived, SimEventResult::Info, SimReason::None, agent.targetFacilityId, facility->visitDurationHours);
            }
            else if (agent.homeObjectId == 0)
            {
                BecomeHomeless(agent, context); // 帰り着いたが、家は解体されていた: 元の位置で、住居なしになる
            }
            else
            {
                RecordEvent(agent, context, SimEventKind::ReturnHome, SimEventResult::Success, SimReason::None, 0, 0.0f, 0.0f, 0.0f, agent.outingStopCount); // 状態を戻す前の写し
                FinalizeTestOnOutingEnd(agent, context); // 行動テストの計画があれば、ここで終える(外出が終わった)
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

// ---------------------------------------------------------------------------
// デバッグの記録(SimDebugRecorder)への窓口。記録がOFFなら、すぐ戻る。ここは、乱数を引かず、シミュレーションの状態を変えない。
// ---------------------------------------------------------------------------
SimResidentSnapshot DemandSystem::MakeSnapshot(const ResidentAgent& agent) const
{
    SimResidentSnapshot snapshot;
    snapshot.state = agent.state;
    snapshot.stamina = agent.stamina;
    snapshot.staminaCap = EffectiveStaminaCap(agent, m_settings);
    snapshot.lifeNeed = agent.lifeNeed;
    snapshot.activityNeed = agent.activityNeed;
    snapshot.satisfaction = agent.satisfaction;
    snapshot.penaltyLevel = agent.penaltyLevel;
    snapshot.outingStopCount = agent.outingStopCount;
    snapshot.homeObjectId = agent.homeObjectId;
    snapshot.targetFacilityId = agent.targetFacilityId;
    return snapshot;
}

uint64_t DemandSystem::RecordEvent(const ResidentAgent& agent, const GameContext& context, SimEventKind kind, SimEventResult result,
                                   SimReason reason, uint32_t facilityId, float a, float b, float c,
                                   int stopIndex, uint64_t detailId, SimAnomaly anomaly)
{
    if (!m_debugRecorder.IsEnabled())
    {
        return 0;
    }
    SimEvent event;
    event.residentId = agent.objectId;
    event.week = context.clock ? context.clock->GetWeekCount() : 0;
    event.hour = context.clock ? context.clock->GetTimeOfDayHours() : 0.0f;
    event.kind = kind;
    event.result = result;
    event.reason = reason;
    event.anomaly = anomaly;
    event.stopIndex = (stopIndex >= 0) ? stopIndex : agent.outingStopCount;
    event.facilityId = facilityId;
    event.detailId = detailId;
    event.a = a;
    event.b = b;
    event.c = c;
    event.snapshot = MakeSnapshot(agent);
    return m_debugRecorder.AddEvent(std::move(event));
}

uint64_t DemandSystem::RecordDecisionDetail(const ResidentAgent& agent, const GameContext& context, const OutingPurposeChoice& purpose, const PurposeTrace& purposeTrace,
                                            const std::vector<CandidateEvaluation>& evaluations, const FacilityChoiceTrace& choice, int chosenIndex,
                                            int stopIndex, bool isStopover, float stopChance, float stopRoll,
                                            bool foodChecked, float foodAccept, float foodRoll, bool foodPassed, bool forcedFacility)
{
    if (!m_debugRecorder.IsEnabled())
    {
        return 0;
    }
    SimDecisionDetail detail;
    detail.residentId = agent.objectId;
    detail.outingId = m_debugRecorder.CurrentOutingId(agent.objectId);
    detail.week = context.clock ? context.clock->GetWeekCount() : 0;
    detail.hour = context.clock ? context.clock->GetTimeOfDayHours() : 0.0f;
    detail.stopIndex = stopIndex;
    detail.isStopover = isStopover;
    detail.stopChance = stopChance;
    detail.stopRoll = stopRoll;
    detail.settings = m_settings;           // 判断の時の設定(後でスライダーを動かしても、この判断の計算は読み返せる)
    detail.traits = agent.traits;
    detail.stamina = agent.stamina;
    detail.lifeNeed = agent.lifeNeed;
    detail.activityNeed = agent.activityNeed;
    detail.history = agent.history;
    detail.purpose = purpose;
    detail.purposeTrace = purposeTrace;
    detail.needDrivenDaily = purpose.needDriven && purpose.category == FacilityCategory::Daily;
    detail.evaluations = evaluations;
    detail.choice = choice;
    detail.chosenIndex = chosenIndex;
    detail.foodChainChecked = foodChecked;
    detail.foodChainAcceptChance = foodAccept;
    detail.foodChainRoll = foodRoll;
    detail.foodChainPassed = foodPassed;
    detail.forcedFacility = forcedFacility;
    return m_debugRecorder.AddDetail(std::move(detail));
}

void DemandSystem::CheckAgentInvariants(const ResidentAgent& agent, const GameContext& context, const std::unordered_map<uint32_t, const GameObject*>* index)
{
    if (!m_debugRecorder.IsEnabled() || !context.objects)
    {
        return;
    }
    // オブジェクトの有無・見た目の位置は、(まとめて調べる時は)ティックの最初に作った表から引く(FindByIdの線形探索を、住民の数だけ繰り返さないため)。
    auto find = [&](uint32_t id) -> const GameObject*
    {
        if (index)
        {
            const auto it = index->find(id);
            return it == index->end() ? nullptr : it->second;
        }
        return GameObjectFactory::FindById(*context.objects, id);
    };
    SimInvariantInput input;
    input.settings = &m_settings;
    input.homeObjectExists = (agent.homeObjectId == 0) || find(agent.homeObjectId) != nullptr;
    input.targetObjectExists = (agent.targetFacilityId == 0) || find(agent.targetFacilityId) != nullptr;
    const GameObject* marker = find(agent.objectId);
    input.markerPositionFinite = !marker
        || (std::isfinite(marker->transform.position.x) && std::isfinite(marker->transform.position.y) && std::isfinite(marker->transform.position.z));

    const uint32_t mask = CheckResidentInvariants(agent, input);
    const uint32_t newly = m_debugRecorder.UpdateAnomalyState(agent.objectId, mask);
    if (newly == 0)
    {
        return;
    }
    // 新しく違反になった異常ごとに、1件ずつ記録する(同じ異常は、直るまで、繰り返し記録しない)。aは、問題の値。
    for (uint32_t code = 1; code < 32; ++code)
    {
        if ((newly & (1u << code)) == 0)
        {
            continue;
        }
        float value = 0.0f;
        switch (static_cast<SimAnomaly>(code))
        {
        case SimAnomaly::StaminaOutOfRange:       value = agent.stamina; break;
        case SimAnomaly::NeedOutOfRange:          value = (agent.lifeNeed < 0.0f || agent.lifeNeed > 100.0f) ? agent.lifeNeed : agent.activityNeed; break;
        case SimAnomaly::SatisfactionOutOfRange:  value = agent.satisfaction; break;
        case SimAnomaly::PenaltyOutOfRange:       value = static_cast<float>(agent.penaltyLevel); break;
        case SimAnomaly::StopCountNegative:       value = static_cast<float>(agent.outingStopCount); break;
        case SimAnomaly::WaypointIndexOutOfRange: value = static_cast<float>(agent.currentWaypointIndex); break;
        case SimAnomaly::MissingTargetFacility:   value = static_cast<float>(agent.targetFacilityId); break;
        case SimAnomaly::MissingHome:
        case SimAnomaly::HomelessWithHome:        value = static_cast<float>(agent.homeObjectId); break;
        default: break;
        }
        RecordEvent(agent, context, SimEventKind::Anomaly, SimEventResult::Violation, SimReason::None, 0, value, 0.0f, 0.0f, -1, 0, static_cast<SimAnomaly>(code));
    }
}

void DemandSystem::CheckAllInvariants(const GameContext& context)
{
    if (!m_debugRecorder.IsEnabled())
    {
        return;
    }
    if (!context.objects)
    {
        return;
    }
    std::unordered_map<uint32_t, const GameObject*> index;
    index.reserve(context.objects->size());
    for (const GameObject& object : *context.objects)
    {
        index.emplace(object.id, &object);
    }
    for (const ResidentAgent& agent : m_agents)
    {
        CheckAgentInvariants(agent, context, &index);
    }
}

float DemandSystem::RouteLength(const std::vector<XMFLOAT3>& waypoints)
{
    float length = 0.0f;
    for (size_t i = 0; i + 1 < waypoints.size(); ++i)
    {
        const float dx = waypoints[i + 1].x - waypoints[i].x;
        const float dz = waypoints[i + 1].z - waypoints[i].z;
        length += std::sqrt(dx * dx + dz * dz);
    }
    return length;
}

// ---------------------------------------------------------------------------
// 行動テスト(段階3。SimTest.h)。指示は「計画」にして待たせ、次の安全なタイミングで、1回だけ消化する。
// 通常のプレイ(計画が1つも無い時)は、ここを通っても、何も変わらない(乱数の消費も、同じ)。
// ---------------------------------------------------------------------------
SimTestSubmitResult DemandSystem::SubmitTestCommand(const SimTestCommand& command, uint32_t selectedResidentId, const std::vector<GameObject>& objects)
{
    const SimTestSubmitResult result = SubmitTestCommandSilently(command, selectedResidentId, objects);
    for (const auto& callback : m_onTestSubmitted)
    {
        callback(command, selectedResidentId); // 再現セッション・タイムラインの記録(画面からの指示だけ)
    }
    return result;
}

SimTestSubmitResult DemandSystem::SubmitTestCommandSilently(const SimTestCommand& command, uint32_t selectedResidentId, const std::vector<GameObject>& objects)
{
    SimTestSubmitResult result;
    result.batchId = m_nextTestBatchId++;

    // 今の位置(見た目のオブジェクトの位置)で、対象を選ぶ。範囲は、実行ボタンを押した時の位置。
    std::unordered_map<uint32_t, const GameObject*> index;
    index.reserve(objects.size());
    for (const GameObject& object : objects) { index.emplace(object.id, &object); }
    std::vector<SimTestResidentPoint> points;
    points.reserve(m_agents.size());
    for (const ResidentAgent& agent : m_agents)
    {
        SimTestResidentPoint point;
        point.residentId = agent.objectId;
        const auto it = index.find(agent.objectId);
        if (it != index.end())
        {
            point.x = it->second->transform.position.x;
            point.z = it->second->transform.position.z;
        }
        points.push_back(point);
    }
    bool truncated = false;
    const std::vector<uint32_t> targets = SelectTestTargets(points, command, selectedResidentId, &truncated);
    result.targeted = static_cast<int>(targets.size());
    result.truncated = truncated;

    const int maxStops = (std::max)(0, m_settings.maxStopovers);
    for (uint32_t residentId : targets)
    {
        SimTestPlan plan;
        plan.testId = m_nextTestId++;
        plan.batchId = result.batchId;
        plan.residentId = residentId;
        plan.action = command.action;
        plan.mode = command.mode;
        plan.facilityId = (command.mode == SimTestMode::ForceFacility || command.action == SimTestAction::Redirect) ? command.facilityId : 0;
        plan.staminaValue = command.staminaValue;
        plan.createdWeek = m_lastWeekCount;
        plan.createdHour = m_lastClockHour;
        if (command.action == SimTestAction::ForceOuting)
        {
            plan.forceOuting = true;
            plan.forcedStopovers = (std::max)(0, (std::min)(command.forcedStopovers, maxStops));
            plan.stopoversRequested = plan.forcedStopovers;
        }
        else if (command.action == SimTestAction::ForceStopover)
        {
            plan.forcedStopovers = (std::max)(1, (std::min)(command.forcedStopovers, (std::max)(1, maxStops)));
            plan.stopoversRequested = plan.forcedStopovers;
        }

        // 既に実行中(外出が始まっている)の計画は、新しい指示で置き換える(外出させた後に、帰宅・立ち寄りを続けて試せるように)。
        // まだ待機中の計画は、置き換えない(同じ住民に、同時に2つの指示を出させない)。
        SimTestPlan* existing = FindActiveTestPlan(residentId);
        if (existing && existing->status == SimTestStatus::Running)
        {
            existing->status = SimTestStatus::Cancelled;
            existing->reject = SimTestReject::Superseded;
            existing->finishedWeek = m_lastWeekCount;
            existing->finishedHour = m_lastClockHour;
            m_debugRecorder.SetActiveTest(residentId, 0);
            existing = nullptr;
        }
        if (existing)
        {
            plan.status = SimTestStatus::Rejected;
            plan.reject = SimTestReject::AlreadyTesting;
            plan.finishedWeek = m_lastWeekCount;
            plan.finishedHour = m_lastClockHour;
            ++result.rejectedNow;
        }
        else if ((plan.mode == SimTestMode::ForceFacility || plan.action == SimTestAction::Redirect) && plan.facilityId == 0)
        {
            plan.status = SimTestStatus::Rejected;      // 施設を指定する指示なのに、施設が選ばれていない
            plan.reject = SimTestReject::FacilityNotFound;
            plan.finishedWeek = m_lastWeekCount;
            plan.finishedHour = m_lastClockHour;
            ++result.rejectedNow;
        }
        else
        {
            plan.status = SimTestStatus::Pending;
            ++result.created;
        }
        m_testPlans.push_back(plan);
    }

    // 上限を超えたら、終わった計画の、古い物から捨てる(待機中・実行中は、捨てない)。
    while (m_testPlans.size() > kMaxTestPlans)
    {
        auto it = std::find_if(m_testPlans.begin(), m_testPlans.end(), [](const SimTestPlan& p) { return !SimTestIsActive(p.status); });
        if (it == m_testPlans.end()) { break; }
        m_testPlans.erase(it);
    }
    return result;
}

int DemandSystem::CancelTestBatch(uint32_t batchId)
{
    int cancelled = 0;
    for (SimTestPlan& plan : m_testPlans)
    {
        if (plan.batchId == batchId && SimTestIsActive(plan.status))
        {
            plan.status = SimTestStatus::Cancelled;
            plan.finishedWeek = m_lastWeekCount;
            plan.finishedHour = m_lastClockHour;
            m_debugRecorder.SetActiveTest(plan.residentId, 0);
            ++cancelled;
        }
    }
    return cancelled;
}

int DemandSystem::CancelTestPlan(uint32_t testId)
{
    for (SimTestPlan& plan : m_testPlans)
    {
        if (plan.testId == testId && SimTestIsActive(plan.status))
        {
            plan.status = SimTestStatus::Cancelled;
            plan.finishedWeek = m_lastWeekCount;
            plan.finishedHour = m_lastClockHour;
            m_debugRecorder.SetActiveTest(plan.residentId, 0);
            return 1;
        }
    }
    return 0;
}

void DemandSystem::ClearFinishedTestPlans()
{
    m_testPlans.erase(std::remove_if(m_testPlans.begin(), m_testPlans.end(), [](const SimTestPlan& p) { return !SimTestIsActive(p.status); }), m_testPlans.end());
}

int DemandSystem::CountActiveTestPlans() const
{
    int count = 0;
    for (const SimTestPlan& plan : m_testPlans) { count += SimTestIsActive(plan.status) ? 1 : 0; }
    return count;
}

int DemandSystem::CountResidentsInRect(float minX, float minZ, float maxX, float maxZ, const std::vector<GameObject>& objects) const
{
    const float loX = (std::min)(minX, maxX), hiX = (std::max)(minX, maxX), loZ = (std::min)(minZ, maxZ), hiZ = (std::max)(minZ, maxZ);
    std::unordered_map<uint32_t, const GameObject*> index;
    index.reserve(objects.size());
    for (const GameObject& object : objects) { index.emplace(object.id, &object); }
    int count = 0;
    for (const ResidentAgent& agent : m_agents)
    {
        const auto it = index.find(agent.objectId);
        if (it == index.end()) { continue; }
        const XMFLOAT3& p = it->second->transform.position;
        if (p.x >= loX && p.x <= hiX && p.z >= loZ && p.z <= hiZ) { ++count; }
    }
    return count;
}

SimTestPlan* DemandSystem::FindActiveTestPlan(uint32_t residentId)
{
    for (SimTestPlan& plan : m_testPlans)
    {
        if (plan.residentId == residentId && SimTestIsActive(plan.status))
        {
            return &plan;
        }
    }
    return nullptr;
}

void DemandSystem::BeginTestPlan(SimTestPlan& plan, const ResidentAgent& agent, const GameContext& context, float roll, float chance)
{
    plan.status = SimTestStatus::Running;
    // 先に番号を付けてから記録するので、この TestCommand イベント自身にも、テストの番号が付く。
    m_debugRecorder.SetActiveTest(agent.objectId, plan.testId);
    plan.firstEventId = RecordEvent(agent, context, SimEventKind::TestCommand, SimEventResult::Success, SimReason::TestApplied, plan.facilityId,
                                    static_cast<float>(plan.testId), roll, chance);
}

void DemandSystem::FinishTestPlan(SimTestPlan& plan, SimTestStatus status, SimTestReject reject, int detail, const ResidentAgent* agent, const GameContext& context)
{
    plan.status = status;
    plan.reject = reject;
    plan.rejectDetail = detail;
    plan.finishedWeek = context.clock ? context.clock->GetWeekCount() : m_lastWeekCount;
    plan.finishedHour = context.clock ? context.clock->GetTimeOfDayHours() : m_lastClockHour;
    if (agent)
    {
        if (status == SimTestStatus::Rejected)
        {
            // 拒否も、記録する(普通のルールの結果。灰色)。a=テストの番号 b=拒否の理由の番号。
            m_debugRecorder.SetActiveTest(agent->objectId, plan.testId);
            const uint64_t id = RecordEvent(*agent, context, SimEventKind::TestCommand, SimEventResult::NotMet, SimReason::TestRejected, plan.facilityId,
                                            static_cast<float>(plan.testId), static_cast<float>(static_cast<int>(reject)), static_cast<float>(detail));
            if (plan.firstEventId == 0) { plan.firstEventId = id; }
        }
        m_debugRecorder.SetActiveTest(agent->objectId, 0);
    }
}

void DemandSystem::MaintainTestPlansAtTick(GameContext& context)
{
    if (m_testPlans.empty() || !context.clock)
    {
        return;
    }
    const int week = context.clock->GetWeekCount();
    for (SimTestPlan& plan : m_testPlans)
    {
        if (plan.status != SimTestStatus::Pending)
        {
            continue;
        }
        const ResidentAgent* agent = nullptr;
        for (const ResidentAgent& a : m_agents) { if (a.objectId == plan.residentId) { agent = &a; break; } }
        if (!agent)
        {
            FinishTestPlan(plan, SimTestStatus::Expired, SimTestReject::WorldChanged, 0, nullptr, context);
            continue;
        }
        // 期限: 次の週までに、タイミングが来なければ、期限切れ(就寝中・遅い時刻の間は、待つ)。
        if (week > plan.createdWeek + 1)
        {
            FinishTestPlan(plan, SimTestStatus::Expired, SimTestReject::None, 0, agent, context);
            continue;
        }
        // 前提: 外出させるなら、家にいる。立ち寄りなら、外出中。ここで外れたら、通常ルールの拒否。
        if (plan.action == SimTestAction::ForceOuting && agent->state != ResidentAgentState::AtHome)
        {
            FinishTestPlan(plan, SimTestStatus::Rejected, SimTestReject::NotAtHome, static_cast<int>(agent->state), agent, context);
        }
        else if (plan.action == SimTestAction::ForceStopover && agent->state != ResidentAgentState::GoingOut && agent->state != ResidentAgentState::Visiting)
        {
            FinishTestPlan(plan, SimTestStatus::Rejected, SimTestReject::NotOut, static_cast<int>(agent->state), agent, context);
        }
    }
}

void DemandSystem::ProcessImmediateTestPlans(GameContext& context)
{
    if (m_testPlans.empty() || !context.objects)
    {
        return;
    }
    int budget = 8; // 1回の更新で消化する数の上限(経路探索を含むので、一度に、まとめて行わない)
    for (SimTestPlan& plan : m_testPlans)
    {
        if (plan.status != SimTestStatus::Pending || plan.action == SimTestAction::ForceOuting || plan.action == SimTestAction::ForceStopover)
        {
            continue;
        }
        if (budget-- <= 0)
        {
            break;
        }
        ResidentAgent* agent = nullptr;
        for (ResidentAgent& a : m_agents) { if (a.objectId == plan.residentId) { agent = &a; break; } }
        if (!agent)
        {
            FinishTestPlan(plan, SimTestStatus::Expired, SimTestReject::WorldChanged, 0, nullptr, context);
            continue;
        }

        if (plan.action == SimTestAction::SetStamina)
        {
            const float cap = EffectiveStaminaCap(*agent, m_settings);
            const float value = (std::max)(0.0f, (std::min)(plan.staminaValue, cap));
            BeginTestPlan(plan, *agent, context, value, cap);
            agent->stamina = value;
            FinishTestPlan(plan, SimTestStatus::Succeeded, SimTestReject::None, 0, agent, context);
        }
        else if (plan.action == SimTestAction::ReturnHome)
        {
            if (agent->state != ResidentAgentState::GoingOut && agent->state != ResidentAgentState::Visiting)
            {
                FinishTestPlan(plan, SimTestStatus::Rejected, SimTestReject::NotOut, static_cast<int>(agent->state), agent, context);
                continue;
            }
            BeginTestPlan(plan, *agent, context);
            // 今の位置から、家へ帰る(利用はしない。売上・回復なし。位置は飛ばさない)。道が無ければ、満足度を下げずに、立ち往生。
            agent->targetFacilityId = 0;
            agent->visitHoursLeft = 0.0f;
            agent->outingStopCount = 0;
            agent->usedFoodLikeFacilityThisOuting = false;
            agent->lastDecisionNote = "テストで、帰宅させられた";
            StartReturnFromHere(*agent, context, false);
            if (agent->state == ResidentAgentState::AtHome)
            {
                FinalizeTestOnOutingEnd(*agent, context); // 道路の情報が無く、その場で家にいる扱いになった時は、ここで終える
            }
            // 歩いて帰る時は、外出が終わる(家に着く)まで、実行中。終わった時に、FinalizeTestOnOutingEnd が、成功にする。
        }
        else if (plan.action == SimTestAction::Redirect)
        {
            GameObject* marker = GameObjectFactory::FindById(*context.objects, agent->objectId);
            const GameObject* facility = GameObjectFactory::FindById(*context.objects, plan.facilityId);
            const FacilityDefinition* definition = facility ? FindFacilityDefinition(facility->buildingType) : nullptr;
            if (agent->state != ResidentAgentState::GoingOut)
            {
                FinishTestPlan(plan, SimTestStatus::Rejected, SimTestReject::NotGoingOut, static_cast<int>(agent->state), agent, context);
                continue;
            }
            if (!facility || facility->kind != ObjectKind::Building || !definition)
            {
                FinishTestPlan(plan, SimTestStatus::Rejected, SimTestReject::FacilityNotFound, 0, agent, context);
                continue;
            }
            if (facility->id == agent->targetFacilityId)
            {
                FinishTestPlan(plan, SimTestStatus::Rejected, SimTestReject::SameFacility, 0, agent, context);
                continue;
            }
            // 今回の外出で、もう使った施設には、行かない(立ち寄りと同じルール。向かっている間の使用済みは、履歴の先頭から、立ち寄りの回数ぶん)。
            for (size_t i = 0; i < agent->history.size() && static_cast<int>(i) < agent->outingStopCount; ++i)
            {
                if (agent->history[i].facilityObjectId == facility->id)
                {
                    FinishTestPlan(plan, SimTestStatus::Rejected, SimTestReject::FacilityNotEligible, static_cast<int>(CandidateReject::VisitedThisOuting), agent, context);
                    marker = nullptr;
                    break;
                }
            }
            if (plan.status != SimTestStatus::Pending)
            {
                continue;
            }
            if (!marker || !m_roads || m_roads->empty() || !m_pathfinder)
            {
                FinishTestPlan(plan, SimTestStatus::Rejected, SimTestReject::NoRoute, 0, agent, context);
                continue;
            }
            XMFLOAT3 homePosition = agent->homePosition;
            if (agent->homeObjectId != 0)
            {
                if (const GameObject* home = GameObjectFactory::FindById(*context.objects, agent->homeObjectId)) { homePosition = home->transform.position; }
            }
            const RoadGraph graph = RoadGraph::BuildFromSegments(*m_roads);
            PathCache cache;
            const RouteEndpoint from = ToEndpoint(MakeBuildingSnapshot(0, marker->transform.position, *m_roads, graph));
            const RouteEndpoint to = ToEndpoint(MakeBuildingSnapshot(facility->id, facility->transform.position, *m_roads, graph));
            const RouteEndpoint home = ToEndpoint(MakeBuildingSnapshot(0, homePosition, *m_roads, graph));
            const RoutePlan leg = PlanRoute(from, to, *m_roads, graph, *m_pathfinder, cache);
            const RoutePlan back = leg.valid ? PlanRoute(to, home, *m_roads, graph, *m_pathfinder, cache) : RoutePlan{};
            if (!leg.valid || !back.valid)
            {
                FinishTestPlan(plan, SimTestStatus::Rejected, SimTestReject::NoRoute, 0, agent, context);
                continue;
            }
            // 体力: 新しい行き先までの道のり + そこから家までの帰り(通常の施設選びと同じ判定)。
            if (agent->stamina < RequiredStaminaForWalk(*definition, leg.length + back.length, m_settings))
            {
                FinishTestPlan(plan, SimTestStatus::Rejected, SimTestReject::NotEnoughStamina, static_cast<int>(RequiredStaminaForWalk(*definition, leg.length + back.length, m_settings)), agent, context);
                continue;
            }
            BeginTestPlan(plan, *agent, context);
            agent->targetFacilityId = facility->id;
            if (CategoryOfPurpose(agent->purpose) != definition->category)
            {
                agent->purpose = DefaultPurposeOf(definition->category);
            }
            agent->waypoints = leg.waypoints;
            agent->waypointsHomeToTarget = true;
            agent->currentWaypointIndex = 0;
            agent->segmentIndices = leg.segmentIndices;
            agent->lastDecisionNote = "テストで、行き先を変えられた";
            RecordEvent(*agent, context, SimEventKind::MoveStart, SimEventResult::Info, SimReason::TestApplied, facility->id, leg.length);
            // 外出が終わるまで、実行中。
        }
    }
}

void DemandSystem::FinalizeTestOnOutingEnd(ResidentAgent& agent, GameContext& context)
{
    if (m_testPlans.empty())
    {
        return;
    }
    SimTestPlan* plan = FindActiveTestPlan(agent.objectId);
    if (!plan)
    {
        return;
    }
    if (plan->status == SimTestStatus::Running && plan->forcedStopovers <= 0)
    {
        FinishTestPlan(*plan, SimTestStatus::Succeeded, SimTestReject::None, 0, &agent, context);
    }
    else if (plan->status == SimTestStatus::Running)
    {
        // 強制するはずの立ち寄りの判定が、外出が終わるまで、来なかった(回数が上限・外出が先に終わった など)。
        FinishTestPlan(*plan, SimTestStatus::Rejected, SimTestReject::NotConsumed, plan->forcedStopovers, &agent, context);
    }
    else
    {
        // 待機中のまま、外出が終わった(立ち寄りの指示が、消化されなかった・家にいる住民が、住居なしになった)。
        FinishTestPlan(*plan, SimTestStatus::Rejected,
                       plan->action == SimTestAction::ForceOuting ? SimTestReject::NotAtHome : SimTestReject::NotConsumed, plan->forcedStopovers, &agent, context);
    }
}

void DemandSystem::RunDemandTick(GameContext& context, std::vector<RoadSegment>& segments)
{
    PROFILE_SCOPE("DemandSystem::RunDemandTick");

    ++m_tickIndex;
    CheckAllInvariants(context); // 記録がONの時だけ。住民の状態が壊れていないかを、ティックごとにまとめて調べる
    MaintainTestPlansAtTick(context); // 行動テストの、待機中の計画の前提(家にいる・外出中)・期限を確かめる

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
        if (IsHouseReserved(house.id))
        {
            continue; // 入居者を選んでいる最中の住宅。選び終わるまで、新しい住民は生まれさせない
        }
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
    if (m_debugRecorder.IsEnabled() && context.clock)
    {
        // 記録: 今回のティックで、新しく外出を考える条件が整っていたか(不発の理由が「そもそも考えない時間」だった、を見分けるため)。
        SimTickGate gate;
        gate.canDepart = canDepart && !facilities.empty();
        gate.sleeping = context.clock->IsSleeping();
        gate.tooLate = context.clock->GetTimeOfDayHours() >= m_settings.lastDepartureHour;
        gate.noFacilities = facilities.empty();
        gate.week = context.clock->GetWeekCount();
        gate.hour = context.clock->GetTimeOfDayHours();
        gate.tickIndex = m_tickIndex;
        m_debugRecorder.NoteTickGate(gate);
    }
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

            // 外出を考えるか(確率)。確率の求め方は ComputeOutingChance の1か所(画面の予測値と同じ式)。
            OutingChanceBranch chanceBranch = OutingChanceBranch::NeedDriven;
            const float chance = ComputeOutingChance(agent, m_settings, &chanceBranch);
            const float outingRoll = m_random.Next01(); // 乱数は、行動テストの強制の有無に関わらず、いつも1回引く(他の住民の乱数の流れを、変えないため)
            const bool naturalHit = outingRoll < chance;
            // 行動テスト: この住民に「外出させる」指示が待機中なら、確率の抽選だけを、当選にする(通常ルール。他の条件は、普通に通す)。
            SimTestPlan* testPlan = FindActiveTestPlan(agent.objectId);
            if (testPlan && !(testPlan->action == SimTestAction::ForceOuting && testPlan->status == SimTestStatus::Pending))
            {
                testPlan = nullptr;
            }
            const bool outingHit = naturalHit || (testPlan != nullptr);
            // 記録: 不発も含めて、住民ごとの集計に数える(不発はイベントにしない。毎ティック全住民ぶんになるため)。強制した時も、本当の抽選の結果を数える。
            if (m_debugRecorder.IsEnabled())
            {
                m_debugRecorder.NoteOutingRoll(agent.objectId, chanceBranch, chance, outingRoll, naturalHit, context.clock->GetWeekCount(), context.clock->GetTimeOfDayHours());
            }
            if (!outingHit)
            {
                continue;
            }
            if (testPlan)
            {
                BeginTestPlan(*testPlan, agent, context, outingRoll, chance); // ここから、この外出のイベントに、テストの番号が付く
            }
            RecordEvent(agent, context, SimEventKind::OutingRoll, SimEventResult::Success,
                        chanceBranch == OutingChanceBranch::NeedDriven ? SimReason::OutingNeedDriven : SimReason::OutingSpontaneous, 0, chance, outingRoll);

            if (!home->hasRoadConnection)
            {
                agent.lastDecisionNote = "外出したいが、自宅の近くに道路が無い";
                RecordEvent(agent, context, SimEventKind::OutingBlocked, SimEventResult::NotMet, SimReason::HomeNoRoad);
                if (testPlan) { FinishTestPlan(*testPlan, SimTestStatus::Rejected, SimTestReject::HomeNoRoad, 0, &agent, context); }
                continue;
            }

            // 目的と、候補(今の目的のカテゴリの施設)。
            PurposeTrace purposeTrace;
            OutingPurposeChoice purpose = ChoosePurpose(agent, m_settings, m_random, &purposeTrace);

            // 行動テスト(施設を指定): 目的のカテゴリを、指定した施設のカテゴリに合わせる(乱数は、既に、普通に引いた後)。
            const FacilitySnapshot* forcedFacility = nullptr;
            if (testPlan && testPlan->mode == SimTestMode::ForceFacility)
            {
                for (const FacilitySnapshot& facility : facilities)
                {
                    if (facility.building.id == testPlan->facilityId) { forcedFacility = &facility; break; }
                }
                const FacilityDefinition* forcedDefinition = forcedFacility ? FindFacilityDefinition(forcedFacility->type) : nullptr;
                if (!forcedDefinition)
                {
                    FinishTestPlan(*testPlan, SimTestStatus::Rejected, SimTestReject::FacilityNotFound, 0, &agent, context);
                    continue;
                }
                purpose.category = forcedDefinition->category;
                if (CategoryOfPurpose(purpose.purpose) != forcedDefinition->category)
                {
                    purpose.purpose = DefaultPurposeOf(forcedDefinition->category);
                }
            }
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
                const uint64_t detailId = RecordDecisionDetail(agent, context, purpose, purposeTrace, {}, FacilityChoiceTrace{}, -1, 0, false, 0.0f, -1.0f);
                RecordEvent(agent, context, SimEventKind::PurposeChoice, SimEventResult::Info, SimReason::None, 0, 0.0f, 0.0f, 0.0f, 0, detailId);
                RecordEvent(agent, context, SimEventKind::OutingBlocked, SimEventResult::NotMet, SimReason::NoFacilityInCategory);
                if (testPlan) { FinishTestPlan(*testPlan, SimTestStatus::Rejected, SimTestReject::NoFacilityInCategory, static_cast<int>(purpose.category), &agent, context); }
                continue;
            }

            std::vector<CandidateEvaluation> evaluations;
            FacilityChoiceTrace choiceTrace;
            int chosen = ChooseFacility(agent, candidates, purpose, m_settings, m_random, evaluations, 0, &choiceTrace);

            // 行動テスト(施設を指定): 抽選の結果を無視して、指定した施設を選ぶ。ただし、除外された施設(到達不可・体力不足など)は、選ばない(拒否)。
            bool facilityForced = false;
            if (forcedFacility)
            {
                int forcedIndex = -1;
                for (size_t i = 0; i < candidates.size(); ++i)
                {
                    if (candidates[i].objectId == forcedFacility->building.id) { forcedIndex = static_cast<int>(i); break; }
                }
                if (forcedIndex < 0 || evaluations[static_cast<size_t>(forcedIndex)].reject != CandidateReject::None)
                {
                    // 理由を、判断の内訳に残してから、拒否する(候補の表に、除外の理由が出る)。
                    const uint64_t rejectedDetail = RecordDecisionDetail(agent, context, purpose, purposeTrace, evaluations, choiceTrace, -1, 0, false, 0.0f, -1.0f, false, 1.0f, -1.0f, true, false);
                    RecordEvent(agent, context, SimEventKind::PurposeChoice, SimEventResult::Info, SimReason::None, 0, 0.0f, 0.0f, 0.0f, 0, rejectedDetail);
                    RecordEvent(agent, context, SimEventKind::FacilitySelect, SimEventResult::NotMet, SimReason::NoPickableCandidate, forcedFacility->building.id, 0.0f, 0.0f, 0.0f, 0, rejectedDetail);
                    FinishTestPlan(*testPlan, SimTestStatus::Rejected,
                                   forcedIndex < 0 ? SimTestReject::FacilityNotFound : SimTestReject::FacilityNotEligible,
                                   forcedIndex < 0 ? 0 : static_cast<int>(evaluations[static_cast<size_t>(forcedIndex)].reject), &agent, context);
                    continue;
                }
                for (CandidateEvaluation& evaluation : evaluations) { evaluation.chosen = false; }
                evaluations[static_cast<size_t>(forcedIndex)].chosen = true;
                choiceTrace.pickedIndex = forcedIndex;
                chosen = forcedIndex;
                facilityForced = true;
            }

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

            // 記録: 目的の決定と、施設の抽選(内訳は1件にまとめて、2つのイベントから参照する)。
            const uint64_t decisionDetailId = RecordDecisionDetail(agent, context, purpose, purposeTrace, evaluations, choiceTrace, chosen, 0, false, 0.0f, -1.0f, false, 1.0f, -1.0f, true, facilityForced);
            RecordEvent(agent, context, SimEventKind::PurposeChoice, SimEventResult::Info, SimReason::None, 0, 0.0f, 0.0f, 0.0f, 0, decisionDetailId);

            if (chosen < 0 || !plans[static_cast<size_t>(chosen)].valid)
            {
                agent.lastDecisionNote = "外出したいが、行ける施設が無い(道路でつながっていない/体力が足りない)";
                RecordEvent(agent, context, SimEventKind::FacilitySelect, SimEventResult::NotMet, SimReason::NoPickableCandidate, 0, 0.0f, 0.0f, 0.0f, 0, decisionDetailId);
                if (testPlan) { FinishTestPlan(*testPlan, SimTestStatus::Rejected, SimTestReject::NoPickable, 0, &agent, context); }
                continue;
            }
            RecordEvent(agent, context, SimEventKind::FacilitySelect, SimEventResult::Success, SimReason::None, candidates[static_cast<size_t>(chosen)].objectId,
                        evaluations[static_cast<size_t>(chosen)].weight, evaluations[static_cast<size_t>(chosen)].chance, 0.0f, 0, decisionDetailId);

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
            RecordEvent(agent, context, SimEventKind::MoveStart, SimEventResult::Info, SimReason::None, agent.targetFacilityId, plan.length, 0.0f, 0.0f, 0);

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

uint32_t DemandSystem::FindResidentIdForHome(uint32_t homeObjectId) const
{
    if (homeObjectId == 0)
    {
        return 0;
    }
    for (const ResidentAgent& agent : m_agents)
    {
        if (agent.homeObjectId == homeObjectId)
        {
            return agent.objectId;
        }
    }
    return 0;
}

std::vector<ResidentListItem> DemandSystem::ListResidents() const
{
    std::vector<ResidentListItem> items;
    items.reserve(m_agents.size());
    for (const ResidentAgent& agent : m_agents)
    {
        ResidentListItem item;
        item.residentId = agent.objectId;
        item.homeObjectId = agent.homeObjectId;
        item.state = agent.state;
        items.push_back(item);
    }
    return items;
}

ResidentDebugView DemandSystem::GetResidentDebugView(uint32_t agentObjectId, const std::vector<GameObject>& objects) const
{
    ResidentDebugView view;
    for (const ResidentAgent& agent : m_agents)
    {
        if (agent.objectId != agentObjectId)
        {
            continue;
        }
        view.found = true;
        view.residentId = agent.objectId;
        view.homeObjectId = agent.homeObjectId;
        view.homeExists = (agent.homeObjectId != 0) && GameObjectFactory::FindById(objects, agent.homeObjectId) != nullptr;
        view.homePosition = agent.homePosition;
        view.state = agent.state;

        view.targetFacilityId = agent.targetFacilityId;
        if (agent.targetFacilityId != 0)
        {
            if (const GameObject* target = GameObjectFactory::FindById(objects, agent.targetFacilityId))
            {
                view.targetExists = true;
                view.targetType = target->buildingType;
            }
        }
        view.purpose = agent.purpose;
        view.outingStopCount = agent.outingStopCount;
        view.usedFoodLikeFacilityThisOuting = agent.usedFoodLikeFacilityThisOuting;
        view.visitHoursLeft = agent.visitHoursLeft;

        view.lifeNeed = agent.lifeNeed;
        view.activityNeed = agent.activityNeed;
        view.needThreshold = m_settings.needSatisfiedThreshold;
        view.stamina = agent.stamina;
        view.staminaCap = EffectiveStaminaCap(agent, m_settings);
        view.penaltyLevel = agent.penaltyLevel;
        view.maxPenaltyLevel = m_settings.maxPenaltyLevel;
        view.satisfaction = agent.satisfaction;
        view.unmetLifeHours = agent.unmetLifeHours;
        view.unmetActivityHours = agent.unmetActivityHours;
        view.satisfiedHours = agent.satisfiedHours;
        view.traits = agent.traits;
        view.history = agent.history;
        view.lastDecisionNote = agent.lastDecisionNote;

        if (const GameObject* marker = GameObjectFactory::FindById(objects, agent.objectId))
        {
            view.position = marker->transform.position;
            view.markerVisible = marker->visible;
        }
        view.waypointCount = agent.waypoints.size();
        view.currentWaypointIndex = agent.currentWaypointIndex;
        view.waypointsHomeToTarget = agent.waypointsHomeToTarget;
        view.segmentCount = agent.segmentIndices.size();
        view.routeTotalLength = RouteLength(agent.waypoints);
        if (agent.waypoints.size() >= 2 && agent.currentWaypointIndex + 1 < agent.waypoints.size())
        {
            // 今の位置から次のwaypointまでと、その先の残り。
            float remaining = 0.0f;
            const XMFLOAT3& next = agent.waypoints[agent.currentWaypointIndex + 1];
            const float dx = next.x - view.position.x;
            const float dz = next.z - view.position.z;
            remaining += std::sqrt(dx * dx + dz * dz);
            for (size_t i = agent.currentWaypointIndex + 1; i + 1 < agent.waypoints.size(); ++i)
            {
                const float sx = agent.waypoints[i + 1].x - agent.waypoints[i].x;
                const float sz = agent.waypoints[i + 1].z - agent.waypoints[i].z;
                remaining += std::sqrt(sx * sx + sz * sz);
            }
            view.routeRemainingLength = remaining;
        }

        view.nextOutingChance = ComputeOutingChance(agent, m_settings, &view.nextOutingBranch);
        return view;
    }
    return view;
}

std::vector<HomelessResidentInfo> DemandSystem::ListHomelessResidents() const
{
    std::vector<HomelessResidentInfo> list;
    for (const ResidentAgent& agent : m_agents)
    {
        // 住居なしで、元の位置に立っている住民だけ(外出中の住民は、帰り着いて、住居なしになってから)。
        if (agent.state != ResidentAgentState::Homeless || agent.homeObjectId != 0)
        {
            continue;
        }
        HomelessResidentInfo info;
        info.residentId = agent.objectId;
        info.satisfaction = agent.satisfaction;
        info.stamina = agent.stamina;
        info.staminaCap = EffectiveStaminaCap(agent, m_settings);
        info.lifeNeed = agent.lifeNeed;
        info.activityNeed = agent.activityNeed;
        info.penaltyLevel = agent.penaltyLevel;
        info.needUnmet = IsLifeNeedUnmet(agent, m_settings) || IsActivityNeedUnmet(agent, m_settings);
        list.push_back(info);
    }
    return list;
}

int DemandSystem::CountHomelessResidents() const
{
    int count = 0;
    for (const ResidentAgent& agent : m_agents)
    {
        if (agent.state == ResidentAgentState::Homeless && agent.homeObjectId == 0)
        {
            ++count;
        }
    }
    return count;
}

void DemandSystem::ReserveHouse(uint32_t houseObjectId)
{
    if (houseObjectId != 0 && !IsHouseReserved(houseObjectId))
    {
        m_reservedHouses.push_back(houseObjectId);
    }
}

void DemandSystem::ReleaseHouse(uint32_t houseObjectId)
{
    m_reservedHouses.erase(std::remove(m_reservedHouses.begin(), m_reservedHouses.end(), houseObjectId), m_reservedHouses.end());
}

bool DemandSystem::IsHouseReserved(uint32_t houseObjectId) const
{
    return std::find(m_reservedHouses.begin(), m_reservedHouses.end(), houseObjectId) != m_reservedHouses.end();
}

bool DemandSystem::MoveInResident(uint32_t residentObjectId, uint32_t houseObjectId, GameContext& context)
{
    if (!context.objects)
    {
        return false;
    }
    const GameObject* house = GameObjectFactory::FindById(*context.objects, houseObjectId);
    if (!house || house->kind != ObjectKind::Building || house->buildingType != BuildingType::House)
    {
        return false;
    }
    // 1軒に住める人数(今は1人)。既に満員なら、入れない。
    const int capacity = (std::max)(1, GetBuildingDefinition(BuildingType::House).representativeAgentCount);
    int occupants = 0;
    ResidentAgent* target = nullptr;
    for (ResidentAgent& agent : m_agents)
    {
        if (agent.homeObjectId == houseObjectId) { ++occupants; }
        if (agent.objectId == residentObjectId) { target = &agent; }
    }
    if (!target || occupants >= capacity || target->state != ResidentAgentState::Homeless || target->homeObjectId != 0)
    {
        return false;
    }

    ResidentAgent& agent = *target;
    RecordEvent(agent, context, SimEventKind::MovedIn, SimEventResult::Success, SimReason::None, houseObjectId); // 家のidを変える前の写し
    agent.homeObjectId = houseObjectId;
    agent.lastDecisionNote = "新しい住宅へ入居する";
    // 今の位置から、新しい家へ、道路を歩いて向かう(位置は飛ばさない)。道が無ければ立ち往生(満足度は下げない)。
    // 到着すれば、通常の帰宅と同じ処理で、家にいる状態になる(家のidが0でないので、住居なしにならない)。
    StartReturnFromHere(agent, context, false);
    if (agent.state == ResidentAgentState::Stranded)
    {
        agent.lastDecisionNote = "新しい住宅へ行く道が無い(道がつながれば、向かう)";
    }
    ReleaseHouse(houseObjectId);
    return true;
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