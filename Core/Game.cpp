#include "Game.h"
#include "Debug.h"
#include "Profiler.h"
#include "InputAction.h"
#include "BakedModelCache.h"
#include "AssetImporter.h"
#include "WorldScale.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include "imgui.h"
#include "imgui_internal.h"


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
    m_context.flags = &m_gameFlags;
    m_context.economy = &m_gameEconomy;
    m_context.scenario = &m_scenarioManager;
    m_modelCache.Initialize(&m_renderer);
    m_context.modelCache = &m_modelCache;

    // DebugRendererはフィールドの格子線・建物/道路のプレビュー描画にも使うため、
    // ENABLE_EDITORの外(Releaseビルドでも)で初期化する。
    if (!m_debugRenderer.Initialize(m_renderer.GetDevice(), m_renderer.GetContext()))
    {
        Debug::Error("Game::Initialize failed : DebugRenderer initialize failed");
        return false;
    }

    // 素材の一覧パネル(F2で開閉)。開くまでは何も処理しないので、ここでは参照を渡すだけ。
    m_assetBrowser.Initialize(&m_renderer, &m_dayNightCycle);
    m_assetViewer.Initialize(&m_renderer, &m_dayNightCycle);
    m_assetBrowser.SetOpenViewerHandler([this](const std::vector<AssetEntry>& models, const std::string& title)
    {
        m_assetViewer.Open(models, title);
    });

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

    // Editorメニュー: パネルを登録し、各システムの開閉・スイッチをつなぐ(設定ファイルを読むのは、全部登録した後)。
    m_editorUi.Initialize();
    m_debugEditor.AttachEditorUi(&m_editorUi);
    m_assetBrowser.SetOpenFlag(m_editorUi.PanelFlag("Assets"));
    m_assetViewer.SetOpenFlag(m_editorUi.PanelFlag("Asset Viewer"));

#endif



    m_sceneManager.Init(&m_context);

    //透視投影
    m_camera.SetPosition(0.0f, 0.0f, -10.0f);
    m_camera.SetTarget(0.0f, 0.0f, 0.0f);
    m_camera.SetProjection(
        //視野角
        DirectX::XMConvertToRadians(45.0f),
        static_cast<float>(m_renderer.GetViewWidth()) /
        static_cast<float>(m_renderer.GetViewHeight()),
        WorldScale::kCameraNearPlane,
        WorldScale::kCameraFarPlane
    );


    // 初期実装時の動作確認用に置いていた巨大モデル(character-female-f)とその下のキューブ。
    // 本来のゲーム内容には不要なので非表示にしておく(ロード処理ごとコメントアウト)。
    //if (!m_cubeModel.LoadFromFile(m_renderer.GetDevice(), "Models/cube.obj"))
    //{
    //    Debug::Error("Game::Initialize failed : Model load failed");
    //    return false;
    //}

    //if (!m_kennyModel.LoadFromFile(m_renderer.GetDevice(), "Models/character-female-f.obj"))
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
    if (!m_houseModel.LoadFromFile(m_renderer.GetDevice(), GetBuildingDefinition(BuildingType::House).modelPath))
    {
        Debug::Error("Game::Initialize failed : House model load failed");
        return false;
    }
    if (!m_officeModel.LoadFromFile(m_renderer.GetDevice(), GetBuildingDefinition(BuildingType::Office).modelPath))
    {
        Debug::Error("Game::Initialize failed : Office model load failed");
        return false;
    }
    if (!m_shopModel.LoadFromFile(m_renderer.GetDevice(), GetBuildingDefinition(BuildingType::Shop).modelPath))
    {
        Debug::Error("Game::Initialize failed : Shop model load failed");
        return false;
    }

    m_field.Initialize(WorldScale::kFieldWidthCells, WorldScale::kFieldDepthCells, WorldScale::kCellSize);
    m_propPlacer.Initialize(&m_context, &m_field);

    // 街づくりのカメラの距離。最初のズーム・一番寄る距離は絶対の値(マスを大きくすると、建物が大きく映る)、
    // 一番引ける距離だけフィールドの幅に合わせる(一番引けば、全体が見える)。
    // 平行移動の速さ・ズームの1目盛りの比率は、距離に比例するので、そのままでよい。
    {
        CityCameraSettings camera = m_cityCamera.GetSettings();
        camera.defaultDistance = WorldScale::kCameraDefaultDistance;
        camera.minDistance = WorldScale::kCameraMinDistance;
        camera.maxDistance = WorldScale::kCameraMaxDistanceFieldRatio * m_field.GetWorldWidth();
        camera.boundsMargin = 2.0f * m_field.GetCellSize(); // 最初は2.0(マス1.0の時の2マス分)
        m_cityCamera.SetSettings(camera);
    }

    // Assetsパネルの「シーンの中央に置く」と、フォルダ操作の「使用中」の判定を、ゲーム側のつなぎで渡す。
    m_assetBrowser.SetPlaceAtCenterHandler([this](const AssetEntry& entry) { PlaceAssetAtViewCenter(entry.path); });
    m_assetBrowser.SetFolderLockCheck([this](const std::string& folder) { return IsFolderUsedByProps(folder); });

    // 地面モデル(任意)。用意されていれば読み込み、Fieldの実サイズにぴったり合うよう
    // モデルの実測サイズ(GetBoundsMin/Max)からスケールを自動計算して配置する。
    // モデルのローカル原点がバウンディングボックスの中心からズレていても
    // (中心を原点に置いていない、隅を原点にしているモデルなど)、XZの中心をField中心に、
    // 天面(Y最大値)をField/建物/道路が前提とするy=0の地面基準に自動で揃える。
    // モデルがどんな単位・大きさ・原点位置で作られていてもグリッドとズレない。
    // 無ければ失敗として扱わずスキップする(パスを用意して再起動すれば自動的に反映される)。
    if (m_groundModel.LoadFromFile(m_renderer.GetDevice(), "RoadTiles/Models/roadTile_163.obj"))
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
    if (!m_agentModel.LoadFromFile(m_renderer.GetDevice(), "Models/character-female-f.obj"))
    {
        Debug::Error("Game::Initialize failed : Agent model load failed");
        return false;
    }

    m_demandSystem.Initialize(&m_pathfinder, &m_agentModel);

    // セーブ/ロード: 街を構成するシステムを渡し、状態を持つシステム(時計・フラグ・所持金・住民)を登録する。
    // 新しい状態(お金・建物レベル・チュートリアルの進行など)を足す時は、その要素がISaveableを
    // 実装して、ここにRegisterSaveableを1行足すだけでセーブ対象になる。
    m_scenarioManager.Initialize(
        &m_context, &m_field, &m_roadSystem, &m_buildController, &m_occupancyGrid, &m_orientationRegistry);
    m_scenarioManager.RegisterSaveable(&m_gameClock);
    m_scenarioManager.RegisterSaveable(&m_gameFlags);
    m_scenarioManager.RegisterSaveable(&m_gameEconomy);
    m_scenarioManager.RegisterSaveable(&m_demandSystem);
#ifdef ENABLE_EDITOR
    // 読み込みで建物などのオブジェクトが入れ替わるので、エディタの選択(添字)を解除する。
    m_scenarioManager.SetOnWorldReplaced([this]() { m_debugEditor.ClearSelection(); });

    // 「Editor > エディタ機能のON/OFF」に出すスイッチ。全て登録し終えてから、保存してある設定を読む。
    m_buildController.RegisterEditorToggles(m_editorUi.Settings());
    m_editorUi.Settings().RegisterToggle("grid_overlay", "床の格子線を表示", &m_showGridOverlay,
        "地面のマス目の線を描く(青マスの線も含む)");
    m_editorUi.Settings().RegisterToggle("model_cache", "モデルキャッシュを使う", &m_useModelCache,
        "OFFにすると、これから読むモデルは毎回ローダーで読み直す(保存済みのキャッシュは残る)。起動時の読み込みは、常にキャッシュを使う");
    m_editorUi.LoadSettings();
    BakedModelCache::SetEnabled(m_useModelCache);
#endif
    return true;
}

void Game::OnResize(UINT width, UINT height)
{
    m_renderer.Resize(width, height);

    m_camera.SetProjection(
        DirectX::XMConvertToRadians(45.0f),
        static_cast<float>(width) / static_cast<float>(height),
        WorldScale::kCameraNearPlane,
        WorldScale::kCameraFarPlane
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
    m_context.assetDrag = AssetDragState(); // ドラッグの状態も、毎フレーム、ImGuiから読み直す(UpdateAssetDragState)
    m_context.costPreview = CostPreview(); // 建設費のタグは、今フレームに建設ツールが書いた時だけ出す

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

    // メニューバーとドックスペース(パネルを吸着させる土台)。ImGuiのパネルを描くより先に出す
    // (ドックに吸着したパネルは、土台が先に出ていないと1フレーム隠れるため)。
    // ここで、ゲームビュー(中央の枠)の位置と大きさが、今フレームの値で決まる。
    m_editorUi.BeginFrame(BuildEditorFrameInfo());
    ApplyGameViewRect();
#endif

    // ゲームプレイ用UI(上部HUD)の入力は、ワールドへの操作(オブジェクト選択・建物/道路の配置)より先に
    // 処理する。HUDの上のクリックがワールドへ抜けないよう、mouseOverUI/inputConsumedを先に確定させる。
    // (ImGuiのBeginFrameの後なのは、ImGuiのパネルの上にいるかを今フレームの状態で見るため)
    UpdateGameUI();

#ifdef ENABLE_EDITOR
    // Assetsからモデルをドラッグしている状態を読み取り、ゲームの画面の上で離されたら飾りを置く。
    UpdateAssetDragState();
#endif
    if (IsInGameScene())
    {
        m_propPlacer.Update();
    }

    HandleScenarioHotkeys();

#ifdef ENABLE_EDITOR
    // F2: 素材の一覧パネルの開閉(入力欄に文字を入力している間は、ショートカットとして扱わない)。
    if (!ImGui::GetIO().WantTextInput && m_inputManager.IsKeyPressed(VK_F2))
    {
        m_assetBrowser.Toggle();
    }
#endif
    // サムネイルの生成の進行と、選択中のモデルの詳細表示。パネルが閉じている間は何もしない。
    BakedModelCache::SetEnabled(m_useModelCache); // Editorメニューで切り替えられる
    ProcessPendingDrops();                         // エクスプローラーからドロップされた物の取り込み
    m_assetBrowser.Update();
    m_assetViewer.Update();

    // 街づくり(Playing)のカメラ。フリーカメラ(DebugEditor::Update の中)は、Editingの間だけ動く。
    UpdateCityCamera();

    // タイトル画面(背景の街・カメラ・スタート/終了のメニュー)。ゲームシーンの間は何もしない。
    UpdateTitle();

#ifdef ENABLE_EDITOR
    m_debugEditor.Update();
    m_editorUi.SaveSettingsIfChanged();
#endif

    // Playingモードの時だけ建物/道路配置を更新する。
    // 呼び出し順が優先度チェーンになっており、片方がクリックを消費したら
    // もう片方は同じフレームでは動かない。
    // 街(建物・道路・住民)の更新は、ゲームシーンの間だけ。タイトル画面でクリックしても、街の操作(建物の配置など)が
    // 同時に起きないようにする。
    if (IsInGameScene() && m_context.mode == GameMode::Playing)
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

bool Game::IsInTitleScene() const
{
    return m_sceneManager.GetCurrentSceneName() == SceneName::Title;
}

void Game::UpdateTitle()
{
    const bool inTitle = IsInTitleScene();
    if (inTitle && !m_wasInTitleScene)
    {
        // タイトル画面が始まった: 背景に、初期マップの街を出す(無ければ、空のフィールドのまま)。
        // 街の更新(住民・需要)は、ゲームシーンの間だけなので、背景は静止した街になる(カメラと昼夜の光だけが動く)。
        m_titleTime = 0.0f;
        if (m_scenarioManager.LoadStartScenario())
        {
            Debug::Info("Game: タイトル画面の背景に、初期マップを読み込みました");
        }

        // 街の広がり(建物・道路の端が入る四角の中心と、そこから一番遠い点までの距離)を求めて、カメラが回る中心にする。
        bool any = false;
        float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f;
        auto include = [&](float x, float z)
        {
            if (!any) { minX = maxX = x; minZ = maxZ = z; any = true; return; }
            minX = (std::min)(minX, x); maxX = (std::max)(maxX, x);
            minZ = (std::min)(minZ, z); maxZ = (std::max)(maxZ, z);
        };
        for (const GameObject& obj : m_objects)
        {
            if (obj.kind == ObjectKind::Building) { include(obj.transform.position.x, obj.transform.position.z); }
        }
        for (const RoadSegment& road : m_roadSystem.GetSegments())
        {
            include(road.start.x, road.start.z);
            include(road.end.x, road.end.z);
        }
        if (any)
        {
            m_titleFocus = DirectX::XMFLOAT3((minX + maxX) * 0.5f, 0.0f, (minZ + maxZ) * 0.5f);
            const float halfX = (maxX - minX) * 0.5f;
            const float halfZ = (maxZ - minZ) * 0.5f;
            m_titleRadius = std::sqrt(halfX * halfX + halfZ * halfZ) + m_field.GetCellSize();
        }
        else
        {
            m_titleFocus = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
            m_titleRadius = m_field.GetWorldWidth() * 0.25f;
        }
    }
    m_wasInTitleScene = inTitle;
    if (!inTitle)
    {
        return;
    }

    // ---- カメラ: 街の中心を見下ろしながら、ゆっくり回る ----
    m_titleTime += m_timeManager.GetDeltaTime();
    {
        constexpr float kPitch = 0.62f;         // 見下ろす角度(ラジアン。約35度)
        constexpr float kYawStart = 0.6f;
        constexpr float kYawSpeed = 0.08f;      // 1秒に回る角度(ラジアン)。約78秒で1周
        float distance = m_titleRadius * 2.8f + 4.0f;
        distance = (std::max)(10.0f, (std::min)(distance, m_field.GetWorldWidth() * 1.2f));
        const float yaw = kYawStart + m_titleTime * kYawSpeed;
        const float horizontal = std::cos(kPitch) * distance;
        m_camera.SetTarget(m_titleFocus.x, 0.0f, m_titleFocus.z);
        m_camera.SetPosition(
            m_titleFocus.x + std::sin(yaw) * horizontal,
            std::sin(kPitch) * distance,
            m_titleFocus.z + std::cos(yaw) * horizontal);
    }

    // ---- メニュー(スタート・終了) ----
    if (!m_uiAvailable)
    {
        // UIが使えない時は、Enterでだけ始められる。
        if (m_inputManager.IsKeyPressed(VK_RETURN)) { m_sceneManager.ChangeScene(SceneName::Game); }
        return;
    }

    TitleInput input;
    input.screenW = static_cast<float>(m_renderer.GetViewWidth());
    input.screenH = static_cast<float>(m_renderer.GetViewHeight());
    const POINT mouse = m_inputManager.GetMousePosition(); // ゲームビューの左上が原点
    input.mouseX = static_cast<float>(mouse.x);
    input.mouseY = static_cast<float>(mouse.y);
    input.pressed = m_inputManager.IsKeyPressed(VK_LBUTTON);
    input.down = m_inputManager.IsKeyDown(VK_LBUTTON);
    bool typing = false;
#ifdef ENABLE_EDITOR
    // デバッグGUI(ImGui)のパネルの上では反応しない。入力欄に文字を入力している間は、Enterも使わない。
    input.blockedByOtherUI = ImGui::GetIO().WantCaptureMouse;
    typing = ImGui::GetIO().WantTextInput;
#endif


    const TitleResult result = m_titleMenu.Update(input);
    if (result.command == TitleCommand::Start || (!typing && m_inputManager.IsKeyPressed(VK_RETURN)))
    {
        m_sceneManager.ChangeScene(SceneName::Game);
    }
    else if (result.command == TitleCommand::Quit)
    {
        PostMessage(m_hwnd, WM_CLOSE, 0, 0); // ウィンドウを閉じる(Windowが終了の処理を行う)
    }
}

bool Game::IsInGameScene() const
{
    return m_sceneManager.GetCurrentSceneName() == SceneName::Game;
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
    state.cash = m_gameEconomy.GetCash();
    state.stats = m_demandSystem.GetStats();

    // カーソルの横の建設費タグ。建物・道路の配置が今フレームにGameContext::costPreviewへ書いたものを渡す。
    state.costTag = m_context.costPreview;
    POINT mouse = m_inputManager.GetMousePosition();
    state.cursorX = static_cast<float>(mouse.x);
    state.cursorY = static_cast<float>(mouse.y);
    state.menu = BuildMenuModel();
    return state;
}

namespace
{
    // メニューに出す日本語の名前。ゲームの中身(建物・道路の種類)が増えたら、ここに足す。
    // 値段は、BuildingDefinition::cost・RoadTypeDefinition::costPerCell(建設費の表)から読むので、表の数字を変えれば、メニューも変わる。
    const char* BuildingMenuName(BuildingType type)
    {
        switch (type)
        {
        case BuildingType::House:  return "住宅";
        case BuildingType::Office: return "オフィス";
        case BuildingType::Shop:   return "お店";
        }
        return "?";
    }

    UIIcon BuildingMenuIcon(BuildingType type)
    {
        switch (type)
        {
        case BuildingType::House:  return UIIcon::House;
        case BuildingType::Office: return UIIcon::Office;
        case BuildingType::Shop:   return UIIcon::Shop;
        }
        return UIIcon::House;
    }

    const char* RoadMenuName(RoadType type)
    {
        switch (type)
        {
        case RoadType::Narrow: return "細い道";
        case RoadType::Normal: return "ふつうの道";
        case RoadType::Large:  return "広い道";
        }
        return "?";
    }

    UIIcon RoadMenuIcon(RoadType type)
    {
        switch (type)
        {
        case RoadType::Narrow: return UIIcon::RoadNarrow;
        case RoadType::Normal: return UIIcon::RoadNormal;
        case RoadType::Large:  return UIIcon::RoadLarge;
        }
        return UIIcon::RoadNormal;
    }
}

MenuModel Game::BuildMenuModel() const
{
    MenuModel model;
    const int cash = m_gameEconomy.GetCash();
    const bool buildingTool = (m_context.placementTool == PlacementTool::Building);
    const bool roadTool = (m_context.placementTool == PlacementTool::Road);

    const BuildingType buildingTypes[] = { BuildingType::House, BuildingType::Office, BuildingType::Shop };
    for (BuildingType type : buildingTypes)
    {
        const BuildingDefinition& def = GetBuildingDefinition(type);
        MenuItem item;
        item.icon = BuildingMenuIcon(type);
        item.name = BuildingMenuName(type);
        item.detail = std::to_string(def.cost) + "円";
        item.affordable = (cash >= def.cost);
        item.selected = buildingTool && (m_buildController.GetSelectedType() == type);
        item.command = { MenuCommandType::SelectBuilding, static_cast<int>(type) };
        model.buildings.push_back(item);
    }

    const RoadType roadTypes[] = { RoadType::Narrow, RoadType::Normal, RoadType::Large };
    for (RoadType type : roadTypes)
    {
        const RoadTypeDefinition& def = GetRoadTypeDefinition(type);
        MenuItem item;
        item.icon = RoadMenuIcon(type);
        item.name = RoadMenuName(type);
        item.detail = std::to_string(def.costPerCell) + "円/マス";
        item.affordable = (cash >= def.costPerCell);
        item.selected = roadTool && (m_context.roadType == type);
        item.command = { MenuCommandType::SelectRoad, static_cast<int>(type) };
        model.roads.push_back(item);
    }

    // 道路の引き方。道路を選んでいない時も、今の設定が分かるよう、選択の印を出す。
    MenuItem straight;
    straight.icon = UIIcon::RoadStraight;
    straight.name = "直線";
    straight.selected = (m_context.roadDrawMode == RoadDrawMode::Straight);
    straight.command = { MenuCommandType::SetRoadDrawMode, static_cast<int>(RoadDrawMode::Straight) };
    model.drawModes.push_back(straight);
    MenuItem freehand;
    freehand.icon = UIIcon::RoadFreehand;
    freehand.name = "自由描き";
    freehand.selected = (m_context.roadDrawMode == RoadDrawMode::Curve);
    freehand.command = { MenuCommandType::SetRoadDrawMode, static_cast<int>(RoadDrawMode::Curve) };
    model.drawModes.push_back(freehand);

    // 情報タブ。
    const DemandStats& stats = m_demandSystem.GetStats();
    model.info.push_back({ "日数", std::to_string(m_gameClock.GetDayCount()) + "日目" });
    model.info.push_back({ "所持金", std::to_string(cash) + "円" });
    model.info.push_back({ "人口", std::to_string(stats.populationTotal) + "人" });
    model.info.push_back({ "住民(代表)", std::to_string(stats.agentCount) + "人" });
    model.info.push_back({ "雇用", std::to_string(stats.jobsFilled) + " / " + std::to_string(stats.jobCapacityTotal) });
    model.info.push_back({ "住宅 / 職場", std::to_string(stats.houseCount) + " / " + std::to_string(stats.officeCount) });

    // 配置中のしるし(閉じている間、左下に出す)。
    if (buildingTool)
    {
        model.activeToolLabel = BuildingMenuName(m_buildController.GetSelectedType());
    }
    else if (roadTool)
    {
        model.activeToolLabel = std::string("道路(") + RoadMenuName(m_context.roadType) + ")";
    }

    model.statusText = m_menuStatus;
    return model;
}

void Game::ApplyMenuCommand(const MenuCommand& command)
{
    switch (command.type)
    {
    case MenuCommandType::SelectBuilding:
        m_roadSystem.CancelPlacement();
        m_buildController.SetSelectedType(static_cast<BuildingType>(command.arg));
        m_context.placementTool = PlacementTool::Building;
        break;
    case MenuCommandType::SelectRoad:
        m_roadSystem.CancelPlacement();
        m_context.roadType = static_cast<RoadType>(command.arg);
        m_context.placementTool = PlacementTool::Road;
        break;
    case MenuCommandType::SetRoadDrawMode:
        m_roadSystem.CancelPlacement();
        m_context.roadDrawMode = static_cast<RoadDrawMode>(command.arg);
        break;
    case MenuCommandType::CancelPlacement:
        m_roadSystem.CancelPlacement();
        m_context.placementTool = PlacementTool::None;
        break;
    case MenuCommandType::Save:
        m_scenarioManager.Save(ScenarioManager::kQuickSavePath, m_menuStatus);
        break;
    case MenuCommandType::Load:
        m_scenarioManager.Load(ScenarioManager::kQuickSavePath, m_menuStatus);
        break;
    case MenuCommandType::None:
        break;
    }
}

void Game::HandleMenuKeys()
{
    if (m_context.mode != GameMode::Playing)
    {
        return;
    }

    bool overImGui = false;
#ifdef ENABLE_EDITOR
    // デバッグパネルの入力欄に文字を入力している間は、キーをゲームの操作として扱わない。
    if (ImGui::GetIO().WantTextInput)
    {
        return;
    }
    overImGui = ImGui::GetIO().WantCaptureMouse;
#endif

    // Esc、または右クリック(ImGuiのパネルの上の右クリックは除く)。
    const bool escape = m_inputManager.IsKeyPressed(VK_ESCAPE);
    const bool cancel = escape || (m_inputManager.IsActionPressed(InputAction::Cancel) && !overImGui);

    if (m_gameUI.IsMenuOpen())
    {
        // 開いている: 「戻る」と同じ。
        if (cancel)
        {
            m_gameUI.CloseMenu();
            m_context.inputConsumed = true;
        }
        return;
    }

    if (m_context.placementTool != PlacementTool::None)
    {
        // 配置中: 置きかけの道路があれば、先にそれをやめる(RoadSystemが同じキーで行う)。何も置きかけでなければ、配置ツール自体をやめる。
        if (cancel && !m_roadSystem.HasPendingPlacement())
        {
            m_context.placementTool = PlacementTool::None;
            m_context.inputConsumed = true;
        }
        return;
    }

    // 何も配置していない時のEscは、メニューを開く。
    if (escape)
    {
        m_gameUI.OpenMenu();
    }
}

void Game::SyncMenuTimeScale()
{
    const bool open = m_gameUI.IsMenuOpen();
    if (open && !m_menuWasOpen)
    {
        // 開いた: 時間を止め、置きかけの道路はやめる(メニューを閉じた後に、中途半端な状態を残さない)。
        m_scaleBeforeMenu = m_gameClock.GetTimeScale();
        m_gameClock.SetTimeScale(0.0f);
        m_roadSystem.CancelPlacement();
    }
    else if (!open && m_menuWasOpen)
    {
        m_gameClock.SetTimeScale(m_scaleBeforeMenu);
    }
    m_menuWasOpen = open;
}

void Game::HandleScenarioHotkeys()
{
    // タイトル画面では街が無いので、ゲームシーンの間だけ有効にする。
    if (m_sceneManager.GetCurrentSceneName() != SceneName::Game)
    {
        return;
    }

#ifdef ENABLE_EDITOR
    // デバッグパネルの入力欄に文字を入力している間は、キーをショートカットとして扱わない。
    if (ImGui::GetIO().WantTextInput)
    {
        return;
    }
#endif

    std::string message;
    if (m_inputManager.IsKeyPressed(VK_F5))
    {
        m_scenarioManager.Save(ScenarioManager::kQuickSavePath, message);
    }
    if (m_inputManager.IsKeyPressed(VK_F9))
    {
        m_scenarioManager.Load(ScenarioManager::kQuickSavePath, message);
    }
}

EditorUi::FrameInfo Game::BuildEditorFrameInfo() const
{
    EditorUi::FrameInfo info;
    // HUDはゲームビューの中に描くので、メニューバーは、ウィンドウの最上端から始める(HUDの下には置かない)。
    info.hudTopInset = 0.0f;
    info.inGameScene = IsGameUIVisible();
    info.mouseOverGameHud = m_context.mouseOverUI;
    info.playing = (m_context.mode == GameMode::Playing);
    return info;
}

void Game::UpdateAssetDragState()
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload || !payload->Data || !payload->IsDataType(AssetBrowser::kModelPayloadType))
    {
        return;
    }

    AssetDragState& state = m_context.assetDrag;
    state.active = true;
    state.path = static_cast<const char*>(payload->Data);

    // ImGuiのどのウィンドウの上でもない(中央の透過部分や、ウィンドウの外)で、ゲームのHUDの上でもなければ、「ゲームの画面の上」。
    // (ドッキングの中央の透過部分では、ImGuiのHoveredWindowが空になる)
    ImGuiContext* imgui = ImGui::GetCurrentContext();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const bool insideWindow = mouse.x >= 0.0f && mouse.y >= 0.0f && mouse.x < display.x && mouse.y < display.y;
    state.overGameView = insideWindow && imgui && imgui->HoveredWindow == nullptr && !m_context.mouseOverUI;
    state.dropped = state.overGameView && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
}

void Game::OnMouseWheel(float notches)
{
    m_inputManager.AddMouseWheel(notches);
}

void Game::UpdateCityCamera()
{
    const bool inGame = IsInGameScene();
    const bool playing = inGame && m_context.mode == GameMode::Playing;

    if (inGame && !m_wasInGameScene)
    {
        // ゲームシーンが始まった: フィールドの中心を、見下ろす最初の見え方にする(タイトル画面の水平なカメラのままにしない)。
        m_cityCamera.SetBounds(m_field.GetWorldWidth() * 0.5f, m_field.GetWorldDepth() * 0.5f);
        m_cityCamera.Reset(m_camera);
    }
    else if (playing && !m_wasPlayingInGame)
    {
        // Editing(フリーカメラ)からPlayingへ戻った: 今の視点を引き継ぐ(視点が飛ばないように)。
        m_cityCamera.AdoptFrom(m_camera);
    }
    m_wasInGameScene = inGame;
    m_wasPlayingInGame = playing;

    // ホイールは、使わない時も取り出して捨てる(溜まったまま、次に使った時に一気に効かないように)。
    const float wheel = m_inputManager.ConsumeMouseWheel();
    if (!playing)
    {
        m_cameraDragging = false;
        return;
    }

    // ImGuiのパネルの上や、文字を入力している間は、カメラを動かさない。ゲームのHUDの上も同じ。
    bool typing = false;
    bool overUI = m_context.mouseOverUI;
#ifdef ENABLE_EDITOR
    typing = ImGui::GetIO().WantTextInput;
    overUI = overUI || ImGui::GetIO().WantCaptureMouse;
#endif

    const POINT mouse = m_inputManager.GetMousePosition(); // ゲームビューの左上が原点
    const float viewWidth = static_cast<float>(m_renderer.GetViewWidth());
    const float viewHeight = static_cast<float>(m_renderer.GetViewHeight());
    const bool insideView = mouse.x >= 0 && mouse.y >= 0 &&
                            static_cast<float>(mouse.x) < viewWidth && static_cast<float>(mouse.y) < viewHeight;
    const bool cursorValid = insideView && !overUI;

    // 中ボタン: ゲームビューの上で押した時に、つかみ始める。押している間は続け、離したら終わる。
    if (m_inputManager.IsActionDown(InputAction::CameraDrag))
    {
        if (!m_cameraDragging && m_inputManager.IsActionPressed(InputAction::CameraDrag) && cursorValid)
        {
            m_cameraDragging = true;
        }
    }
    else
    {
        m_cameraDragging = false;
    }

    CityCameraInput input;
    input.deltaSeconds = m_timeManager.GetDeltaTime(); // 実時間(ゲーム内時間の停止・倍速に影響されない)
    if (!typing && !m_gameUI.IsMenuOpen()) // メニューが開いている間は、キーでカメラを動かさない
    {
        input.panX = (m_inputManager.IsActionDown(InputAction::CameraRight) ? 1.0f : 0.0f) -
                     (m_inputManager.IsActionDown(InputAction::CameraLeft) ? 1.0f : 0.0f);
        input.panZ = (m_inputManager.IsActionDown(InputAction::CameraForward) ? 1.0f : 0.0f) -
                     (m_inputManager.IsActionDown(InputAction::CameraBack) ? 1.0f : 0.0f);
        input.fast = m_inputManager.IsActionDown(InputAction::CameraFast);
    }
    input.wheelNotches = cursorValid ? wheel : 0.0f; // UIの上のホイールは、ズームにしない
    input.dragging = m_cameraDragging;
    input.cursorValid = cursorValid;
    input.cursorX = static_cast<float>(mouse.x);
    input.cursorY = static_cast<float>(mouse.y);
    input.prevCursorX = static_cast<float>(m_prevCameraCursor.x);
    input.prevCursorY = static_cast<float>(m_prevCameraCursor.y);
    input.viewWidth = viewWidth;
    input.viewHeight = viewHeight;

    m_cityCamera.Update(input, m_camera);
    m_prevCameraCursor = mouse;
}

void Game::OnFilesDropped(const std::vector<std::wstring>& paths)
{
    m_pendingDrops.insert(m_pendingDrops.end(), paths.begin(), paths.end());
}

void Game::ProcessPendingDrops()
{
    if (m_pendingDrops.empty())
    {
        return;
    }

    std::vector<std::filesystem::path> sources;
    sources.reserve(m_pendingDrops.size());
    for (const std::wstring& path : m_pendingDrops)
    {
        sources.emplace_back(path);
    }
    m_pendingDrops.clear();

    // 元の場所のファイルは動かさず、Assets/Incoming/ へコピーする。
    const ImportResult result = AssetImporter::Import(sources);

    Debug::Info("AssetImporter: " + result.message);
    for (const std::string& warning : result.warnings)
    {
        Debug::Warning("AssetImporter: " + warning);
    }

    m_assetBrowser.ShowImportResult(result.message, result.importedFolders.empty() ? std::string() : result.importedFolders.front());
}

void Game::ApplyGameViewRect()
{
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
    if (!m_editorUi.GetGameViewRect(x, y, width, height))
    {
        return; // ドックスペースがまだ無い: ウィンドウ全体のまま
    }

    // ゲームの絵(3D・デバッグ線・HUD)は、この四角だけに描く。マウスの座標は、この四角の左上が原点になる。
    m_renderer.SetGameViewRect(x, y, width, height);
    m_inputManager.SetViewOrigin(static_cast<int>(m_renderer.GetViewX()), static_cast<int>(m_renderer.GetViewY()));

    // カメラの縦横比は、ビューの縦横比(ウィンドウの縦横比ではない)。
    m_camera.SetAspect(static_cast<float>(m_renderer.GetViewWidth()) / static_cast<float>(m_renderer.GetViewHeight()));
}

void Game::PlaceAssetAtViewCenter(const std::string& modelPath)
{
    // ゲームビューの中心(ビューの左上が原点の座標)。
    float x = static_cast<float>(m_renderer.GetViewWidth()) * 0.5f;
    float y = static_cast<float>(m_renderer.GetViewHeight()) * 0.5f;
    if (m_propPlacer.PlaceAtScreenPoint(modelPath, x, y))
    {
        return;
    }

    // 画面の中心の先に地面が無い(カメラが水平に近い・地面の外を向いている)時は、フィールドの中心へ置く。
    if (!m_propPlacer.PlaceAt(modelPath, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f)))
    {
        Debug::Warning("Game: 飾りを置けませんでした(モデルを読めません) : " + modelPath);
    }
    else
    {
        Debug::Info("Game: 画面の中心に地面が見えないので、フィールドの中心へ置きました");
    }
}

bool Game::IsFolderUsedByProps(const std::string& folder) const
{
    for (const GameObject& obj : m_objects)
    {
        if (obj.kind != ObjectKind::Prop)
        {
            continue;
        }
        const std::string& path = obj.modelPath;
        if (path.size() > folder.size() && path.compare(0, folder.size(), folder) == 0 && path[folder.size()] == '/')
        {
            return true;
        }
    }
    return false;
}

void Game::UpdateGameUI()
{
    if (!IsGameUIVisible())
    {
        // タイトル画面などでは、メニューを開いたままにしない(止めた時間も戻す)。
        m_gameUI.CloseMenu();
        SyncMenuTimeScale();
        return;
    }

    // Esc・右クリック(メニューを閉じる/配置をやめる/メニューを開く)は、メニューの更新より先に処理する。
    HandleMenuKeys();

    HudInput input;
    input.screenW = static_cast<float>(m_renderer.GetViewWidth());
    input.screenH = static_cast<float>(m_renderer.GetViewHeight());

    POINT mouse = m_inputManager.GetMousePosition();
    input.mouseX = static_cast<float>(mouse.x);
    input.mouseY = static_cast<float>(mouse.y);

    // HUD・メニューのボタンは、マウスの左ボタンだけで押す(Enter/Spaceは、建物を置く操作のためのキー)。
    input.pressed = m_inputManager.IsKeyPressed(VK_LBUTTON);
    input.down = m_inputManager.IsKeyDown(VK_LBUTTON);
    input.menuEnabled = (m_context.mode == GameMode::Playing); // メニューは、実際に遊ぶ状態(Playing)だけ


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

    if (result.menuCommand.type != MenuCommandType::None)
    {
        ApplyMenuCommand(result.menuCommand);
    }
    SyncMenuTimeScale();
}

void Game::Draw()
{
    // ゲーム内時刻から今の光の状態(太陽/月の向き・色・空の色)を求めて、描画に渡す。
    m_renderer.SetLighting(m_dayNightCycle.Evaluate(m_gameClock.GetTimeOfDayHours()));

    m_renderer.BeginFrame();

    m_sceneManager.Draw();

    // 街(地面・建物・道路・住民)は、ゲームシーンと、タイトル画面(背景)で描く。
    // 格子線・建設のゴースト・接続線・デバッグ表示は、ゲームシーンだけ。
    const bool inGame = IsInGameScene();
    const bool inTitle = IsInTitleScene();
    if (inGame || inTitle)
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

    const EditorUi::FrameInfo editorFrame = BuildEditorFrameInfo(); // ヒントの表示用(今フレームのモード・HUDの上か)

    if (inTitle)
    {
        m_roadSystem.Draw(); // 背景の街の道路(置きかけの表示は、更新されないので出ない)
    }

    if (inGame)
    {
#ifdef ENABLE_EDITOR
        const bool showGrid = m_showGridOverlay; // Editorメニューで切り替える
#else
        // エディタが無いビルド(Release): 格子線は、建物・道路を置こうとしている間だけ出す(置く場所の目安)。
        const bool showGrid = (m_context.placementTool != PlacementTool::None);
#endif
        if (showGrid)
        {
            m_field.DrawGridOverlay(m_debugRenderer, m_orientationRegistry);
        }
        m_propPlacer.Draw(); // Assetsからのドラッグ中のゴースト
#ifdef ENABLE_EDITOR
        m_demandSystem.Draw(m_debugRenderer); // 建物と道路の接続線(デバッグ表示)。エディタが無いビルドでは出さない
#endif

        if (m_context.mode == GameMode::Playing)
        {
            m_buildController.Draw(m_editorUi.PanelFlag("Build"));
            m_roadSystem.Draw();
        }
    }

#ifdef ENABLE_EDITOR
    m_debugEditor.Draw();
#endif

#ifdef ENABLE_EDITOR
    // 人流・需要シミュレーションの統計・プロファイラ・ログはモードによらず常時表示する
    // (Editorメニューのパネル一覧、またはタイトルバーの×で閉じられる)。
    m_demandSystem.DrawDebugUI(m_editorUi.PanelFlag("City Stats"));
    m_gameClock.DrawDebugUI(m_editorUi.PanelFlag("Time"));
    m_gameEconomy.DrawDebugUI(m_editorUi.PanelFlag("Economy"));
    m_assetBrowser.DrawDebugUI();
    m_assetViewer.DrawDebugUI();
    m_scenarioManager.DrawDebugUI(m_editorUi.PanelFlag("Scenario"));
    Profiler::DrawDebugUI(m_objects, m_roadSystem, m_occupancyGrid, m_demandSystem, m_editorUi.PanelFlag("Profiler"));
    Debug::DrawDebugUI(m_editorUi.PanelFlag("Log"));

    // ホバーのヒントとショートカット一覧。全てのパネルを描いた後。
    m_editorUi.EndFrame(editorFrame);
#endif

    // DebugRendererはフィールドの格子線・建物/道路の見た目にも使うため、
    // ENABLE_EDITORの外(Releaseビルドでも)でFlushする。
    m_debugRenderer.Flush(m_camera);

    // ゲームプレイ用UIは、3Dとデバッグ線の手前、ImGuiのデバッグパネルの奥に描く
    // (ImGuiはm_debugEditor.EndFrameで最前面に描かれる)。
    if (IsGameUIVisible() || (m_uiAvailable && inTitle))
    {
        // HUD(タイトル画面では、スタート・終了のメニュー)は、ゲームビューの中に描く(ビューの左上を原点にして、ビューの大きさに合わせて並べる)。
        m_uiRenderer.BeginFrame(
            static_cast<float>(m_renderer.GetViewWidth()),
            static_cast<float>(m_renderer.GetViewHeight()),
            m_renderer.GetViewX(),
            m_renderer.GetViewY());
        if (inTitle)
        {
            m_titleMenu.Draw(m_uiRenderer, m_gameUI.GetSkin()); // 見た目(スキン)は、HUD・ゲーム内メニューと同じ物を使う
        }
        else
        {
            m_gameUI.Draw(BuildHudState());
        }
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
    m_editorUi.SaveSettingsIfChanged(true);
#ifdef ENABLE_EDITOR
    m_debugEditor.Finalize();
#endif
    m_sceneManager.Finalize();
    m_renderer.Finalize();
}
