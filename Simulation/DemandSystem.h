#pragma once
#include <vector>
#include <string>
#include <DirectXMath.h>
#include "ResidentAgent.h"
#include "ResidentSchedule.h"
#include "ResidentSimSettings.h"
#include "ResidentLogic.h"
#include "FacilityLedger.h"
#include "BuildingType.h"
#include "RoadCutting.h"
#include "RoutePlanner.h"
#include "ISaveable.h"

struct GameContext;
class GameObject;
struct RoadSegment;
class IPathfinder;
class Model;
class DebugRenderer;

// 建物1つ分の「道路への接続線」の表示用データ(建物位置→接続点)。
struct RoadConnectionLine
{
    DirectX::XMFLOAT3 buildingPosition;
    DirectX::XMFLOAT3 connectionPoint;
};

// HUD・メニュー・City Statsパネルに表示する集計値。
struct DemandStats
{
    int houseCount = 0;       // 配置されているHouseの数
    int officeCount = 0;      // 配置されているOfficeの数(仕事の概念は停止中)
    int facilityCount = 0;    // 配置されている施設(コンビニ・スーパー・映画館・カフェ)の数
    int populationTotal = 0;  // House.populationRepresentedの合計(表示上の人口)
    int agentCount = 0;       // 実際にシミュレートしている住民の数
    int unmetDemand = 0;      // 生活またはアクティビティの需要が不足(80未満)の住民の数
    int homelessCount = 0;    // 住んでいた住宅が解体されて、住居が無い住民の数(agentCountに含まれる)
    int strandedCount = 0;    // 道路が無くなって、帰れなくなった(立ち往生)住民の数(agentCountに含まれる)

    // 施設の利用の集計(住民が実際に利用した結果)。
    int visitsThisWeek = 0;
    int salesThisWeek = 0;
    int visitsLastWeek = 0;
    int salesLastWeek = 0;
    int visitsThisMonth = 0;
    int salesThisMonth = 0;

    // ---- 停止中の仕事の概念(今は使わない。0のまま) ----
    int jobCapacityTotal = 0;
    int jobsFilled = 0;
};

// Profilerのメモリ内訳表示(経路探索・人流)用の集計値。
struct DemandMemoryStats
{
    int agentCount = 0;            // 住民の数
    size_t totalWaypoints = 0;     // 全住民の移動経路(waypoints)の合計点数
    int roadGraphNodeCount = 0;    // 直近の経路探索グラフのノード数
    int roadGraphEdgeCount = 0;    // 直近の経路探索グラフのエッジ数
};

// 住民1人の、表示用のコピー(ゲーム内の「住民の情報」パネル用。DemandSystemの内部を、画面側に見せないための橋渡し)。
struct ResidentInfo
{
    bool found = false;                     // その住宅に住む住民が見つかったか
    uint32_t homeObjectId = 0;
    ResidentAgentState state = ResidentAgentState::AtHome;
    bool homeless = false;                  // 住居が無い(住宅が解体された)住民か
    float satisfaction = 100.0f;            // 満足度(今は、道路が無くなった時に下がるだけ)
    uint32_t targetFacilityId = 0;          // 向かっている(利用中の)施設のid(0=なし)
    UsagePurpose purpose = UsagePurpose::TopUp;
    float lifeNeed = 0.0f;
    float activityNeed = 0.0f;
    float needThreshold = 80.0f;            // 充足とみなす値
    float stamina = 0.0f;
    float staminaCap = 0.0f;                // 今の体力の上限(ペナルティ込み)
    int penaltyLevel = 0;
    int maxPenaltyLevel = 0;
    float unmetLifeHours = 0.0f;            // 不足が続いている時間
    float unmetActivityHours = 0.0f;
    float satisfiedHours = 0.0f;            // 両方が充足している状態が続いている時間
    ResidentTraits traits;
    std::string lastDecisionNote;
    std::vector<VisitRecord> history;       // 最近使った施設(新しい順)
};

// 施設選びの記録1回分(デバッグ用。「誰が・何を目的に・どの施設を・どんな重みで選んだか」を、後から追えるようにする)。
struct DecisionLogEntry
{
    uint32_t homeObjectId = 0;                  // 外出を決めた住民の自宅のid
    int weekCount = 0;                          // 通算の週
    float hour = 0.0f;                          // 時刻
    bool needDriven = false;                    // 需要の不足による外出か(falseなら自発的な外出)
    FacilityCategory category = FacilityCategory::Daily;
    UsagePurpose purpose = UsagePurpose::TopUp;
    float stamina = 0.0f;                       // 決めた時の体力
    float lifeNeed = 0.0f;
    float activityNeed = 0.0f;
    std::vector<CandidateEvaluation> evaluations; // 全ての候補の評価(除外の理由・重み・確率・選ばれたか)
    int chosenIndex = -1;                       // 選ばれた候補の添字(-1=選べなかった)
    bool stopover = false;                      // 立ち寄りの選択か(falseなら、外出の主目的の施設選び)
    int stopIndex = 0;                          // 立ち寄りの何回目か(1から。stopoverがtrueの時だけ)
    float stopChance = 0.0f;                    // 立ち寄りを考える確率(判定の時の値。stopoverがtrueの時だけ)
    std::string note;                           // 補足(「食事系が続くので、寄らなかった」など)
};

// 解体された建物1棟ぶんの情報。建物を消す前に、呼び出し側が集めて渡す(消した後は、位置も種類も分からなくなるため)。
struct DemolishedBuilding
{
    uint32_t id = 0;                            // GameObject::id
    BuildingType type = BuildingType::House;
    DirectX::XMFLOAT3 position{};               // 建物の位置(住宅なら、住民が出現する場所)
};

// 住民の生成・外出・施設の利用・売上、需要と体力、週の処理を管理するクラス。
// Game::Update/Drawから毎フレーム呼ばれる(BuildController/RoadSystemと同じ所有のされ方)。
// 仕事(通勤)の概念は一旦停止中: 関連するコードは、消さずに、無効にして残してある(Office建物は建てられるが、何も起きない)。
class DemandSystem : public ISaveable
{
public:
    // 使用する経路探索アルゴリズムと、住民の見た目に使うモデルを渡す
    // (経路探索アルゴリズムは差し替え可能にするため、具体的なアルゴリズムはここでは知らない)。
    void Initialize(IPathfinder* pathfinder, Model* agentModel);

    // deltaTime: 前フレームからの経過秒数(ゲーム内時間の倍率をかけた後の値。GameClock::GetScaledDeltaSecondsを渡す。
    // 停止中は0、倍速中は大きくなり、住民の移動と需要更新の間隔の両方がそれに従う)。
    // segments: 現在配置されている道路区間(読み取り+debugLoad書き込み)。
    // 週末・週の始まりの処理と、施設の利用時間は、context.clock(GameClock)の時刻・イベントで決まる。
    void Update(float deltaTime, GameContext& context, std::vector<RoadSegment>& segments);

    // Editorの「City Stats」パネル(まとめ・設定・施設の集計・住民・施設選びの記録)。
    // openがnullptrでなければ、タイトルバーの×で閉じられ、*openがfalseの間は描かない。
    void DrawDebugUI(bool* open = nullptr);

    // 住民・統計・接続線・更新タイマー・集計を初期状態に戻す(セーブデータの読み込み前の全消去に使う)。
    // 住民の見た目のGameObject(Agent)の削除は呼び出し側が行う。
    void Reset();

    // ---- セーブ/ロード(ISaveable) ----
    // 住民ごとに、家(建物のuid)、状態、内部パラメータ(特徴・体力・需要・ペナルティ・履歴)、今の外出(施設・経路・位置)、
    // 見た目のオブジェクトの位置を保存する。施設ごとの来訪者数・売上の集計も保存する。
    // 統計と接続線は次の需要の更新で再計算されるので保存しない。
    const char* SaveKey() const override { return "demand"; }
    void Save(Json& section, const SaveContext& context) const override;
    void Load(const Json& section, int version, const LoadContext& context) override;
    void ResetToDefault() override { Reset(); }

    // 直近の集計(人口・住民の数・施設の利用の集計など)のコピーを返す。HUD・メニューの表示に使う。
    DemandStats GetStats() const;

    // 調整用の設定(Editorの「City Stats」パネルで変えられる)と、施設ごとの集計。
    ResidentSimSettings& GetSettings() { return m_settings; }
    const ResidentSimSettings& GetSettings() const { return m_settings; }
    const FacilityLedger& GetLedger() const { return m_ledger; }

    // 建物-道路の接続線(入口/私道)を毎フレーム描画する。RunDemandTickで計算した結果を使う。
    void Draw(DebugRenderer& debugRenderer) const;

    // 建物が解体された(ゲームのオブジェクトからは、もう消した後)。住民・集計を、安全に整える。
    //  ・住宅: 住んでいた住民は、消さない。家にいた住民は、元の位置に出現して、住居が無い状態(Homeless)になる。外出中の住民は、
    //    今の外出を続け、帰り着いた時に、元の位置で、住居が無い状態になる(途中で、位置を飛ばさない)。
    //  ・施設: 向かっていた・利用中の住民は、利用せず(売上・回復なし)、その場から帰宅を始める。確定した過去の売上・来訪者数は、記録として残す。
    // 道路・占有・オブジェクトの削除は、呼び出し側が行う。
    void OnBuildingDemolished(const DemolishedBuilding& building, GameContext& context);

    // 道路が切り取られた(RoadSystem::ApplyCutの後)。住民の、無効になった経路を、安全に整える(暫定の仕様)。
    //  ・全員の、道路区間の添字(混雑表示用)を、空にする(区間の並びが変わったため)。
    //  ・歩いている・施設の中の住民のうち、これからの経路が、取り除いた部分(removed)を通る人は、今の外出をやめて、帰宅を優先する。
    //    今の位置の近くの、残った道路から、自宅(家が無ければ、元の家の位置)への経路を探し直す。
    //    帰れる: 迂回して帰る(体力を消費し、満足度が下がる)。帰れない: 立ち往生(その場の、道路の上で、動かない。満足度が下がる。道がつながれば、帰り始める)。位置は、ワープさせない。
    //  ・施設の中にいる住民は、そのまま(利用が終わって、帰り始める時に、帰れなければ、その時に、立ち往生する)。
    // segments: 切り取った後の道路区間。
    void OnRoadsChanged(const std::vector<RemovedRoadSpan>& removed, const std::vector<RoadSegment>& segments, GameContext& context);

    // 直近の需要の更新(RunDemandTick)で、近くに道路が無かった建物(住宅・施設)のid。「道路がない！」の表示に使う。
    const std::vector<uint32_t>& GetBuildingsWithoutRoad() const { return m_buildingsWithoutRoad; }

    // 住居が無い(Homeless)住民の、見た目のオブジェクト(GameObject::id)の一覧。「住居がない！」を、住民の上に出すのに使う。
    std::vector<uint32_t> GetHomelessAgentObjectIds() const;

    // 立ち往生(道路が無くなって帰れない)住民の、見た目のオブジェクト(GameObject::id)の一覧。「立ち往生！」を、住民の上に出すのに使う。
    std::vector<uint32_t> GetStrandedAgentObjectIds() const;

    // 住宅(GameObject::id)に住む住民の、表示用のコピーを返す(いなければ found==false)。
    ResidentInfo GetResidentInfoForHome(uint32_t homeObjectId) const;

    // 住民の見た目のオブジェクト(Agent。GameObject::id)で探す版。画面で住民をクリックした時に使う。
    ResidentInfo GetResidentInfoForAgent(uint32_t agentObjectId) const;

    // Inspector表示用: idで指定した住民の状態を文字列で返す(見つからなければ空文字)。
    std::string GetAgentDebugSummary(uint32_t objectId) const;

    // Profilerのメモリ内訳表示用の集計値を返す。
    DemandMemoryStats GetMemoryStats() const;

private:
    // 施設の利用が終わって、帰り始める前に、今の道路で、施設から自宅への経路を作り直す(道路が切り取られて、行きの経路が使えなくなっていることがあるため)。
    // 経路が見つかれば、その経路で帰る(元より長ければ、迂回なので、体力を消費し、満足度が下がる)。見つからなければ、立ち往生にして、falseを返す。
    // applyDetourPenalty: 元の経路(今の保存してある経路)より長ければ、迂回の体力の消費・満足度の低下を与えるか。
    // 立ち寄りの途中では、保存してある経路が「前の施設→今の施設」の短い経路なので、falseにする(家までの道のりと比べられない)。
    bool ReplanReturnRoute(ResidentAgent& agent, GameContext& context, bool applyDetourPenalty = true);

    // 主目的の施設を使い終わった住民が、低い確率で、近くの施設に立ち寄る(P1)。立ち寄るなら、その施設へ歩き始めて、trueを返す。
    // 範囲(今いる施設から)・体力(立ち寄り先 + 家までの帰り)・時刻・食事系の連続は、設定とResidentLogicの関数で判定する。currentFacilityは、今いる施設。
    bool TryStartStopover(ResidentAgent& agent, GameContext& context, const GameObject& currentFacility);

    // 外出の途中(立ち寄りへ向かう途中など)で、行き先が無くなった時に、今の位置から家へ帰り始める(道が無ければ立ち往生)。位置は飛ばさない。
    void StartReturnFromHere(ResidentAgent& agent, GameContext& context);

    // 立ち往生している住民が、道がつながって帰れるようになったかを調べ、帰れるなら、帰り始めさせる(RunDemandTickが、毎回呼ぶ)。
    // 今の位置の近くの道路から、自宅(家が無ければ、元の家の位置)への経路を探す。見つかれば、その経路で歩いて帰る(帰り着けば、家にいる・住居なしになる)。
    // 帰り始めたらtrue。
    bool TryRecoverStranded(ResidentAgent& agent, GameContext& context, const std::vector<RoadSegment>& segments, const RoadGraph& graph, PathCache& cache);

    // 住民を、立ち往生にする(道路の上に立たせ、動かない・施設を使わない状態にして、満足度を下げる)。
    // 今の位置が、道路の上でない(切り取られた跡・施設の中など)時は、近くの、残った道路の上に寄せる(遠くへは、ワープさせない)。
    void BecomeStranded(ResidentAgent& agent, GameContext& context, const std::vector<RoadSegment>& segments);

    // 住民を、住居が無い状態にする(元の住宅の位置に、見える状態で立たせる。同じ位置に何人もいる時は、少しずつずらす)。
    void BecomeHomeless(ResidentAgent& agent, GameContext& context);

    // 向かっていた・利用中だった施設が無くなった住民の、今の外出をやめる(売上・回復なし。その場から帰宅を始める)。
    void AbortOutingToDemolishedFacility(ResidentAgent& agent, GameContext& context);

    // 住民1人の、表示用のコピーを作る。
    ResidentInfo MakeResidentInfo(const ResidentAgent& agent) const;

    // 週末・週の始まりのイベント(GameClock)を受け取って、住民の需要・ペナルティ・体力と、施設の集計を更新する。
    void ProcessClockEvents(GameContext& context);

    // 毎フレーム: 活動時間が進んだ分だけ、住民の需要の不足の継続・体力の回復を進める。
    void AdvanceAgentTimers(GameContext& context);

    // 毎フレーム: 外出・帰宅中の住民を、なめらかに移動させる。施設に着いたら利用を始め、終わったら帰らせる。
    void UpdateAgentMovement(float deltaTime, GameContext& context);

    // 住民の移動(外出/帰宅)を始める。経路を進行方向に合わせて並べ替え、出発地点に置く。
    void StartMove(ResidentAgent& agent, bool toTarget, GameObject* obj) const;

    // 施設の利用が終わった: 需要・体力・履歴・集計・資金に反映して、帰宅を始めさせる。
    void FinishVisit(ResidentAgent& agent, GameContext& context, GameObject* obj);

    // 数秒おきに呼ぶ: 新しい住民の生成、外出の判定と施設選び、統計更新を行う(重い処理はここだけ)。
    void RunDemandTick(GameContext& context, std::vector<RoadSegment>& segments);

    // 記録を足す(古いものから捨てる)。
    void PushDecisionLog(DecisionLogEntry entry);

    // ---- Editorのパネル(DemandSystemDebug.cpp) ----
    void DrawSettingsTab();
    void DrawFacilitiesTab();
    void DrawResidentsTab();
    void DrawDecisionsTab();

    IPathfinder* m_pathfinder = nullptr;
    Model* m_agentModel = nullptr;
    std::vector<ResidentAgent> m_agents;

    // 仕事の概念(出勤・退勤の予定表)は、一旦停止中。今は使わない(消さずに残してある)。
    ResidentSchedule m_schedule;

    ResidentSimSettings m_settings;         // 調整用の設定
    SimRandom m_random{ 12345 };            // 施設選びなどの乱数(settings.seedから)
    FacilityLedger m_ledger;                // 施設ごとの来訪者数・売上
    int m_weeksEnded = 0;                   // 処理した週末の回数(月末の判定に使う。セーブされる)

    float m_tickTimer = 0.0f;
    static constexpr float kTickInterval = 3.0f; // 3秒ごとに需要の再計算を行う

    DemandStats m_lastStats;

    // 直近のRunDemandTickで見つかった、道路に接続している建物の接続線一覧(Draw()で使う)。
    std::vector<RoadConnectionLine> m_connectionLines;

    // 今の道路区間(Updateで受け取った物を、借りているだけ。経路の作り直しに使う)。
    const std::vector<RoadSegment>* m_roads = nullptr;

    // 道路に接続できていない建物のid(直近のRunDemandTickの結果。保存しない)。
    std::vector<uint32_t> m_buildingsWithoutRoad;

    // 施設選びの記録(新しいものが後ろ。デバッグ用。保存しない)。
    std::vector<DecisionLogEntry> m_decisionLog;

    // 最後に時計から読んだ、通算の週(デバッグ表示用)。
    int m_lastWeekCount = 1;

    // Profilerのメモリ内訳表示用。直近のRunDemandTickで作った経路探索グラフの規模を覚えておく
    // (グラフ自体はRunDemandTick内のローカル変数のため、規模だけをここに残す)。
    int m_lastRoadGraphNodeCount = 0;
    int m_lastRoadGraphEdgeCount = 0;
};
