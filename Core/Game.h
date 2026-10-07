#pragma once
#include <windows.h>
#include "Renderer.h"
#include"InputManager.h"
#include"SceneManager.h"
#include "Model.h"
#include "DebugEditor.h"
#include"DebugRenderer.h"
#include "GameContext.h"
#include "TimeManager.h"
#include "GameClock.h"
#include "DayNightCycle.h"
#include "GameObject.h"
#include"Camera.h"
#include "Field.h"
#include "CellOccupancyGrid.h"
#include "GridOrientationRegistry.h"
#include "BuildController.h"
#include "StraightRoadPlacementStrategy.h"
#include "CurveRoadPlacementStrategy.h"
#include "QuadRoadMeshGenerator.h"
#include "RoadSystem.h"
#include "DijkstraPathfinder.h"
#include "DemandSystem.h"

class Game
{
public:
    Game();
    ~Game();

    bool Initialize(HWND hwnd);
    void OnResize(UINT width, UINT height);
    void RunFrame();
    void Update();
    void Draw();
    void UpdateWindowTitle();
    void Finalize();

private:
    HWND m_hwnd;
    DebugEditor m_debugEditor;//imgui用
    DebugRenderer m_debugRenderer;//ワイヤーフレームなどデバッグ用の描画
    Renderer m_renderer;
    InputManager m_inputManager;
    SceneManager m_sceneManager;
    TimeManager m_timeManager;

    // ゲーム内時刻(実時間のTimeManagerとは別)と、時刻から光の状態を求める昼夜サイクル。
    GameClock m_gameClock;
    DayNightCycle m_dayNightCycle;

    Camera m_camera;

    GameContext m_context;

    Model m_cubeModel;
    Model m_kennyModel;
    GameObject m_cubeObject;
    GameObject m_kennyObject;
    std::vector<GameObject> m_objects;

    // フィールド・建物配置・道路配置
    Field m_field;
    CellOccupancyGrid m_occupancyGrid;
    // 建物⇔道路の向き合わせ用、チャンク単位の基準角度台帳(早い者勝ちで固定)。
    GridOrientationRegistry m_orientationRegistry;

    // 地面の見た目(任意)。指定パスのモデルが無ければ読み込みをスキップする
    // (無くてもゲームは問題なく起動する)。
    // 実サイズ・原点位置によらずField::GetWorldWidth/Depthとy=0の地面基準に自動で揃える。
    Model m_groundModel;

    // 建物種別ごとのモデル(BuildingDefinition::modelPathから読み込む)。
    Model m_houseModel;
    Model m_officeModel;
    Model m_shopModel;
    BuildController m_buildController;
    StraightRoadPlacementStrategy m_roadPlacementStrategy;
    CurveRoadPlacementStrategy m_curveRoadPlacementStrategy;
    QuadRoadMeshGenerator m_roadMeshGenerator;
    RoadSystem m_roadSystem;

    // 人流・需要シミュレーション
    Model m_agentModel;
    DijkstraPathfinder m_pathfinder;
    DemandSystem m_demandSystem;
};
