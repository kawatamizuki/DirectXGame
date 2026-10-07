#include "Game.h"
#include "Debug.h"
#include "Profiler.h"
#include "InputAction.h"
#ifdef ENABLE_EDITOR
#include "imgui.h"
#endif


Game::Game()
    : m_hwnd(nullptr)
{
}
Game::~Game()
{
   /* Finalize();*/
}

bool Game::Initialize(HWND hwnd)
{

    if (!m_renderer.Initialize(hwnd))
    {
        Debug::Error("Game::Initialize failed : Renderer initialize failed");
        return false;
    }

    m_hwnd = hwnd;
    m_inputManager.SetWindowHandle(hwnd);

    m_renderer.SetVSyncEnabled(false);
    m_timeManager.SetTargetFPS(60);
    m_context.renderer = &m_renderer;
    m_context.debugRenderer = &m_debugRenderer;
    m_context.camera = &m_camera;
    m_context.input = &m_inputManager;
    m_context.time = &m_timeManager;
    m_context.clock = &m_gameClock;
    m_context.objects = &m_objects;
    m_context.demandSystem = &m_demandSystem;

    // DebugRendererはフィールドの格子線・建物/道路のプレビュー描画にも使うため、
    // ENABLE_EDITORの外(Releaseビルドでも)で初期化する。
    if (!m_debugRenderer.Initialize(m_renderer.GetDevice(), m_renderer.GetContext()))
    {
        Debug::Error("Game::Initialize failed : DebugRenderer initialize failed");
        return false;
    }

    // ゲームプレイ用UI(独自の2D描画。ImGuiとは独立)。初期化に失敗してもゲーム自体は続けられるので、
    // UIだけを無効にして続行する(原因はログに出る)。
    if (m_uiRenderer.Initialize(m_renderer.GetDevice(), m_renderer.GetContext()))
    {
        m_gameUI.Initialize(&m_uiRenderer, &m_dayNightCycle);
        m_uiAvailable = true;
    }
    else
    {
        Debug::Error("Game::Initialize : UIRenderer initialize failed (ゲームプレイ用UIは表示されません)");
    }

#ifdef ENABLE_EDITOR

    if (!m_debugEditor.Initialize(hwnd, &m_context))
    {
        Debug::Error("Game::Initialize failed : DebugEditor initialize failed");
        return false;
    }

#endif



    m_sceneManager.Init(&m_context);
    //透視投影
    m_camera.SetPosition(0.0f, 0.0f, -10.0f);
    m_camera.SetTarget(0.0f, 0.0f, 0.0f);
    m_camera.SetProjection(
        //視野角
        DirectX::XMConvertToRadians(45.0f),
        static_cast<float>(m_renderer.GetWindowWidth()) /
        static_cast<float>(m_renderer.GetWindowHeight()),
        0.1f,
        100.0f
    );


    // 初期実装時の動作確認用に置いていた巨大モデル(character-female-f)とその下のキューブ。
    // 本来のゲーム内容には不要なので非表示にしておく(ロード処理ごとコメントアウト)。
    //if (!m_cubeModel.LoadFromObj(m_renderer.GetDevice(), "Models/cube.obj"))
    //{
    //    Debug::Error("Game::Initialize failed : Model load failed");
    //    return false;
    //}

    //if (!m_kennyModel.LoadFromObj(m_renderer.GetDevice(), "Models/character-female-f.obj"))
    //{
    //    Debug::Error("Game::Initialize failed : KennyModel load failed");
    //    return false;
    //}

    //m_cubeObject.model = &m_cubeModel;
    //m_cubeObject.transform.position = { 0.0f, -1.0f, 0.0f };
    //m_cubeObject.transform.scale = { 0.5f, 0.5f, 0.5f };
    //m_cubeObject.kind = ObjectKind::Environment;
    //m_objects.push_back(m_cubeObject);

    //m_kennyObject.model = &m_kennyModel;
    //m_kennyObject.transform.position = { 0.0f, 0.0f, 0.0f };
    //m_kennyObject.transform.scale = { 5.0f, 5.0f, 5.0f };
    //m_kennyObject.kind = ObjectKind::Environment;
    //m_objects.push_back(m_kennyObject);

    // 建物種別ごとのモデルをBuildingDefinition::modelPathから読み込む
    // (パスの二重管理を避けるため、パス文字列自体はBuildingType.cppの定義を唯一の情報源にする)。
    if (!m_houseModel.LoadFromObj(m_renderer.GetDevice(), GetBuildingDefinition(BuildingType::House).modelPath))
    {
        Debug::Error("Game::Initialize failed : House model load failed");
        return false;
    }
    if (!m_officeModel.LoadFromObj(m_renderer.GetDevice(), GetBuildingDefinition(BuildingType::Office).modelPath))
    {
        Debug::Error("Game::Initialize failed : Office model load failed");
        return false;
    }
    if (!m_shopModel.LoadFromObj(m_renderer.GetDevice(), GetBuildingDefinition(BuildingType::Shop).modelPath))
    {
        Debug::Error("Game::Initialize failed : Shop model load failed");
        return false;
    }

    m_field.Initialize(20, 20, 1.0f);

    // 地面モデル(任意)。用意されていれば読み込み、Fieldの実サイズにぴったり合うよう
    // モデルの実測サイズ(GetBoundsMin/Max)からスケールを自動計算して配置する。
    // モデルのローカル原点がバウンディングボックスの中心からズレていても
    // (中心を原点に置いていない、隅を原点にしているモデルなど)、XZの中心をField中心に、
    // 天面(Y最大値)をField/建物/道路が前提とするy=0の地面基準に自動で揃える。
    // モデルがどんな単位・大きさ・原点位置で作られていてもグリッドとズレない。
    // 無ければ失敗として扱わずスキップする(パスを用意して再起動すれば自動的に反映される)。
    if (m_groundModel.LoadFromObj(m_renderer.GetDevice(), "RoadTiles/Models/roadTile_163.obj"))
    {
        GameObject groundObject;
        groundObject.model = &m_groundModel;
        groundObject.kind = ObjectKind::Environment;

        DirectX::XMFLOAT3 boundsMin = m_groundModel.GetBoundsMin();
        DirectX::XMFLOAT3 boundsMax = m_groundModel.GetBoundsMax();
        float modelWidth = boundsMax.x - boundsMin.x;
        float modelDepth = boundsMax.z - boundsMin.z;

        if (modelWidth > 0.0001f && modelDepth > 0.0001f)
        {
            float scaleX = m_field.GetWorldWidth() / modelWidth;
            float scaleZ = m_field.GetWorldDepth() / modelDepth;
            float centerX = (boundsMin.x + boundsMax.x) * 0.5f;
            float centerZ = (boundsMin.z + boundsMax.z) * 0.5f;

            groundObject.transform.scale = { scaleX, 1.0f, scaleZ };
            groundObject.transform.position =
            {
                -centerX * scaleX,
                -boundsMax.y,
                -centerZ * scaleZ
            };
        }

        m_objects.push_back(groundObject);
    }
    else
    {
        Debug::Info("Game::Initialize : 地面モデルが見つからないためスキップしました。用意でき次第、再起動すると自動的に配置されます。");
    }

    m_buildController.Initialize(
        &m_context, &m_field, &m_occupancyGrid,
        &m_roadSystem, &m_orientationRegistry,
        &m_houseModel, &m_officeModel, &m_shopModel);

    m_roadSystem.Initialize(
        &m_context, &m_field, &m_occupancyGrid,
        &m_roadPlacementStrategy, &m_curveRoadPlacementStrategy, &m_roadMeshGenerator,
        &m_orientationRegistry);

    // 代表住民の見た目は、既に読み込んでいるKennyキャラクターモデルを仮で流用する
    if (!m_agentModel.LoadFromObj(m_renderer.GetDevice(), "Models/character-female-f.obj"))
    {
        Debug::Error("Game::Initialize failed : Agent model load failed");
        return false;
    }

    m_demandSystem.Initialize(&m_pathfinder, &m_agentModel);

    return true;
}

void Game::OnResize(UINT width, UINT height)
{
    m_renderer.Resize(width, height);

    m_camera.SetProjection(
        DirectX::XMConvertToRadians(45.0f),
        static_cast<float>(width) / static_cast<float>(height),
        0.1f,
        100.0f
    );
}

void Game::RunFrame()
{
    m_timeManager.BeginFrame();
    Update();
    Draw();

}

void Game::Update()
{
    Profiler::BeginFrame();

    // 毎フレームのクリック消費状況をリセットしてから呼び出しを行う。
    // Editorのギズモ/選択と、プレイヤーの配置操作が同じクリックを二重処理するのを防ぐ
    m_context.inputConsumed = false;
    m_context.mouseOverUI = false;
    m_context.hudTopInset = 0.0f;

    m_inputManager.Update();
    m_sceneManager.Update();
    m_timeManager.Update();

    // ゲーム内時刻を進める(Editing/Playingに関わらず進める。Editorで時刻を動かして見た目を確認できる)。
    m_gameClock.Update(m_timeManager.GetDeltaTime());

    for (auto& obj : m_objects)
    {
        //obj.transform.rotation.y += 0.01f;
    }
#ifdef ENABLE_EDITOR
    m_debugEditor.BeginFrame();
#endif

    // ゲームプレイ用UI(上部HUD)の入力は、ワールドへの操作(オブジェクト選択・建物/道路の配置)より先に
    // 処理する。HUDの上のクリックがワールドへ抜けないよう、mouseOverUI/inputConsumedを先に確定させる。
    // (ImGuiのBeginFrameの後なのは、ImGuiのパネルの上にいるかを今フレームの状態で見るため)
    UpdateGameUI();

#ifdef ENABLE_EDITOR
    m_debugEditor.Update();
#endif

    // Playingモードの時だけ建物/道路配置を更新する。
    // 呼び出し順が優先度チェーンになっており、片方がクリックを消費したら
    // もう片方は同じフレームでは動かない。
    if (m_context.mode == GameMode::Playing)
    {
        if (!m_context.inputConsumed)
        {
            m_buildController.Update();
        }
        if (!m_context.inputConsumed)
        {
            m_roadSystem.Update();
        }

        // 住民の移動・需要更新は、ゲーム内時間の倍率(停止/倍速)に従って進める。
        m_demandSystem.Update(m_gameClock.GetScaledDeltaSeconds(), m_context, m_roadSystem.GetSegments());
    }
}

bool Game::IsGameUIVisible() const
{
    // ゲームプレイ用UIは、UIの初期化に成功していて、ゲームシーンの間だけ出す(タイトル画面には出さない)。
    return m_uiAvailable && m_sceneManager.GetCurrentSceneName() == SceneName::Game;
}

HudState Game::BuildHudState() const
{
    HudState state;
    state.dayCount = m_gameClock.GetDayCount();
    state.timeOfDayHours = m_gameClock.GetTimeOfDayHours();
    state.timeScale = m_gameClock.GetTimeScale();
    state.stats = m_demandSystem.GetStats();
    return state;
}

void Game::UpdateGameUI()
{
    if (!IsGameUIVisible())
    {
        return;
    }

    HudInput input;
    input.screenW = static_cast<float>(m_renderer.GetWindowWidth());
    input.screenH = static_cast<float>(m_renderer.GetWindowHeight());

    POINT mouse = m_inputManager.GetMousePosition();
    input.mouseX = static_cast<float>(mouse.x);
    input.mouseY = static_cast<float>(mouse.y);

    input.pressed = m_inputManager.IsActionPressed(InputAction::Decide);
    input.down = m_inputManager.IsActionDown(InputAction::Decide);
    input.released = m_inputManager.IsActionReleased(InputAction::Decide);

#ifdef ENABLE_EDITOR
    // デバッグGUI(ImGui)のパネルの上にカーソルがある時は、HUDは反応しない(ImGuiが手前に描かれるため)。
    input.blockedByOtherUI = ImGui::GetIO().WantCaptureMouse;
#endif

    HudResult result = m_gameUI.Update(input, BuildHudState());

    m_context.mouseOverUI = result.mouseOverUI;
    m_context.hudTopInset = m_gameUI.GetLayout().bar.Bottom();
    if (result.consumedClick)
    {
        m_context.inputConsumed = true;
    }
    if (result.requestedTimeScale.has_value())
    {
        m_gameClock.SetTimeScale(*result.requestedTimeScale);
    }
}

void Game::Draw()
{
    // ゲーム内時刻から今の光の状態(太陽/月の向き・色・空の色)を求めて、描画に渡す。
    m_renderer.SetLighting(m_dayNightCycle.Evaluate(m_gameClock.GetTimeOfDayHours()));

    m_renderer.BeginFrame();

    m_sceneManager.Draw();

    {
        PROFILE_SCOPE("Game::Draw ObjectLoop");
        for (auto& obj : m_objects)
        {
            if (obj.model && obj.visible)
            {
                m_renderer.DrawModel(*obj.model, obj.transform, m_camera);
            }
        }
    }

    m_field.DrawGridOverlay(m_debugRenderer, m_orientationRegistry);
    m_demandSystem.Draw(m_debugRenderer);

    if (m_context.mode == GameMode::Playing)
    {
        m_buildController.Draw();
        m_roadSystem.Draw();
    }

#ifdef ENABLE_EDITOR
    m_debugEditor.Draw();
#endif

    // 人流・需要シミュレーションの統計・プロファイラ・ログはモードによらず常時表示する
    m_demandSystem.DrawDebugUI();
    m_gameClock.DrawDebugUI();
    Profiler::DrawDebugUI(m_objects, m_roadSystem, m_occupancyGrid, m_demandSystem);
    Debug::DrawDebugUI();

    // DebugRendererはフィールドの格子線・建物/道路の見た目にも使うため、
    // ENABLE_EDITORの外(Releaseビルドでも)でFlushする。
    m_debugRenderer.Flush(m_camera);

    // ゲームプレイ用UIは、3Dとデバッグ線の手前、ImGuiのデバッグパネルの奥に描く
    // (ImGuiはm_debugEditor.EndFrameで最前面に描かれる)。
    if (IsGameUIVisible())
    {
        m_uiRenderer.BeginFrame(
            static_cast<float>(m_renderer.GetWindowWidth()),
            static_cast<float>(m_renderer.GetWindowHeight()));
        m_gameUI.Draw(BuildHudState());
        m_uiRenderer.EndFrame();
    }

#ifdef ENABLE_EDITOR
    m_debugEditor.EndFrame();
#endif

    if (!m_renderer.IsVSyncEnabled())
    {
        m_timeManager.WaitForTargetFPS();
    }

    m_renderer.EndFrame();





}

void Game::UpdateWindowTitle()
{

    //========================================
    // FPS表示
    //========================================
    std::wstring title =
        L"FPS : " +
        std::to_wstring(
            static_cast<int>(m_timeManager.GetFPS())
        );

    title += m_renderer.IsVSyncEnabled()
        ? L" | VSync ON"
        : L" | VSync OFF";

    SetWindowText(m_hwnd, title.c_str());
}

void Game::Finalize()
{
#ifdef ENABLE_EDITOR
    m_debugEditor.Finalize();
#endif
    m_sceneManager.Finalize();
    m_renderer.Finalize();
}
