#pragma once
#include <vector>
#include <string>
#include <DirectXMath.h>
#include "ResidentAgent.h"
#include "ResidentSchedule.h"

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

// City Statsパネルに表示する集計値。
struct DemandStats
{
    int houseCount = 0;       // 配置されているHouseの数
    int officeCount = 0;      // 配置されているOfficeの数
    int populationTotal = 0;  // House.populationRepresentedの合計(表示上の人口)
    int agentCount = 0;       // 実際にシミュレートしている代表住民の数
    int jobCapacityTotal = 0; // Officeの受け入れ可能数の合計
    int jobsFilled = 0;       // 職に就いている代表住民の数
    int unmetDemand = 0;      // 職を見つけられなかった代表住民の数
};

// Profilerのメモリ内訳表示(経路探索・人流)用の集計値。
struct DemandMemoryStats
{
    int agentCount = 0;            // 代表住民の数
    size_t totalWaypoints = 0;     // 全代表住民の移動経路(waypoints)の合計点数
    int roadGraphNodeCount = 0;    // 直近の経路探索グラフのノード数
    int roadGraphEdgeCount = 0;    // 直近の経路探索グラフのエッジ数
};

// 需要の発生(House)・割り当て(Office)・代表住民の生成/移動を管理するクラス。
// Game::Update/Drawから毎フレーム呼ばれる(BuildController/RoadSystemと同じ所有のされ方)。
class DemandSystem
{
public:
    // 使用する経路探索アルゴリズムと、代表住民の見た目に使うモデルを渡す
    // (経路探索アルゴリズムは差し替え可能にするため、具体的なアルゴリズムはここでは知らない)。
    void Initialize(IPathfinder* pathfinder, Model* agentModel);

    // deltaTime: 前フレームからの経過秒数(ゲーム内時間の倍率をかけた後の値。GameClock::GetScaledDeltaSecondsを渡す。
    // 停止中は0、倍速中は大きくなり、住民の移動と需要更新の間隔の両方がそれに従う)。
    // segments: 現在配置されている道路区間(読み取り+debugLoad書き込み)。
    // 住民の出勤・帰宅は、context.clockの時刻とm_scheduleの予定表で決まる。
    void Update(float deltaTime, GameContext& context, std::vector<RoadSegment>& segments);

    void DrawDebugUI() const;

    // 直近のRunDemandTickで集計した街の指標(人口・職の埋まり具合など)のコピーを返す。
    // ゲームプレイ用UI(上部HUD)の表示に使う。
    DemandStats GetStats() const { return m_lastStats; }

    // 建物-道路の接続線(入口/私道)を毎フレーム描画する。RunDemandTickで計算した結果を使う。
    void Draw(DebugRenderer& debugRenderer) const;

    // Inspector表示用: idで指定した代表住民の状態を1行の文字列で返す(見つからなければ空文字)。
    std::string GetAgentDebugSummary(uint32_t objectId) const;

    // Profilerのメモリ内訳表示用の集計値を返す。
    DemandMemoryStats GetMemoryStats() const;

private:
    // 毎フレーム呼ぶ: 予定表に従って出勤・退勤を始めさせ、通勤中のエージェントをなめらかに移動させる。
    void UpdateAgentMovement(float deltaTime, GameContext& context);

    // agentの通勤(出勤/帰宅)を始める。経路を進行方向に合わせて並べ替え、自宅/職場の出発地点に置く。
    void StartCommute(ResidentAgent& agent, bool toOffice, GameObject* obj) const;

    // 数秒おきに呼ぶ: 新しい代表住民の生成、職探し、経路計算、統計更新を行う(重い処理はここだけ)。
    void RunDemandTick(GameContext& context, std::vector<RoadSegment>& segments);

    IPathfinder* m_pathfinder = nullptr;
    Model* m_agentModel = nullptr;
    std::vector<ResidentAgent> m_agents;

    // 住民の1日の予定表(出勤・退勤の時刻)。ゲーム内容が固まったらこのデータだけを調整する。
    ResidentSchedule m_schedule;

    float m_tickTimer = 0.0f;
    static constexpr float kTickInterval = 3.0f; // 3秒ごとに需要の再計算を行う

    DemandStats m_lastStats;

    // 直近のRunDemandTickで見つかった、道路に接続している建物の接続線一覧(Draw()で使う)。
    std::vector<RoadConnectionLine> m_connectionLines;

    // Profilerのメモリ内訳表示用。直近のRunDemandTickで作った経路探索グラフの規模を覚えておく
    // (グラフ自体はRunDemandTick内のローカル変数のため、規模だけをここに残す)。
    int m_lastRoadGraphNodeCount = 0;
    int m_lastRoadGraphEdgeCount = 0;
};
