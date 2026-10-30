#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <DirectXMath.h>
#include "ResidentAgent.h"
#include "ResidentSchedule.h"
#include "ResidentSimSettings.h"
#include "ResidentLogic.h"
#include "FacilityLedger.h"
#include "BuildingType.h"
#include "RoadCutting.h"
#include "RoutePlanner.h"
#include "SimDebugRecorder.h"
#include <functional>
#include "SimRunDigest.h"
#include "SimStats.h"
#include "SimTest.h"
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

// 住民1人の、デバッグ表示用の「今の値」のコピー(Editorの「住民デバッグ」パネル用)。過去の判断の値は、SimDebugRecorderの記録が持つ。
// 住民の内部(ResidentAgent)を画面側へ見せないための橋渡し。
struct ResidentDebugView
{
    bool found = false;                     // 住民が見つかったか
    uint32_t residentId = 0;                // 見た目のオブジェクトのid(住民の識別)
    uint32_t homeObjectId = 0;              // 住宅のid(0=住居なし)
    bool homeExists = false;                // 住宅のオブジェクトが今あるか
    DirectX::XMFLOAT3 homePosition{};       // 住宅が解体された時の元の位置(homeObjectIdが0の間だけ意味を持つ)
    ResidentAgentState state = ResidentAgentState::AtHome;

    // 今の外出。
    uint32_t targetFacilityId = 0;
    bool targetExists = false;              // 行き先の施設のオブジェクトが今あるか
    BuildingType targetType = BuildingType::Shop; // targetExistsの時だけ
    UsagePurpose purpose = UsagePurpose::TopUp;
    int outingStopCount = 0;
    bool usedFoodLikeFacilityThisOuting = false;
    float visitHoursLeft = 0.0f;            // 施設の中に、あと何ゲーム時間いるか

    // 需要・体力など。
    float lifeNeed = 0.0f;
    float activityNeed = 0.0f;
    float needThreshold = 80.0f;
    float stamina = 0.0f;
    float staminaCap = 0.0f;                // ペナルティ込みの上限
    int penaltyLevel = 0;
    int maxPenaltyLevel = 0;
    float satisfaction = 0.0f;
    float unmetLifeHours = 0.0f;
    float unmetActivityHours = 0.0f;
    float satisfiedHours = 0.0f;
    ResidentTraits traits;
    std::vector<VisitRecord> history;       // 最近使った施設(新しい順)
    std::string lastDecisionNote;

    // 位置・経路。
    DirectX::XMFLOAT3 position{};           // 見た目のオブジェクトの位置
    bool markerVisible = false;             // 見た目が表示されているか(家・施設の中では非表示)
    size_t waypointCount = 0;
    size_t currentWaypointIndex = 0;
    bool waypointsHomeToTarget = true;
    size_t segmentCount = 0;                // 経路が通る道路区間の数
    float routeTotalLength = 0.0f;          // 経路の長さの合計
    float routeRemainingLength = 0.0f;      // 今の位置から、経路の終わりまでの残りの長さ

    // 次の外出判定(家にいる時の予測。実処理と同じ ComputeOutingChance で計算)。
    float nextOutingChance = 0.0f;
    OutingChanceBranch nextOutingBranch = OutingChanceBranch::NeedDriven;
};

// 住民一覧(デバッグの選択用)の1件。
struct ResidentListItem
{
    uint32_t residentId = 0;
    uint32_t homeObjectId = 0;
    ResidentAgentState state = ResidentAgentState::AtHome;
};

// 住居が無い(Homeless)住民1人の、入居先を選ぶ画面に出す情報(表示用のコピー)。
struct HomelessResidentInfo
{
    uint32_t residentId = 0;        // 住民の見た目のオブジェクトのid(入居させる時に渡す)
    float satisfaction = 0.0f;
    float stamina = 0.0f;
    float staminaCap = 0.0f;
    float lifeNeed = 0.0f;
    float activityNeed = 0.0f;
    int penaltyLevel = 0;
    bool needUnmet = false;         // 生活かアクティビティの需要が不足しているか
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
// 乱数の取り方(新しい世界を始める時 = ゲームの開始・ロード・リセット)。
//  ・Reseed       通常のプレイ。新しい予測しにくい種で始める(同じセーブを読み直しても、その後の抽選結果が変わる)。既定。
//  ・FixedSeed    再現テスト用。設定の種(settings.seed)で始める(同じ種・同じ配置なら、同じ選択が再現される)。
//  ・RestoreState 再現テスト用。セーブに残した乱数の内部状態を復元する(セーブした位置から、同じ乱数列を続ける)。セーブに無ければ設定の種で始める。
// 擬似乱数であり、「真の乱数」ではない(種が同じなら、同じ列になる)。
enum class SimRandomMode : uint8_t
{
    Reseed,
    FixedSeed,
    RestoreState
};

// 通常プレイ用の、新しい種を作る(時刻・OSの乱数から。0にならない)。
uint32_t MakeFreshRandomSeed();

class DemandSystem : public ISaveable
{
public:
    // 記録部に、統計を観察者として付ける(メンバーの住所を渡すので、コピー・移動は禁止)。
    DemandSystem() { m_debugRecorder.AddObserver(&m_stats); m_debugRecorder.AddObserver(&m_digest); }
    DemandSystem(const DemandSystem&) = delete;
    DemandSystem& operator=(const DemandSystem&) = delete;

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

    // ---- 入居(住居が無い住民を、新しい住宅へ入れる) ----
    // 住宅を解体された住民は、住居なし(Homeless)になる。新しい住宅を建てた時に、その中から、誰を入居させるかを、プレイヤーが選べる。

    // 今、入居できる住民(住居なしで、元の位置に立っている住民)の一覧。外出中に家が解体された住民は、帰り着いて住居なしになってから、載る。
    std::vector<HomelessResidentInfo> ListHomelessResidents() const;
    int CountHomelessResidents() const;

    // 新しい住宅を「入居者を選ぶ待ち」にする間、その住宅に、新しい住民を生まれさせない。選び終わったら ReleaseHouse(MoveInResident も、解除する)。
    // 待ちの住宅は、リセット・ロード・解体で、自動的に外れる(セーブはしない)。
    void ReserveHouse(uint32_t houseObjectId);
    void ReleaseHouse(uint32_t houseObjectId);
    bool IsHouseReserved(uint32_t houseObjectId) const;

    // 住居なしの住民を、住宅に入居させる。住民は、今の位置から、道路を歩いて、新しい家へ向かう(位置は飛ばさない)。
    // 道が無ければ、立ち往生(満足度は下げない)になり、道がつながれば、向かう。住宅の種類がHouseでない・既に住民がいる・
    // 住民が住居なしでない時は、何もせずfalse。成功したら、待ちの解除(ReleaseHouse)もする。
    bool MoveInResident(uint32_t residentObjectId, uint32_t houseObjectId, GameContext& context);

    // Inspector表示用: idで指定した住民の状態を文字列で返す(見つからなければ空文字)。
    std::string GetAgentDebugSummary(uint32_t objectId) const;

    // Profilerのメモリ内訳表示用の集計値を返す。
    DemandMemoryStats GetMemoryStats() const;

    // ---- デバッグ(Editorの「住民デバッグ」パネル用。ゲームの動作には関わらない) ----

    // 判断・状態遷移の記録。ON/OFFの切り替え、容量の変更、読み取りに使う。既定は無効(Editorのビルドが有効にする)。
    SimDebugRecorder& GetDebugRecorder() { return m_debugRecorder; }
    const SimDebugRecorder& GetDebugRecorder() const { return m_debugRecorder; }

    // 統計(段階4)。記録と同時に、軽いカウンターを足していく(記録がONの間だけ)。表示は DebugTools/SimStatsPanel。
    SimStats& GetBehaviorStats() { return m_stats; }
    const SimStats& GetBehaviorStats() const { return m_stats; }

    // 住民(見た目のオブジェクトのid)の、今の値のコピー。見つからなければ found==false。objectsは、家・施設・見た目の有無を調べるのに使う。
    ResidentDebugView GetResidentDebugView(uint32_t agentObjectId, const std::vector<GameObject>& objects) const;

    // 住宅に住む住民の見た目のオブジェクトのid(いなければ0)。住宅を選んだ時に、その住民をデバッグ対象にするのに使う。
    uint32_t FindResidentIdForHome(uint32_t homeObjectId) const;

    // 全住民の一覧(選択用)。
    std::vector<ResidentListItem> ListResidents() const;

    // 乱数の内部状態(記録のON/OFFで乱数の消費が変わらないことの確認に使う)。
    uint32_t GetRandomState() const { return m_random.GetState(); }

    // ---- 乱数・再現(段階5) ----
    // 新しい世界(ゲームの開始・ロード・リセット)を始める時の、乱数の取り方。既定は Reseed(ロードで、抽選の結果が変わる)。
    void SetRandomMode(SimRandomMode mode) { m_randomMode = mode; }
    SimRandomMode GetRandomMode() const { return m_randomMode; }
    // 今の世界の乱数の種(世界を始めた時に決めた値。FixedSeed に入れれば、同じ列で始められる)。
    uint32_t GetActiveSeed() const { return m_activeSeed; }
    // 今の乱数の内部状態を、指定の値に戻す(再現テスト用)。
    void RestoreRandomState(uint32_t state) { m_random.SetState(state); }
    // 世界を始めてからの、イベント・外出判定の「指紋」(再現できたかの確認用)。
    SimRunDigestResult GetRunDigest() const { return m_digest.Result(); }
    const std::vector<SimDigestTraceEntry>& GetRunDigestTrace() const { return m_digest.Trace(); }
    // 行動テストの指示を出した時(画面から)に呼ばれる。再現セッションの記録用。再生中の指示では、呼ばれない。
    void SetOnTestCommandSubmitted(std::function<void(const SimTestCommand&, uint32_t)> callback) { m_onTestSubmitted = std::move(callback); }
    // 再生用: 記録しないで、指示を出す。
    SimTestSubmitResult SubmitTestCommandSilently(const SimTestCommand& command, uint32_t selectedResidentId, const std::vector<GameObject>& objects);

    // ---- 行動テスト(段階3。Editorの「住民デバッグ」の「行動テスト」タブ用。詳しくは SimTest.h) ----
    // 指示を、対象の住民ごとの「計画」にして、待機させる(ここでは、住民の状態を変えない)。計画は、次の安全なタイミング
    // (外出判定・立ち寄り判定・更新の最初)で、1回だけ消化される。selectedResidentId: target==Selected の時の住民。
    SimTestSubmitResult SubmitTestCommand(const SimTestCommand& command, uint32_t selectedResidentId, const std::vector<GameObject>& objects);

    // 待機中・実行中の計画を、取り消す(batchId=指示のまとまり / testId=1件)。取り消した件数を返す。
    // 実行中の計画は、強制を外す(既に始まった外出・行動は、そのまま続く)。
    int CancelTestBatch(uint32_t batchId);
    int CancelTestPlan(uint32_t testId);

    // 計画の一覧(古い物から。待機中・実行中・終わった物。上限を超えると、終わった古い物から捨てる)と、終わった物を消す操作。
    const std::vector<SimTestPlan>& GetTestPlans() const { return m_testPlans; }
    void ClearFinishedTestPlans();
    int CountActiveTestPlans() const;

    // 四角(ワールドのXZ)の中にいる住民の人数(今の位置。「範囲」の指示を出す前の、目安の表示用)。
    int CountResidentsInRect(float minX, float minZ, float maxX, float maxZ, const std::vector<GameObject>& objects) const;

private:
    // ---- 記録(SimDebugRecorder)への窓口。記録がOFFなら、すぐ戻る。乱数は引かない・状態は変えない ----

    // 住民の今の状態の写しを作る。
    SimResidentSnapshot MakeSnapshot(const ResidentAgent& agent) const;

    // イベントを1件記録する。stopIndexが負なら、住民の今の立ち寄り回数を使う。
    uint64_t RecordEvent(const ResidentAgent& agent, const GameContext& context, SimEventKind kind, SimEventResult result,
                         SimReason reason = SimReason::None, uint32_t facilityId = 0, float a = 0.0f, float b = 0.0f, float c = 0.0f,
                         int stopIndex = -1, uint64_t detailId = 0, SimAnomaly anomaly = SimAnomaly::None);

    // 施設選び1回ぶんの、判断の内訳を作って記録する(記録がOFFなら0を返す)。
    uint64_t RecordDecisionDetail(const ResidentAgent& agent, const GameContext& context, const OutingPurposeChoice& purpose, const PurposeTrace& purposeTrace,
                                  const std::vector<CandidateEvaluation>& evaluations, const FacilityChoiceTrace& choice, int chosenIndex,
                                  int stopIndex, bool isStopover, float stopChance, float stopRoll,
                                  bool foodChecked = false, float foodAccept = 1.0f, float foodRoll = -1.0f, bool foodPassed = true, bool forcedFacility = false);

    // 住民1人の不変条件を調べ、新しく違反になったものをAnomalyイベントとして記録する(記録がOFFなら何もしない)。
    // index: オブジェクトのid → オブジェクトの表(全住民をまとめて調べる時だけ渡す。nullptrなら、その都度探す)。
    void CheckAgentInvariants(const ResidentAgent& agent, const GameContext& context, const std::unordered_map<uint32_t, const GameObject*>* index = nullptr);

    // 全住民について、CheckAgentInvariants を行う(ティックごと)。
    void CheckAllInvariants(const GameContext& context);

    // 経路の長さの合計(ワールド単位)。
    static float RouteLength(const std::vector<DirectX::XMFLOAT3>& waypoints);

    // ---- 行動テストの内部(SimTest.h) ----
    // 住民に、待機中・実行中の計画があれば、返す(無ければnullptr)。
    SimTestPlan* FindActiveTestPlan(uint32_t residentId);
    // 待機中の計画を、実行中にする(消化した)。住民に、テストの番号を付け(以後のイベントに付く)、TestCommandイベントを記録する。roll/chanceは、強制した確率の抽選の乱数と確率。
    void BeginTestPlan(SimTestPlan& plan, const ResidentAgent& agent, const GameContext& context, float roll = -1.0f, float chance = 0.0f);
    // 計画を終える(成功・拒否・取消・期限切れ)。agentがあれば、拒否のTestCommandイベントを記録し、テストの番号を外す。
    void FinishTestPlan(SimTestPlan& plan, SimTestStatus status, SimTestReject reject, int detail, const ResidentAgent* agent, const GameContext& context);
    // 更新の最初に、待機中の「すぐ消化できる指示」(帰宅・行き先変更・体力)を、上限つきで消化する。
    void ProcessImmediateTestPlans(GameContext& context);
    // 需要判定のたびに、待機中の計画の前提(家にいる・外出中)・期限を確かめる。
    void MaintainTestPlansAtTick(GameContext& context);
    // 外出が終わった(家に着いた・住居なし・立ち往生)時に、実行中の計画を、成功か、拒否(強制が残った)で終える。
    void FinalizeTestOnOutingEnd(ResidentAgent& agent, GameContext& context);

    // 施設の利用が終わって、帰り始める前に、今の道路で、施設から自宅への経路を作り直す(道路が切り取られて、行きの経路が使えなくなっていることがあるため)。
    // 経路が見つかれば、その経路で帰る(元より長ければ、迂回なので、体力を消費し、満足度が下がる)。見つからなければ、立ち往生にして、falseを返す。
    // applyDetourPenalty: 元の経路(今の保存してある経路)より長ければ、迂回の体力の消費・満足度の低下を与えるか。
    // 立ち寄りの途中では、保存してある経路が「前の施設→今の施設」の短い経路なので、falseにする(家までの道のりと比べられない)。
    bool ReplanReturnRoute(ResidentAgent& agent, GameContext& context, bool applyDetourPenalty = true);

    // 主目的の施設を使い終わった住民が、低い確率で、近くの施設に立ち寄る(P1)。立ち寄るなら、その施設へ歩き始めて、trueを返す。
    // 範囲(今いる施設から)・体力(立ち寄り先 + 家までの帰り)・時刻・食事系の連続は、設定とResidentLogicの関数で判定する。currentFacilityは、今いる施設。
    bool TryStartStopover(ResidentAgent& agent, GameContext& context, const GameObject& currentFacility);

    // 外出の途中(立ち寄りへ向かう途中など)で、行き先が無くなった時に、今の位置から家へ帰り始める(道が無ければ立ち往生)。位置は飛ばさない。
    // strandedPenalty: 道が無くて立ち往生になる時に、満足度を下げるか(新しい住宅への入居では、下げない)。
    void StartReturnFromHere(ResidentAgent& agent, GameContext& context, bool strandedPenalty = true);

    // 立ち往生している住民が、道がつながって帰れるようになったかを調べ、帰れるなら、帰り始めさせる(RunDemandTickが、毎回呼ぶ)。
    // 今の位置の近くの道路から、自宅(家が無ければ、元の家の位置)への経路を探す。見つかれば、その経路で歩いて帰る(帰り着けば、家にいる・住居なしになる)。
    // 帰り始めたらtrue。
    bool TryRecoverStranded(ResidentAgent& agent, GameContext& context, const std::vector<RoadSegment>& segments, const RoadGraph& graph, PathCache& cache);

    // 住民を、立ち往生にする(道路の上に立たせ、動かない・施設を使わない状態にして、満足度を下げる)。
    // 今の位置が、道路の上でない(切り取られた跡・施設の中など)時は、近くの、残った道路の上に寄せる(遠くへは、ワープさせない)。
    // reason: 立ち往生した理由(デバッグの記録用。動作には関わらない)。
    // applyPenalty: 満足度を下げるか(道路が無くなった時の罰。入居先の道が無い時は、住民のせいではないので、下げない)。
    void BecomeStranded(ResidentAgent& agent, GameContext& context, const std::vector<RoadSegment>& segments, SimReason reason = SimReason::NoRouteHome, bool applyPenalty = true);

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

    // 入居者を選ぶ待ちの住宅(新しい住民を、まだ生まれさせない)。保存しない。
    std::vector<uint32_t> m_reservedHouses;

    // 行動テストの計画(待機中・実行中・終わった物)。保存しない。
    std::vector<SimTestPlan> m_testPlans;
    uint32_t m_nextTestId = 1;
    uint32_t m_nextTestBatchId = 1;
    static constexpr size_t kMaxTestPlans = 600;

    // 判断・状態遷移・異常の記録(Editorの「住民デバッグ」用。既定は無効。保存しない)。
    SimDebugRecorder m_debugRecorder;
    SimStats m_stats;                       // 統計。m_debugRecorder の観察者として、記録と同時に数える
    SimRunDigest m_digest;                  // 実行結果の指紋。同じく観察者
    SimRandomMode m_randomMode = SimRandomMode::Reseed;
    uint32_t m_activeSeed = 0;              // 今の世界の乱数の種
    std::function<void(const SimTestCommand&, uint32_t)> m_onTestSubmitted;
    uint64_t m_tickIndex = 0;               // RunDemandTick を呼んだ回数(記録用。保存しない)

    // 最後に時計から読んだ、通算の週(デバッグ表示用)。
    int m_lastWeekCount = 1;
    float m_lastClockHour = 8.0f;   // 最後に時計から読んだ時刻(行動テストの計画の記録用)

    // Profilerのメモリ内訳表示用。直近のRunDemandTickで作った経路探索グラフの規模を覚えておく
    // (グラフ自体はRunDemandTick内のローカル変数のため、規模だけをここに残す)。
    int m_lastRoadGraphNodeCount = 0;
    int m_lastRoadGraphEdgeCount = 0;
};
