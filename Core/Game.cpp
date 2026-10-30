#include "Game.h"
#include "Debug.h"
#include "Profiler.h"
#include "InputAction.h"
#include "BakedModelCache.h"
#include "AssetImporter.h"
#include "MiniaudioBackend.h"
#include "AudioSettings.h"
#include "WorldScale.h"
#include "Collision.h"
#include "GameObjectFactory.h"
#include "EditorGuard.h"
#include "FacilityData.h"
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include "imgui.h"
#include "imgui_internal.h"


namespace
{
    // 「1年目 1月 第1週」のような、日付の文字(HUDと、情報タブで使う)。
    std::string CalendarText(const GameClock& clock)
    {
        return std::to_string(clock.GetYear()) + "年目 " + std::to_string(clock.GetMonth()) + "月 第" + std::to_string(clock.GetWeekOfMonth()) + "週"
            + (clock.IsSleeping() ? " (就寝)" : "");
    }
}

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

    // 音。デバイスが無い・初期化に失敗しても、ゲームは無音のまま続けられる(原因はログに出る)。
    m_audio.SetBackend(std::make_unique<MiniaudioBackend>());
    if (!m_audio.Initialize())
    {
        Debug::Warning("Game::Initialize : 音の初期化に失敗しました(音なしで続行します)");
    }
    // 音のパック(配布版では、mp3をそのまま入れず、これに入れてある)。無ければ、Sounds/の普通のファイルから鳴らす。
    m_audio.LoadPack("Sounds/sounds.pak");
    m_audio.LoadTable("Sounds/sounds.json");

    // プレイヤーが決めた音量(オプション)。ファイルが無ければ、全て100のまま。
    AudioSettings savedVolumes;
    if (savedVolumes.LoadFromFile(AudioSettings::kDefaultPath))
    {
        savedVolumes.ApplyTo(m_audio.Mixer());
    }
    m_savedAudioSettings = AudioSettings::FromMixer(m_audio.Mixer());
    m_audioPanel.Initialize(&m_audio, "Sounds/sounds.json");

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
    m_audioPanel.SetOpenFlag(m_editorUi.PanelFlag("Audio"));
    m_residentDebugPanel.SetOpenFlag(m_editorUi.PanelFlag("住民デバッグ"));
    m_simStatsPanel.SetOpenFlag(m_editorUi.PanelFlag("行動統計"));
    m_simReproPanel.SetOpenFlag(m_editorUi.PanelFlag("乱数・再現"));

    // 住民の判断・状態遷移の記録を有効にする(Editorのビルドだけ。Releaseでは無効のまま)。記録は、乱数・シミュレーションの結果を変えない。
    m_demandSystem.GetDebugRecorder().SetEnabled(true);

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
    // 施設(スーパー・映画館・カフェ)のモデル。暫定の見た目(BuildingType.cppのパスを差し替えれば、変わる)。
    if (!m_supermarketModel.LoadFromFile(m_renderer.GetDevice(), GetBuildingDefinition(BuildingType::Supermarket).modelPath))
    {
        Debug::Error("Game::Initialize failed : Supermarket model load failed");
        return false;
    }
    if (!m_cinemaModel.LoadFromFile(m_renderer.GetDevice(), GetBuildingDefinition(BuildingType::Cinema).modelPath))
    {
        Debug::Error("Game::Initialize failed : Cinema model load failed");
        return false;
    }
    if (!m_cafeModel.LoadFromFile(m_renderer.GetDevice(), GetBuildingDefinition(BuildingType::Cafe).modelPath))
    {
        Debug::Error("Game::Initialize failed : Cafe model load failed");
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
        camera.boundsMargin = WorldScale::FromLegacy(2.0f); // 最初は2.0(マス1.0の時の2マス分)
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
    m_buildController.SetModel(BuildingType::Supermarket, &m_supermarketModel);
    m_buildController.SetModel(BuildingType::Cinema, &m_cinemaModel);
    m_buildController.SetModel(BuildingType::Cafe, &m_cafeModel);
    m_buildController.SetOnBuildingPlaced([this](uint32_t buildingId, BuildingType type)
    {
        // 住宅を建てた時に、住居が無い住民がいたら、その住宅を「入居者を選ぶ」待ちにする。
        // (待ちの間は、新しい住民を生まれさせない。選び終わったら、選んだ住民が入居する/誰も選ばなければ、新しい住民が生まれる)
        if (type == BuildingType::House && m_demandSystem.CountHomelessResidents() > 0)
        {
            m_demandSystem.ReserveHouse(buildingId);
            m_moveInQueue.push_back(buildingId);
        }
    });

    m_roadSystem.Initialize(
        &m_context, &m_field, &m_occupancyGrid,
        &m_roadPlacementStrategy, &m_curveRoadPlacementStrategy, &m_roadMeshGenerator,
        &m_orientationRegistry);

    m_demolition.Initialize(&m_context, &m_field, &m_occupancyGrid, &m_roadSystem, &m_buildController, &m_demandSystem);
    m_demolition.SetOnWorldChanged([this]()
    {
        // 建物が消えた: エディタの選択(添字で持っている)・選んでいた建物・追跡していた住民(消えていれば)を外す。
        m_debugEditor.ClearSelection();
        m_selectedBuildingId = 0;
    });
    m_demolition.SetOnMessage([this](const std::string& text)
    {
        m_noticeText = text;
        m_noticeSecondsLeft = 4.0f;
    });

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
    m_scenarioManager.SetOnWorldReplaced([this]()
    {
        m_debugEditor.ClearSelection();
        m_demolition.Cancel(); // ロード中(F9)に、解体の選びかけがあれば、やめる(選んだ位置は、前の街の物)
        CancelMoveIn();        // 入居者を選ぶ待ちも、前の街の物なので、やめる(住宅・住民が入れ替わる)
        m_repro.NotifyWorldReplaced(); // 記録・再生の途中で、街が読み込み直されたら、中止する
    });
    m_repro.Initialize(&m_demandSystem, &m_scenarioManager, &m_gameClock, &m_objects);

    // 「Editor > エディタ機能のON/OFF」に出すスイッチ。全て登録し終えてから、保存してある設定を読む。
    m_buildController.RegisterEditorToggles(m_editorUi.Settings());
    m_editorUi.Settings().RegisterToggle("grid_overlay", "床の格子線を表示", &m_showGridOverlay,
        "地面のマス目の線を描く(青マスの線も含む)");
    m_editorUi.Settings().RegisterToggle("model_cache", "モデルキャッシュを使う", &m_useModelCache,
        "OFFにすると、これから読むモデルは毎回ローダーで読み直す(保存済みのキャッシュは残る)。起動時の読み込みは、常にキャッシュを使う");
    m_editorUi.Settings().RegisterToggle("hit_boxes", "クリックの当たり判定を表示(F3)", &m_showHitBoxes,
        "ゲーム内で、建物・住民をクリックできる範囲の箱を描く(建物=水色、住民=ピンクで、少し大きめ。Playingで見られる)");
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
    // 再現セッションの記録・再生中は、実時間のフレーム間隔ではなく、固定の刻みで時計を進める(PCの速さに、結果が依存しないため)。
    // それ以外の時は、これまでどおり、実時間。建物・道路の数の指紋は、記録中に変えられたら中止するための物。
    const uint64_t structureSignature = static_cast<uint64_t>(m_objects.size()) * 1000003ull + static_cast<uint64_t>(m_roadSystem.GetSegments().size());
    float clockDelta = m_repro.PrepareFrame(m_timeManager.GetDeltaTime(), IsInGameScene() && m_context.mode == GameMode::Playing, structureSignature);
    if (IsInGameScene() && m_context.mode == GameMode::Editing)
    {
        clockDelta = 0.0f; // Stop(Editing)の間は、住民だけでなく、ゲーム内の時間も止める(時刻は、Timeパネルのスライダーで動かせる)
    }
    m_gameClock.Update(clockDelta);

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
    UpdateBuildingSelection();

#ifdef ENABLE_EDITOR
    // Assetsからモデルをドラッグしている状態を読み取り、ゲームの画面の上で離されたら飾りを置く。
    UpdateAssetDragState();

    // 「住民デバッグ」の対象を、ゲームの選択(クリックした住民、またはクリックした住宅の住民)に連動させる。
    // ゲームシーンの外(タイトル画面)では、対象を外す。パネルを開いても、ゲーム内の時間は止めない。
    if (IsInGameScene())
    {
        uint32_t selectedResident = m_followAgentId;
        if (selectedResident == 0 && m_selectedBuildingId != 0)
        {
            selectedResident = m_demandSystem.FindResidentIdForHome(m_selectedBuildingId);
        }
        m_residentDebugPanel.NotifySelection(selectedResident);
    }
    else
    {
        m_residentDebugPanel.ClearTarget();
    }

#endif
    if (IsInGameScene())
    {
        m_propPlacer.Update();
    }

    HandleScenarioHotkeys();

#ifdef ENABLE_EDITOR
    // F3: クリックの当たり判定(建物・住民)の表示の切り替え(Debugビルド)。
    if (!ImGui::GetIO().WantTextInput && m_inputManager.IsKeyPressed(VK_F3))
    {
        m_showHitBoxes = !m_showHitBoxes;
    }

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
        m_demolition.Update(); // 解体ツールの間だけ動く(押している間は、自分でinputConsumedを立てる)

        if (m_noticeSecondsLeft > 0.0f)
        {
            m_noticeSecondsLeft -= m_timeManager.GetDeltaTime();
        }

        // 住民の移動・需要更新は、ゲーム内時間の倍率(停止/倍速)に従って進める。
        m_demandSystem.Update(m_gameClock.GetScaledDeltaSeconds(), m_context, m_roadSystem.GetSegments());

        // 一時停止の条件に当てはまるイベントが記録されていたら、シミュレーションの更新が済んだ、この安全な所で、すぐ時間を止める
        // (次のフレームを待つと、倍速では、1フレームで大きく進んでしまうため)。
        if (m_demandSystem.GetDebugRecorder().IsPauseActive())
        {
            SyncMenuTimeScale();
        }
    }
    else
    {
        // ゲームシーンでない(タイトルへ戻った)・Editingモードの間は、解体の選びかけを残さない(戻った時に、前の選択が、残らないように)。
        m_demolition.Cancel();
        m_roadSystem.CancelPlacement();     // Editing(Stop)の間は、道路の置きかけ・カーソルのマーカーも、残さない
        m_roadSystem.ClearHoverMarker();
    }

    // 音: 聞く位置はカメラ。時間は、ゲーム内の倍率に関係なく、実時間で進める(メニューで時間を止めても、曲のフェードは進む)。
    m_audio.SetListenerPosition(m_camera.GetPosition());
    m_audio.Update(m_timeManager.GetDeltaTime());
    SaveAudioSettingsIfChanged();
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
        m_audio.PlayBgm("bgm_title", 1.5f);     // タイトルの曲。ゆっくり出す
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
            m_titleRadius = std::sqrt(halfX * halfX + halfZ * halfZ) + WorldScale::FromLegacy(1.0f);
        }
        else
        {
            m_titleFocus = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
            m_titleRadius = m_field.GetWorldWidth() * 0.25f;
        }
    }
    if (!inTitle && m_wasInTitleScene)
    {
        m_audio.StopBgm(1.0f);                  // タイトルを離れたら、曲を消していく(スタート以外の経路でも)
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
        if (m_inputManager.IsKeyPressed(VK_RETURN))
        {
            m_audio.PlaySe("ui_open");
            m_sceneManager.ChangeScene(SceneName::Game);
        }
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
        m_audio.PlaySe("ui_open");              // 「スタート」で鳴らす(終了では鳴らさない)
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
    state.calendarText = CalendarText(m_gameClock);
    state.timeOfDayHours = m_gameClock.GetTimeOfDayHours();
    state.cash = m_gameEconomy.GetCash();
    state.stats = m_demandSystem.GetStats();

    // カーソルの横の建設費タグ。建物・道路の配置が今フレームにGameContext::costPreviewへ書いたものを渡す。
    state.costTag = m_context.costPreview;
    POINT mouse = m_inputManager.GetMousePosition();
    state.cursorX = static_cast<float>(mouse.x);
    state.cursorY = static_cast<float>(mouse.y);
    state.menu = BuildMenuModel();
    if (m_gameUI.IsResidentPanelOpen())
    {
        state.residentInfo = BuildResidentInfoModel();
    }
    if (m_gameUI.IsMoveInOpen())
    {
        state.moveIn = BuildMoveInModel();
    }
    if (m_followAgentId != 0)
    {
        state.hintText = "住民を追跡中: もう一度クリックで情報 / Esc・右クリックで解除";
    }
    else if (m_noticeSecondsLeft > 0.0f && !m_noticeText.empty())
    {
        state.hintText = m_noticeText; // 解体の結果・お金が足りない、など(少しの間だけ)
    }
    else if (m_context.placementTool == PlacementTool::Demolish)
    {
        state.hintText = "解体: 建物・道路をクリック/なぞって選び、離すと解体(建物1棟100円・道路は建設費の半額) / Esc・右クリックでやめる";
    }

    // 道路に接続できていない建物に、「道路がない！」を出す(建物の少し上)。
    if (IsInGameScene())
    {
        const UINT viewW = m_renderer.GetViewWidth();
        const UINT viewH = m_renderer.GetViewHeight();
        for (uint32_t id : m_demandSystem.GetBuildingsWithoutRoad())
        {
            const GameObject* obj = GameObjectFactory::FindById(m_objects, id);
            if (!obj)
            {
                continue;
            }
            DirectX::XMFLOAT3 above = obj->transform.position;
            above.y += WorldScale::FromLegacy(1.0f);
            POINT screen;
            if (m_camera.WorldToScreen(above, viewW, viewH, screen))
            {
                state.worldLabels.push_back({ static_cast<float>(screen.x), static_cast<float>(screen.y), "道路がない！" });
            }
        }

        // 家を失った住民の上に、「住居がない！」を出す(住民は小さいので、建物より少し低く)。
        for (uint32_t id : m_demandSystem.GetHomelessAgentObjectIds())
        {
            const GameObject* obj = GameObjectFactory::FindById(m_objects, id);
            if (!obj)
            {
                continue;
            }
            DirectX::XMFLOAT3 above = obj->transform.position;
            above.y += WorldScale::FromLegacy(0.7f);
            POINT screen;
            if (m_camera.WorldToScreen(above, viewW, viewH, screen))
            {
                state.worldLabels.push_back({ static_cast<float>(screen.x), static_cast<float>(screen.y), "住居がない！" });
            }
        }

        // 道路が無くなって帰れない住民の上に、「立ち往生！」を出す。
        for (uint32_t id : m_demandSystem.GetStrandedAgentObjectIds())
        {
            const GameObject* obj = GameObjectFactory::FindById(m_objects, id);
            if (!obj)
            {
                continue;
            }
            DirectX::XMFLOAT3 above = obj->transform.position;
            above.y += WorldScale::FromLegacy(0.7f);
            POINT screen;
            if (m_camera.WorldToScreen(above, viewW, viewH, screen))
            {
                state.worldLabels.push_back({ static_cast<float>(screen.x), static_cast<float>(screen.y), "立ち往生！" });
            }
        }
    }
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
        case BuildingType::Shop:   return "コンビニ";
        case BuildingType::Supermarket: return "スーパー";
        case BuildingType::Cinema: return "映画館";
        case BuildingType::Cafe:   return "カフェ";
        }
        return "?";
    }

    UIIcon BuildingMenuIcon(BuildingType type)
    {
        switch (type)
        {
        case BuildingType::House:  return UIIcon::House;
        case BuildingType::Office: return UIIcon::Office;
        case BuildingType::Shop:
        case BuildingType::Supermarket:
        case BuildingType::Cinema:
        case BuildingType::Cafe:   return UIIcon::Shop; // 施設の専用アイコンは、後で足す
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
    const bool demolishTool = (m_context.placementTool == PlacementTool::Demolish);

    const BuildingType buildingTypes[] =
    {
        BuildingType::House, BuildingType::Office, BuildingType::Shop,
        BuildingType::Supermarket, BuildingType::Cinema, BuildingType::Cafe
    };
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

    // ツール(解体)。
    {
        MenuItem demolish;
        demolish.icon = UIIcon::Demolish;
        demolish.name = "解体";
        demolish.detail = std::to_string(kBuildingDemolitionCost) + "円/棟・道路半額";
        demolish.affordable = CanSelectDemolitionTool(cash);
        demolish.selected = demolishTool;
        // お金が足りない(解体の最小の費用も、持っていない)時は、押しても、ツールに入らず、メニューも閉じない(理由を知らせる)。
        demolish.command = demolish.affordable ? MenuCommand{ MenuCommandType::SelectDemolish, 0 } : MenuCommand{ MenuCommandType::NotifyBlocked, 0 };
        model.tools.push_back(demolish);
    }

    // 情報タブ。
    const DemandStats& stats = m_demandSystem.GetStats();
    model.info.push_back({ "日付", CalendarText(m_gameClock) });
    model.info.push_back({ "所持金", std::to_string(cash) + "円" });
    model.info.push_back({ "人口", std::to_string(stats.populationTotal) + "人" });
    model.info.push_back({ "住民", std::to_string(stats.agentCount) + "人(需要が不足: " + std::to_string(stats.unmetDemand) + "人)" });
    if (stats.homelessCount > 0)
    {
        model.info.push_back({ "住居がない住民", std::to_string(stats.homelessCount) + "人" });
    }
    if (stats.strandedCount > 0)
    {
        model.info.push_back({ "立ち往生の住民", std::to_string(stats.strandedCount) + "人" });
    }
    model.info.push_back({ "住宅 / 施設", std::to_string(stats.houseCount) + " / " + std::to_string(stats.facilityCount) });
    model.info.push_back({ "今週の来客 / 売上", std::to_string(stats.visitsThisWeek) + "人 / " + std::to_string(stats.salesThisWeek) + "円" });
    model.info.push_back({ "先週の来客 / 売上", std::to_string(stats.visitsLastWeek) + "人 / " + std::to_string(stats.salesLastWeek) + "円" });
    model.info.push_back({ "今月の来客 / 売上", std::to_string(stats.visitsThisMonth) + "人 / " + std::to_string(stats.salesThisMonth) + "円" });

    // 配置中のしるし(閉じている間、左下に出す)。
    if (buildingTool)
    {
        model.activeToolLabel = BuildingMenuName(m_buildController.GetSelectedType());
    }
    else if (roadTool)
    {
        model.activeToolLabel = std::string("道路(") + RoadMenuName(m_context.roadType) + ")";
    }
    else if (demolishTool)
    {
        model.activeToolLabel = "解体";
    }

    model.statusText = m_menuStatus;
    if (m_noticeSecondsLeft > 0.0f && !m_noticeText.empty())
    {
        model.noticeText = m_noticeText; // 押せない項目の理由など(少しの間だけ)
    }

    // オプション: 音量(0〜100)。今のミキサーの値を、そのまま見せる。
    const AudioSettings volumes = AudioSettings::FromMixer(m_audio.Mixer());
    model.sliders.push_back(MenuSlider{ "全体", static_cast<int>(MenuVolumeChannel::Master), volumes.master });
    model.sliders.push_back(MenuSlider{ "BGM", static_cast<int>(MenuVolumeChannel::Bgm), volumes.bgm });
    model.sliders.push_back(MenuSlider{ "効果音", static_cast<int>(MenuVolumeChannel::Se), volumes.se });
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
    case MenuCommandType::NotifyBlocked:
        m_noticeText = "お金が足りないので、選択できません";
        m_noticeSecondsLeft = 4.0f;
        break;
    case MenuCommandType::SelectDemolish:
        if (!CanSelectDemolitionTool(m_gameEconomy.GetCash()))
        {
            // (メニューの項目は、お金が足りない時は、NotifyBlockedになっているが、念のため、ここでも守る)
            m_noticeText = "お金が足りないので、選択できません";
            m_noticeSecondsLeft = 4.0f;
            break;
        }
        m_roadSystem.CancelPlacement();
        m_demolition.Cancel();
        m_context.placementTool = PlacementTool::Demolish;
        break;
    case MenuCommandType::CancelPlacement:
        m_roadSystem.CancelPlacement();
        m_demolition.Cancel();
        m_context.placementTool = PlacementTool::None;
        break;
    case MenuCommandType::Save:
        m_scenarioManager.Save(ScenarioManager::kQuickSavePath, m_menuStatus);
        break;
    case MenuCommandType::Load:
        m_scenarioManager.Load(ScenarioManager::kQuickSavePath, m_menuStatus);
        break;
    case MenuCommandType::SetVolume:
    {
        AudioSettings volumes = AudioSettings::FromMixer(m_audio.Mixer());
        switch (static_cast<MenuVolumeChannel>(command.arg))
        {
        case MenuVolumeChannel::Master: volumes.master = AudioSettings::Clamp(command.value); break;
        case MenuVolumeChannel::Bgm:    volumes.bgm = AudioSettings::Clamp(command.value); break;
        case MenuVolumeChannel::Se:     volumes.se = AudioSettings::Clamp(command.value); break;
        default: break;
        }
        volumes.ApplyTo(m_audio.Mixer()); // 保存は、つかみ終わってから(SaveAudioSettingsIfChanged)
        break;
    }
    case MenuCommandType::None:
        break;
    }
}

void Game::SaveAudioSettingsIfChanged()
{
    // スライダーをつかんでいる間は、毎フレーム書かない。離したら、変わった分を1回だけ保存する。
    if (m_inputManager.IsKeyDown(VK_LBUTTON) || m_gameUI.IsMenuEditingNumber())
    {
        return;
    }
    const AudioSettings current = AudioSettings::FromMixer(m_audio.Mixer());
    if (current == m_savedAudioSettings)
    {
        return;
    }
    if (current.SaveToFile(AudioSettings::kDefaultPath))
    {
        m_savedAudioSettings = current;
    }
    else
    {
        Debug::Warning("Game: 音量の設定を保存できませんでした: " + std::string(AudioSettings::kDefaultPath));
        m_savedAudioSettings = current; // 毎フレーム、失敗を繰り返さない
    }
}

void Game::HandleMenuKeys()
{
    if (m_context.mode != GameMode::Playing)
    {
        return;
    }

    // オプションの数字欄に入力中: Esc・右クリックは、入力をやめるだけ(メニューは閉じない。GameMenuが処理する)。
    if (m_gameUI.IsMenuOpen() && m_gameUI.IsMenuEditingNumber())
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

    // 「入居者を選ぶ」パネルが開いている: Esc・右クリックは、「新しい住民を迎える」と同じ(誰も選ばずに、閉じる)。
    if (m_gameUI.IsMoveInOpen())
    {
        if (cancel)
        {
            ResolveMoveIn(0);
            m_context.inputConsumed = true;
        }
        return;
    }

    // 「住民の情報」パネルが開いている: Esc・右クリックで、パネルだけを閉じる(メニューは開かない)。
    if (m_gameUI.IsResidentPanelOpen())
    {
        if (cancel)
        {
            m_gameUI.CloseResidentPanel();
            m_context.inputConsumed = true;
        }
        return;
    }

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

    // 住民を追跡している間: Esc・右クリックで、追跡だけをやめる(メニューは開かない)。
    if (m_followAgentId != 0 && m_context.placementTool == PlacementTool::None)
    {
        if (cancel)
        {
            m_followAgentId = 0;
            m_context.inputConsumed = true;
        }
        return;
    }

    if (m_context.placementTool != PlacementTool::None)
    {
        // 配置中: 置きかけの道路があれば、先にそれをやめる(RoadSystemが同じキーで行う)。何も置きかけでなければ、配置ツール自体をやめる。
        if (cancel && m_demolition.HasPending())
        {
            m_demolition.Cancel(); // 解体の選びかけがあれば、先にそれだけをやめる
            m_context.inputConsumed = true;
        }
        else if (cancel && !m_roadSystem.HasPendingPlacement())
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
    const bool open = m_gameUI.IsMenuOpen() || m_gameUI.IsResidentPanelOpen() || m_gameUI.IsMoveInOpen();
    // 建物・道路・解体のツールを選んでいる間(画面を動かして置き場所を探している間)も、時間を止める。
    // ゲーム画面を出していない時(タイトルなど)に、ツールの選択が残っていても止めない。
    const bool toolActive = IsGameUIVisible() && (m_context.mode == GameMode::Playing)
        && (m_context.placementTool != PlacementTool::None);
    // 「住民デバッグ」の一時停止の条件に当てはまって止まっている間も、止める(止める印は、記録部が持つ。解除は、パネルの「再開」)。
    // 元の倍率の記憶・復帰は、メニュー・ツールの停止と同じ仕組みで行うので、重なっても(メニューを開閉しても)、倍率は正しく戻る。
    const bool debugPause = IsGameUIVisible() && (m_context.mode == GameMode::Playing) && m_demandSystem.GetDebugRecorder().IsPauseActive();
    const bool paused = open || toolActive || debugPause;
    if (m_repro.OwnsTimeScale())
    {
        // 再現セッションの再生中は、時間の倍率を、記録どおりに入れる(ここでは触らない)。メニューなどで時間を止める時は、
        // 倍率ではなく、再生のフレームを止める(ふだんのプレイと同じく、メニューを開くと時間が止まり、再生の結果は変わらない)。
        m_repro.SetUserPaused(paused);
        if (open && !m_menuWasOpen)
        {
            m_roadSystem.CancelPlacement();
            m_audio.PlaySe("ui_open");
        }
        m_menuWasOpen = open;
        return;
    }
    if (paused && !m_timePausedByUi)
    {
        // 止める: 元の倍率を覚える(閉じたら戻す)。
        m_scaleBeforeMenu = m_gameClock.GetTimeScale();
        m_gameClock.SetTimeScale(0.0f);
    }
    else if (!paused && m_timePausedByUi)
    {
        m_gameClock.SetTimeScale(m_scaleBeforeMenu);
    }
    m_timePausedByUi = paused;

    if (open && !m_menuWasOpen)
    {
        // 開いた: 置きかけの道路はやめる(メニューを閉じた後に、中途半端な状態を残さない)。
        m_roadSystem.CancelPlacement();
        m_audio.PlaySe("ui_open");      // メニューを開いた時(ボタンでもEscでも)。閉じる時は鳴らさない
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

        // ゲームを始める: 時計を、最初の朝(第1週)に戻す(タイトル画面の間にも時計は動いていて、週が進んでしまうため)。
        m_gameClock.ResetToDefault();
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

    // 左ボタンのドラッグ(UpdateBuildingSelectionが、押したまま動かした時に、始める): つかんでいる間、地面をつかんで動かす。
    const bool leftDragging = m_leftDragActive && m_inputManager.IsKeyDown(VK_LBUTTON);

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
    input.dragging = m_cameraDragging || leftDragging;
    // 感度: 左ボタンのドラッグだけ、倍率を下げる(中ボタンも押している時は、中ボタンの動き=1)。
    input.dragScale = m_cameraDragging ? 1.0f : m_cityCamera.GetSettings().leftDragScale;
    input.cursorValid = cursorValid;
    input.cursorX = static_cast<float>(mouse.x);
    input.cursorY = static_cast<float>(mouse.y);
    input.prevCursorX = static_cast<float>(m_prevCameraCursor.x);
    input.prevCursorY = static_cast<float>(m_prevCameraCursor.y);
    if (m_leftDragJustStarted)
    {
        // ドラッグの始まり: つかむ点を、押した位置にする(動き始めるまでの、数ピクセルも、カメラに反映する)。
        input.prevCursorX = static_cast<float>(m_leftPressPos.x);
        input.prevCursorY = static_cast<float>(m_leftPressPos.y);
    }
    input.viewWidth = viewWidth;
    input.viewHeight = viewHeight;

    // 住民の追跡: 追う点を渡す。キーでの移動・ドラッグをしたら(自分で動かしたい合図)、追跡をやめる。
    if (m_followAgentId != 0)
    {
        const GameObject* target = GameObjectFactory::FindById(m_objects, m_followAgentId);
        const bool userMoved = (input.panX != 0.0f) || (input.panZ != 0.0f) || m_cameraDragging || leftDragging;
        if (!target || userMoved)
        {
            m_followAgentId = 0;
        }
        else
        {
            input.followActive = true;
            input.followX = target->transform.position.x;
            input.followZ = target->transform.position.z;
        }
    }

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

namespace
{
    // 住民(Agent)のクリックの当たり判定を、モデルの箱より、この長さ(マス基準)だけ、全ての方向に大きくする(住民は小さいため)。
    constexpr float kAgentPickMarginLegacyCells = 0.12f;

    // クリックの当たり判定に使う、物の箱(モデルのローカル座標)。建物は、モデルの箱そのまま。住民だけ、少し大きくする。
    // ローカルの箱は、物の拡大(scale)を掛けて、ワールドの大きさになるので、ワールドで「余白の長さ」になるよう、拡大で割って足す。
    // 選択・追跡のしるし(線だけ。当たり判定の箱ではない)。
    // 足元の輪: 中心center、半径radius(地面のすぐ上)。
    void AddGroundRing(DebugRenderer& renderer, const DirectX::XMFLOAT3& center, float radius, const DirectX::XMFLOAT4& color)
    {
        constexpr int kSegments = 24;
        constexpr float kTwoPi = 6.28318530718f;
        const float y = 0.03f;
        DirectX::XMFLOAT3 previous{ center.x + radius, y, center.z };
        for (int i = 1; i <= kSegments; ++i)
        {
            const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(kSegments);
            const DirectX::XMFLOAT3 current{ center.x + std::cos(angle) * radius, y, center.z + std::sin(angle) * radius };
            renderer.AddLine(previous, current, color);
            previous = current;
        }
    }

    // 足元の四角: 物の箱(モデルの箱を、位置・向き・拡大で動かした、8つの角)を、地面に落とした、外側の四角。marginだけ外へ広げる。
    void AddGroundOutline(DebugRenderer& renderer, const GameObject& obj, float margin, const DirectX::XMFLOAT4& color)
    {
        const DirectX::XMFLOAT3 mn = obj.model->GetBoundsMin();
        const DirectX::XMFLOAT3 mx = obj.model->GetBoundsMax();
        const DirectX::XMMATRIX world = obj.transform.GetWorldMatrix();
        float minX = FLT_MAX, maxX = -FLT_MAX, minZ = FLT_MAX, maxZ = -FLT_MAX;
        for (int i = 0; i < 8; ++i)
        {
            const DirectX::XMFLOAT3 corner{ (i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z };
            DirectX::XMFLOAT3 p;
            DirectX::XMStoreFloat3(&p, DirectX::XMVector3Transform(DirectX::XMLoadFloat3(&corner), world));
            minX = (std::min)(minX, p.x); maxX = (std::max)(maxX, p.x);
            minZ = (std::min)(minZ, p.z); maxZ = (std::max)(maxZ, p.z);
        }
        const float y = 0.03f;
        const DirectX::XMFLOAT3 a{ minX - margin, y, minZ - margin }, b{ maxX + margin, y, minZ - margin };
        const DirectX::XMFLOAT3 c{ maxX + margin, y, maxZ + margin }, d{ minX - margin, y, maxZ + margin };
        renderer.AddLine(a, b, color);
        renderer.AddLine(b, c, color);
        renderer.AddLine(c, d, color);
        renderer.AddLine(d, a, color);
    }

    void PickBounds(const GameObject& obj, DirectX::XMFLOAT3& outMin, DirectX::XMFLOAT3& outMax)
    {
        outMin = obj.model->GetBoundsMin();
        outMax = obj.model->GetBoundsMax();
        if (obj.kind != ObjectKind::Agent)
        {
            return;
        }
        const float margin = WorldScale::FromLegacy(kAgentPickMarginLegacyCells);
        const DirectX::XMFLOAT3& s = obj.transform.scale;
        const float mx = margin / (std::max)(std::fabs(s.x), 1e-4f);
        const float my = margin / (std::max)(std::fabs(s.y), 1e-4f);
        const float mz = margin / (std::max)(std::fabs(s.z), 1e-4f);
        outMin = { outMin.x - mx, outMin.y - my, outMin.z - mz };
        outMax = { outMax.x + mx, outMax.y + my, outMax.z + mz };
    }
}

void Game::UpdateBuildingSelection()
{
    // Playingのゲームシーンだけ。
    if (!IsInGameScene() || m_context.mode != GameMode::Playing)
    {
        m_selectedBuildingId = 0;
        m_followAgentId = 0;
        m_leftGesture.Reset(); m_leftDragActive = m_leftDragJustStarted = false;
        return;
    }
    // 建物・道路を置く操作を始めたら、選択も追跡も外す。左ボタンは、配置の道具のもの(自由描きのドラッグなど)なので、カメラは動かさない。
    if (m_context.placementTool != PlacementTool::None)
    {
        m_selectedBuildingId = 0;
        m_followAgentId = 0;
        m_leftGesture.Reset(); m_leftDragActive = m_leftDragJustStarted = false;
        return;
    }
    // メニュー・パネルが開いている間は、選択を触らない。
    if (m_gameUI.IsMenuOpen() || m_gameUI.IsResidentPanelOpen() || m_gameUI.IsMoveInOpen())
    {
        m_leftGesture.Reset(); m_leftDragActive = m_leftDragJustStarted = false;
        return;
    }
    // 選んでいた建物・追跡していた住民が、無くなっていたら(ロードなど)、外す。
    if (m_selectedBuildingId != 0 && !GameObjectFactory::FindById(m_objects, m_selectedBuildingId))
    {
        m_selectedBuildingId = 0;
    }
    if (m_followAgentId != 0 && !GameObjectFactory::FindById(m_objects, m_followAgentId))
    {
        m_followAgentId = 0;
    }

    // ---- 左ボタン: クリックか、ドラッグか ----
    // 世界の上(HUD・メニュー・ImGuiのパネルの上ではない所)で押した時に、始まる。押したまま、少し(6ピクセル)以上動かしたら、
    // ドラッグ(カメラの平行移動。UpdateCityCameraが動かす)。動かさずに離したら、クリック(下で、建物・住民を探す)。
    const POINT cursor = m_inputManager.GetMousePosition(); // ゲームビューの左上が原点
    const bool canStart = !m_context.inputConsumed && !m_context.mouseOverUI && !IsMouseOverEditorUi();
    const ClickDragGesture::Result gesture = m_leftGesture.Update(
        m_inputManager.IsKeyPressed(VK_LBUTTON), canStart, m_inputManager.IsKeyDown(VK_LBUTTON),
        static_cast<float>(cursor.x), static_cast<float>(cursor.y));
    m_leftPressPos = POINT{ static_cast<LONG>(gesture.pressX), static_cast<LONG>(gesture.pressY) };
    m_leftDragJustStarted = gesture.dragStarted;
    m_leftDragActive = gesture.dragging;
    if (gesture.dragging)
    {
        m_context.inputConsumed = true; // ドラッグ中は、他の操作へ抜けない
    }
    if (!gesture.clicked)
    {
        return; // 押している最中・ドラッグ中・何も無い時は、クリックの判定をしない
    }
    // ---- クリック(押した位置で判定) ----
    // カーソルの下の物を探す。住民は小さいので、(少し大きくした箱で)先に探す。見えている住民と、追跡中の住民(建物の中にいても)が対象。
    const POINT mouse = m_leftPressPos;
    const Ray ray = m_camera.ScreenPointToRay(mouse, m_renderer.GetViewWidth(), m_renderer.GetViewHeight());

    uint32_t agentHitId = 0;
    float agentNearest = FLT_MAX;
    uint32_t buildingHitId = 0;
    BuildingType buildingHitType = BuildingType::House;
    float buildingNearest = FLT_MAX;
    for (const GameObject& obj : m_objects)
    {
        if (!obj.model)
        {
            continue;
        }
        const bool isAgent = (obj.kind == ObjectKind::Agent);
        if (!isAgent && obj.kind != ObjectKind::Building)
        {
            continue;
        }
        if (isAgent && !obj.visible && obj.id != m_followAgentId)
        {
            continue; // 建物の中にいる住民は、追跡中でない限り、クリックの対象にしない(建物のクリックを邪魔しないため)
        }
        DirectX::XMFLOAT3 boundsMin, boundsMax;
        PickBounds(obj, boundsMin, boundsMax);
        float distance = 0.0f;
        if (!IntersectRayOBB(ray, obj.transform, boundsMin, boundsMax, distance))
        {
            continue;
        }
        if (isAgent)
        {
            if (distance < agentNearest) { agentNearest = distance; agentHitId = obj.id; }
        }
        else if (distance < buildingNearest)
        {
            buildingNearest = distance;
            buildingHitId = obj.id;
            buildingHitType = obj.buildingType;
        }
    }

    // ---- 住民をクリック ----
    if (agentHitId != 0)
    {
        m_context.inputConsumed = true;
        if (agentHitId == m_followAgentId)
        {
            // 追跡中の住民を、もう1回クリック: 住民の情報を出す(開いている間は、時間が止まる)。
            m_gameUI.OpenResidentPanel();
        }
        else
        {
            // 追跡を始める(カメラが住民を追う)。選んでいた建物は、外す。
            m_followAgentId = agentHitId;
            m_selectedBuildingId = 0;
        }
        return;
    }

    // ---- 建物をクリック ----
    if (buildingHitId != 0)
    {
        m_context.inputConsumed = true; // 建物のクリックは、ここで受け取る(他の操作へ抜けない)
        if (buildingHitId == m_selectedBuildingId)
        {
            // 同じ建物をもう1回クリック: 住宅なら、住民の情報を出す。
            if (buildingHitType == BuildingType::House)
            {
                m_gameUI.OpenResidentPanel();
            }
        }
        else
        {
            m_selectedBuildingId = buildingHitId;
            m_followAgentId = 0; // 建物を選んだら、追跡はやめる
        }
        return;
    }

    // ---- 何も無い所(地面)をクリック: 選択も追跡も外す ----
    m_selectedBuildingId = 0;
    m_followAgentId = 0;
}
ResidentInfoModel Game::BuildResidentInfoModel() const
{
    ResidentInfoModel model;
    model.title = "住民の情報";

    // 追跡している住民があれば、その住民。無ければ、選んでいる住宅の住民。
    const bool byAgent = (m_followAgentId != 0);
    const ResidentInfo info = byAgent ? m_demandSystem.GetResidentInfoForAgent(m_followAgentId)
                                      : m_demandSystem.GetResidentInfoForHome(m_selectedBuildingId);
    model.subtitle = byAgent ? (info.found ? (info.homeless ? std::string("住民(住居がない！)") : "住民(住宅 #" + std::to_string(info.homeObjectId) + ")") : std::string("住民"))
                             : "住宅 #" + std::to_string(m_selectedBuildingId);
    if (!info.found)
    {
        model.emptyMessage = byAgent ? "この住民の情報が見つかりません。" : "この住宅には、まだ住民がいません。";
        return model;
    }

    auto facilityName = [&](uint32_t id) -> std::string
    {
        const GameObject* obj = GameObjectFactory::FindById(m_objects, id);
        const FacilityDefinition* facility = obj ? FindFacilityDefinition(obj->buildingType) : nullptr;
        return facility ? facility->name : "?";
    };
    auto number = [](float value, int decimals) -> std::string
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.*f", decimals, value);
        return text;
    };
    auto header = [&](const char* label)
    {
        ResidentInfoRow row; row.kind = ResidentInfoRow::Kind::Header; row.label = label; model.rows.push_back(row);
    };
    auto value = [&](const char* label, const std::string& text, float gauge = -1.0f, float marker = -1.0f, bool warning = false)
    {
        ResidentInfoRow row; row.kind = ResidentInfoRow::Kind::Value; row.label = label; row.value = text;
        row.gauge = gauge; row.marker = marker; row.warning = warning; model.rows.push_back(row);
    };
    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    auto normalized = [&](float v, float lo, float hi) { return clamp01((v - lo) / (hi - lo)); };

    // 今の状態。
    std::string stateText = "家にいる";
    switch (info.state)
    {
    case ResidentAgentState::GoingOut:      stateText = "外出中(" + facilityName(info.targetFacilityId) + "へ)"; break;
    case ResidentAgentState::Visiting:      stateText = facilityName(info.targetFacilityId) + "を利用中"; break;
    case ResidentAgentState::ReturningHome: stateText = info.homeless ? "元の家へ戻る途中(家は解体された)" : "帰宅中"; break;
    case ResidentAgentState::Homeless:      stateText = "住居がない！(行動できない)"; break;
    case ResidentAgentState::Stranded:      stateText = "立ち往生(道路が無くなって、帰れない)"; break;
    default: break;
    }
    model.subtitle += " / " + stateText;
    if (info.homeless)
    {
        // 家を失った住民: 一番上に、警告の行を出す(データは、そのまま見られる)。
        ResidentInfoRow row; row.kind = ResidentInfoRow::Kind::Value; row.label = "住まい"; row.value = "住居がない！"; row.warning = true;
        model.rows.push_back(row);
    }

    header("需要(80以上で充足)");
    const bool lifeUnmet = info.lifeNeed < info.needThreshold;
    const bool activityUnmet = info.activityNeed < info.needThreshold;
    value("生活", number(info.lifeNeed, 0) + (lifeUnmet ? "  不足" : "  充足"), clamp01(info.lifeNeed / 100.0f), clamp01(info.needThreshold / 100.0f), lifeUnmet);
    value("アクティビティ", number(info.activityNeed, 0) + (activityUnmet ? "  不足" : "  充足"), clamp01(info.activityNeed / 100.0f), clamp01(info.needThreshold / 100.0f), activityUnmet);
    value("生活の不足が続いた時間", number(info.unmetLifeHours, 1) + " 時間");
    value("アクティビティの不足が続いた時間", number(info.unmetActivityHours, 1) + " 時間");
    value("両方が充足している時間", number(info.satisfiedHours, 1) + " 時間");

    header("満足度");
    value("満足度", number(info.satisfaction, 0) + " / 100", clamp01(info.satisfaction / 100.0f), -1.0f, info.satisfaction < 50.0f);

    header("体力");
    value("体力", number(info.stamina, 0) + " / " + number(info.staminaCap, 0), clamp01(info.stamina / (info.staminaCap > 1.0f ? info.staminaCap : 1.0f)));
    value("ペナルティの段階", std::to_string(info.penaltyLevel) + " / " + std::to_string(info.maxPenaltyLevel),
          info.maxPenaltyLevel > 0 ? clamp01(static_cast<float>(info.penaltyLevel) / static_cast<float>(info.maxPenaltyLevel)) : 0.0f, -1.0f, info.penaltyLevel > 0);

    header("特徴(1が標準)");
    value("品質へのこだわり", number(info.traits.qualityWeight, 2), normalized(info.traits.qualityWeight, 0.6f, 1.5f));
    value("値段への敏感さ", number(info.traits.priceSensitivity, 2), normalized(info.traits.priceSensitivity, 0.5f, 1.6f));
    value("遠出のしやすさ", number(info.traits.distanceTolerance, 2), normalized(info.traits.distanceTolerance, 0.7f, 1.5f));
    value("自発性(用事が無くても出かける)", number(info.traits.spontaneity, 2), normalized(info.traits.spontaneity, 0.5f, 1.6f));
    value("需要の減りやすさ", number(info.traits.needDecayScale, 2), normalized(info.traits.needDecayScale, 0.8f, 1.25f));

    header("行動の記録");
    {
        ResidentInfoRow row; row.kind = ResidentInfoRow::Kind::Note; row.label = "直近の選択";
        row.value = info.lastDecisionNote.empty() ? "まだありません" : info.lastDecisionNote;
        model.rows.push_back(row);
    }
    int shown = 0;
    for (const VisitRecord& record : info.history)
    {
        if (shown >= 3) { break; }
        const int hour = static_cast<int>(record.hour);
        const int minute = static_cast<int>((record.hour - static_cast<float>(hour)) * 60.0f);
        char when[48];
        std::snprintf(when, sizeof(when), "第%d週 %02d:%02d", record.weekIndex, hour, minute);
        value(shown == 0 ? "最近の利用" : "", facilityName(record.facilityObjectId) + "  (" + when + ")");
        ++shown;
    }
    if (shown == 0)
    {
        value("最近の利用", "まだありません");
    }
    return model;
}
void Game::UpdateGameUI()
{
    if (!IsGameUIVisible())
    {
        // タイトル画面などでは、メニュー・住民の情報パネルを開いたままにしない(止めた時間も戻す)。
        m_gameUI.CloseMenu();
        m_gameUI.CloseResidentPanel();
        CancelMoveIn();
        m_selectedBuildingId = 0;
        SyncMenuTimeScale();
        return;
    }

    // Playing以外(Editingなど)では、住民の情報パネルも出さない。
    if (m_context.mode != GameMode::Playing)
    {
        m_gameUI.CloseResidentPanel();
        CancelMoveIn();
        m_selectedBuildingId = 0;
    }

    // Esc・右クリック(メニューを閉じる/配置をやめる/メニューを開く)は、メニューの更新より先に処理する。
    HandleMenuKeys();

    // 新しく建てた住宅で、入居者を選ぶ待ちがあれば、パネルを開く(他のパネルが開いていない時だけ)。
    ProcessMoveInQueue();

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

    // オプションの数字欄への入力(数字キー・テンキー・Backspace・Enter・Esc、右クリックも「やめる」)。
    // 入力欄(ImGui)に文字を入力している間は、ゲームのキーとして扱わない。
    bool imguiTyping = false;
#ifdef ENABLE_EDITOR
    imguiTyping = ImGui::GetIO().WantTextInput;
#endif
    if (!imguiTyping && m_gameUI.IsMenuOpen())
    {
        for (int digit = 0; digit <= 9; ++digit)
        {
            if (m_inputManager.IsKeyPressed('0' + digit) || m_inputManager.IsKeyPressed(VK_NUMPAD0 + digit))
            {
                input.typedDigits.push_back(static_cast<char>('0' + digit));
            }
        }
        input.keyBackspace = m_inputManager.IsKeyPressed(VK_BACK);
        input.keyEnter = m_inputManager.IsKeyPressed(VK_RETURN);
        input.keyEscape = m_inputManager.IsKeyPressed(VK_ESCAPE) || m_inputManager.IsActionPressed(InputAction::Cancel);
    }


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
    if (result.menuTabChanged)
    {
        m_audio.PlaySe("ui_open");      // 左のタブ(建築・情報・研究・システム)を開いた時。中の項目(建物の選択など)では鳴らさない
    }
    // 「入居者を選ぶ」: 住民が選ばれた・「新しい住民を迎える」が押された。
    if (result.moveInChosenResidentId != 0)
    {
        ResolveMoveIn(result.moveInChosenResidentId);
    }
    else if (result.moveInDeclined)
    {
        ResolveMoveIn(0);
    }
    SyncMenuTimeScale();
}

void Game::ProcessMoveInQueue()
{
    if (m_gameUI.IsMoveInOpen() || m_gameUI.IsMenuOpen() || m_gameUI.IsResidentPanelOpen() || m_context.mode != GameMode::Playing)
    {
        return;
    }
    while (m_moveInHouseId == 0 && !m_moveInQueue.empty())
    {
        const uint32_t houseId = m_moveInQueue.front();
        m_moveInQueue.erase(m_moveInQueue.begin());
        const GameObject* house = GameObjectFactory::FindById(m_objects, houseId);
        if (!house || m_demandSystem.CountHomelessResidents() == 0)
        {
            // 住宅が、もう無い(解体された)・住居なしの住民が、もういない: 選ぶ必要が無い。待ちを解く(新しい住民が生まれる)。
            m_demandSystem.ReleaseHouse(houseId);
            continue;
        }
        m_moveInHouseId = houseId;
        m_roadSystem.CancelPlacement();     // 置きかけの道路は、やめる(パネルを閉じた後に、中途半端な状態を残さない)
        m_gameUI.OpenMoveIn();
        m_audio.PlaySe("ui_open");
    }
}

MoveInModel Game::BuildMoveInModel() const
{
    MoveInModel model;
    model.title = "入居する住民を選ぶ";
    model.subtitle = "新しい住宅 #" + std::to_string(m_moveInHouseId) + " に住む人を、家が無い住民から選びます";
    model.declineLabel = "新しい住民を迎える";
    for (const HomelessResidentInfo& info : m_demandSystem.ListHomelessResidents())
    {
        MoveInRow row;
        row.residentId = info.residentId;
        row.title = "住民 #" + std::to_string(info.residentId);
        char text[160];
        std::snprintf(text, sizeof(text), "満足度 %.0f / 体力 %.0f・%.0f / 生活 %.0f・アクティビティ %.0f%s",
            info.satisfaction, info.stamina, info.staminaCap, info.lifeNeed, info.activityNeed, info.needUnmet ? "(不足あり)" : "");
        row.detail = text;
        row.warning = info.needUnmet || info.satisfaction < 50.0f;
        model.rows.push_back(row);
    }
    return model;
}

void Game::ResolveMoveIn(uint32_t residentId)
{
    const uint32_t houseId = m_moveInHouseId;
    m_moveInHouseId = 0;
    m_gameUI.CloseMoveIn();
    if (houseId == 0)
    {
        return;
    }
    if (residentId != 0 && m_demandSystem.MoveInResident(residentId, houseId, m_context))
    {
        m_noticeText = "住民 #" + std::to_string(residentId) + " が、新しい住宅に入居します";
        m_noticeSecondsLeft = 4.0f;
        return; // MoveInResident が、待ちの解除もする
    }
    // 誰も選ばない・入居できなかった: 待ちを解く(新しい住民が、今までどおり生まれる)。
    m_demandSystem.ReleaseHouse(houseId);
}

void Game::CancelMoveIn()
{
    m_gameUI.CloseMoveIn();
    // 今、選んでいた住宅(開いている間は、列から取り出してある)と、列に残っている住宅の待ちを、全て解く。
    // (Reset/Loadでは、DemandSystem側が待ちを全て消すので、二重に解いても害は無い)
    if (m_moveInHouseId != 0)
    {
        m_demandSystem.ReleaseHouse(m_moveInHouseId);
        m_moveInHouseId = 0;
    }
    for (uint32_t houseId : m_moveInQueue)
    {
        m_demandSystem.ReleaseHouse(houseId);
    }
    m_moveInQueue.clear();
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
        const bool showGrid = (m_context.placementTool == PlacementTool::Building || m_context.placementTool == PlacementTool::Road);
#endif
        if (showGrid)
        {
            m_field.DrawGridOverlay(m_debugRenderer, m_orientationRegistry);
        }
        m_propPlacer.Draw(); // Assetsからのドラッグ中のゴースト
        // 追跡している住民のしるし(足元の黄色い輪)。建物の中にいても、位置に出す。
        // (これは、選択・追跡を知らせる、ゲームの表示。当たり判定の箱の表示とは別で、Releaseにも出る)
        if (m_followAgentId != 0)
        {
            if (const GameObject* followed = GameObjectFactory::FindById(m_objects, m_followAgentId))
            {
                AddGroundRing(m_debugRenderer, followed->transform.position, WorldScale::FromLegacy(0.45f), { 1.0f, 0.85f, 0.1f, 1.0f });
            }
        }
#ifdef ENABLE_EDITOR
        // クリックの当たり判定の表示(Debugビルドだけ。Releaseには、この描画のコード自体が入らない)。
        // 建物=水色、住民=ピンク(少し大きい箱)。選択中・追跡中は、上の黄色。F3・Editorのスイッチで、ON/OFFする。
        if (m_showHitBoxes)
        {
            for (const GameObject& obj : m_objects)
            {
                if (!obj.model || (obj.kind != ObjectKind::Building && obj.kind != ObjectKind::Agent))
                {
                    continue;
                }
                const bool isAgent = (obj.kind == ObjectKind::Agent);
                if (isAgent && !obj.visible && obj.id != m_followAgentId)
                {
                    continue; // クリックの対象になる住民だけ(PickBoundsと同じ条件)
                }
                DirectX::XMFLOAT3 boundsMin, boundsMax;
                PickBounds(obj, boundsMin, boundsMax);
                const DirectX::XMFLOAT4 color = isAgent ? DirectX::XMFLOAT4{ 1.0f, 0.25f, 1.0f, 1.0f } : DirectX::XMFLOAT4{ 0.2f, 0.9f, 1.0f, 1.0f };
                m_debugRenderer.AddOBB(boundsMin, boundsMax, obj.transform.GetWorldMatrix(), color);
            }
        }
#endif
        if (m_selectedBuildingId != 0)
        {
            // 選んでいる建物のしるし(足元の黄色い四角)。ゲームの表示で、当たり判定の箱の表示とは別(Releaseにも出る)。
            if (const GameObject* selected = GameObjectFactory::FindById(m_objects, m_selectedBuildingId))
            {
                if (selected->model)
                {
                    AddGroundOutline(m_debugRenderer, *selected, WorldScale::FromLegacy(0.08f), { 1.0f, 0.85f, 0.1f, 1.0f });
                }
            }
        }
#ifdef ENABLE_EDITOR
        m_demandSystem.Draw(m_debugRenderer); // 建物と道路の接続線(デバッグ表示)。エディタが無いビルドでは出さない
#endif

        // 道路は、Editing(Stop)でも出す(置いた道路が、消えたように見えない)。建物の配置の表示・解体の表示は、Playingだけ。
        m_roadSystem.Draw();
        if (m_context.mode == GameMode::Playing)
        {
            m_buildController.Draw(m_editorUi.PanelFlag("Build"));
            m_demolition.Draw();
        }
    }

#ifdef ENABLE_EDITOR
    m_debugEditor.Draw();
#endif

#ifdef ENABLE_EDITOR
    // 人流・需要シミュレーションの統計・プロファイラ・ログはモードによらず常時表示する
    // (Editorメニューのパネル一覧、またはタイトルバーの×で閉じられる)。
    m_demandSystem.DrawDebugUI(m_editorUi.PanelFlag("City Stats"));
    m_residentDebugPanel.Draw(m_demandSystem, m_objects);
    m_simStatsPanel.Draw(m_demandSystem, m_objects);
    m_simReproPanel.Draw(m_demandSystem, m_repro);
    m_gameClock.DrawDebugUI(m_editorUi.PanelFlag("Time"));
    m_gameEconomy.DrawDebugUI(m_editorUi.PanelFlag("Economy"));
    m_assetBrowser.DrawDebugUI();
    m_assetViewer.DrawDebugUI();
    m_audioPanel.DrawDebugUI();
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
    m_audio.Shutdown();
    m_renderer.Finalize();
}
