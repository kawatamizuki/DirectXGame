#include "Game.h"
#include "Debug.h"
#include "Profiler.h"
#include "InputAction.h"
#include "BakedModelCache.h"
#include "AssetImporter.h"
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
        0.1f,
        100.0f
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

    m_field.Initialize(20, 20, 1.0f);
    m_propPlacer.Initialize(&m_context, &m_field);

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
    m_propPlacer.Update();

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

#ifdef ENABLE_EDITOR
    m_debugEditor.Update();
    m_editorUi.SaveSettingsIfChanged();
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
    state.cash = m_gameEconomy.GetCash();
    state.stats = m_demandSystem.GetStats();

    // カーソルの横の建設費タグ。建物・道路の配置が今フレームにGameContext::costPreviewへ書いたものを渡す。
    state.costTag = m_context.costPreview;
    POINT mouse = m_inputManager.GetMousePosition();
    state.cursorX = static_cast<float>(mouse.x);
    state.cursorY = static_cast<float>(mouse.y);
    return state;
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
        return;
    }

    HudInput input;
    input.screenW = static_cast<float>(m_renderer.GetViewWidth());
    input.screenH = static_cast<float>(m_renderer.GetViewHeight());

    POINT mouse = m_inputManager.GetMousePosition();
    input.mouseX = static_cast<float>(mouse.x);
    input.mouseY = static_cast<float>(mouse.y);

    input.pressed = m_inputManager.IsActionPressed(InputAction::Decide);

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

    const EditorUi::FrameInfo editorFrame = BuildEditorFrameInfo(); // ヒントの表示用(今フレームのモード・HUDの上か)

    if (m_showGridOverlay)
    {
        m_field.DrawGridOverlay(m_debugRenderer, m_orientationRegistry);
    }
    m_propPlacer.Draw(); // Assetsからのドラッグ中のゴースト
    m_demandSystem.Draw(m_debugRenderer);

    if (m_context.mode == GameMode::Playing)
    {
        m_buildController.Draw(m_editorUi.PanelFlag("Build"));
        m_roadSystem.Draw();
    }

#ifdef ENABLE_EDITOR
    m_debugEditor.Draw();
#endif

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

    // DebugRendererはフィールドの格子線・建物/道路の見た目にも使うため、
    // ENABLE_EDITORの外(Releaseビルドでも)でFlushする。
    m_debugRenderer.Flush(m_camera);

    // ゲームプレイ用UIは、3Dとデバッグ線の手前、ImGuiのデバッグパネルの奥に描く
    // (ImGuiはm_debugEditor.EndFrameで最前面に描かれる)。
    if (IsGameUIVisible())
    {
        // HUDは、ゲームビューの中に描く(ビューの左上を原点にして、ビューの大きさに合わせて並べる)。
        m_uiRenderer.BeginFrame(
            static_cast<float>(m_renderer.GetViewWidth()),
            static_cast<float>(m_renderer.GetViewHeight()),
            m_renderer.GetViewX(),
            m_renderer.GetViewY());
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
    m_editorUi.SaveSettingsIfChanged(true);
#ifdef ENABLE_EDITOR
    m_debugEditor.Finalize();
#endif
    m_sceneManager.Finalize();
    m_renderer.Finalize();
}
