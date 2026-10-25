#pragma once
#include <windows.h>
#include "Renderer.h"
#include"InputManager.h"
#include"SceneManager.h"
#include "Model.h"
#include "DebugEditor.h"
#include "EditorUi.h"
#include"DebugRenderer.h"
#include "GameContext.h"
#include "TimeManager.h"
#include "GameClock.h"
#include "DayNightCycle.h"
#include "UIRenderer.h"
#include "GameFlags.h"
#include "GameEconomy.h"
#include "ScenarioManager.h"
#include "AssetBrowser.h"
#include "AssetViewer.h"
#include "CityCameraController.h"
#include "ModelCache.h"
#include "PropPlacer.h"
#include "GameUI.h"
#include "TitleMenu.h"
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

    // マウスのホイールが回された(Window::SetMouseWheelCallback から呼ぶ)。ズームなどのために、入力に溜める。
    void OnMouseWheel(float notches);

    // エクスプローラーからウィンドウへファイル・フォルダがドロップされた(Window::SetDropFilesCallback から呼ぶ)。
    // ここでは溜めるだけ。コピー(Assets/Incoming への取り込み)は、次のフレームの更新で行う。
    void OnFilesDropped(const std::vector<std::wstring>& paths);
    void RunFrame();
    void Update();
    void Draw();
    void UpdateWindowTitle();
    void Finalize();

private:
    // 街づくり(Playing)のカメラを動かす。ゲームシーンが始まった時は最初の見え方にし、EditingからPlayingへ戻った時は
    // 今の視点を引き継ぐ。入力(WASD・矢印・Shift・ホイール・中ボタンのドラッグ)を集めて、CityCameraControllerへ渡す。
    void UpdateCityCamera();

    // 今、ゲームシーンか(タイトル画面ではないか)。街の更新・描画は、ゲームシーンの間だけ行う。
    bool IsInGameScene() const;

    // 今、タイトル画面か。タイトル画面では、背景に初期マップの街を出し、スタート・終了のメニューを出す。
    bool IsInTitleScene() const;

    // タイトル画面の更新: 始まった瞬間に初期マップを読み込み、カメラを街のまわりでゆっくり回し、スタート・終了のボタンを処理する。
    void UpdateTitle();

    // ゲームプレイ用UI(上部HUD)を出すか。UIの初期化に成功していて、ゲームシーンの間だけtrue。
    bool IsGameUIVisible() const;

    // 今フレームのHUD表示内容(時刻・日数・所持金・街の指標・カーソルの横の建設費)を集める。
    HudState BuildHudState() const;

    // HUDの入力処理(カーソルがHUD上か)。ワールドへの操作より先に呼ぶ。
    void UpdateGameUI();

    // ゲーム内メニュー(右下の「メニュー」)に出す内容(建物・道路の項目と値段・街の情報)を集める。
    MenuModel BuildMenuModel() const;

    // メニューで押された命令(建物・道路を選ぶ・セーブ等)を、実際の処理につなぐ。
    void ApplyMenuCommand(const MenuCommand& command);

    // Esc・右クリックの扱い: メニューを閉じる/配置をやめる/メニューを開く。メニューの更新より先に呼ぶ。
    void HandleMenuKeys();

    // メニューが開いている間は、ゲーム内の時間を止める(閉じたら元の倍率へ戻す)。メニューの更新の後に呼ぶ。
    void SyncMenuTimeScale();

    // F5=クイックセーブ、F9=クイックロード。
    void HandleScenarioHotkeys();

    // EditorUiに渡す、今フレームの画面の情報(HUDの下端・ゲームシーンか・Playingか)。
    EditorUi::FrameInfo BuildEditorFrameInfo() const;

    // ゲームビュー(ドッキングの中央の枠)の位置・大きさを、描画(ビューポート)・マウスの座標・カメラの縦横比へ反映する。
    void ApplyGameViewRect();

    // Assetsパネルのモデルをドラッグしている状態を、ImGuiから読み取って GameContext::assetDrag に書く(毎フレーム、Updateで)。
    void UpdateAssetDragState();

    // 溜めてあるドロップを取り込む(AssetImporter)。取り込んだら、Assetsパネルで選び、結果を見せる。
    void ProcessPendingDrops();

    // Assetsの右クリック「シーンの中央に置く」: ゲームの画面(中央の透過部分)の中心の地面へ置く。
    void PlaceAssetAtViewCenter(const std::string& modelPath);

    // フォルダ(または、その下)のモデルを使っている飾りがシーンにあるか(フォルダの名前の変更・移動を止めるために使う)。
    bool IsFolderUsedByProps(const std::string& folder) const;

    HWND m_hwnd;
    DebugEditor m_debugEditor;//imgui用

    // Editorメニュー・パネルの配置(ドッキング)・ホバーのヒント・ショートカット一覧・エディタ機能のON/OFF。
    EditorUi m_editorUi;

    // 床の格子線を描くか(Editor > エディタ機能のON/OFF に登録している)。
    bool m_showGridOverlay = true;

    // モデルキャッシュ(一度読んだモデルをCache/Modelsに保存して、次回から軽くする)を使うか(同上)。
    bool m_useModelCache = true;
    DebugRenderer m_debugRenderer;//ワイヤーフレームなどデバッグ用の描画
    Renderer m_renderer;
    InputManager m_inputManager;
    SceneManager m_sceneManager;
    TimeManager m_timeManager;

    // ゲーム内時刻(実時間のTimeManagerとは別)と、時刻から光の状態を求める昼夜サイクル。
    GameClock m_gameClock;
    DayNightCycle m_dayNightCycle;

    // ゲームの進行を表すフラグ・マイルストーンの置き場(セーブ対象)。
    GameFlags m_gameFlags;

    // 所持金(建物・道路の建設費の支払い。セーブ対象)。
    GameEconomy m_gameEconomy;

    // 素材(モデル)の一覧とサムネイルを見るパネル(F2で開閉。閉じている間は何もしない)。
    AssetBrowser m_assetBrowser;

    // エクスプローラーからドロップされた、まだ取り込んでいないパス。
    std::vector<std::wstring> m_pendingDrops;

    // 実行中に必要になったモデル(飾りPropなど)の置き場と、Assetsからのドラッグで飾りを置く部品。
    ModelCache m_modelCache;
    PropPlacer m_propPlacer;

    // 選んだモデルを一括で読み込んで、並べて見る別ウィンドウ(重い処理はこちら。閉じている間は何も持たない)。
    AssetViewer m_assetViewer;

    // セーブ/ロード(今の街の状態のスナップショットをファイルに保存・復元する)。
    ScenarioManager m_scenarioManager;

    // ゲームプレイ用UI(独自の2D描画。ImGuiのデバッグパネルとは別物)。
    UIRenderer m_uiRenderer;
    GameUI m_gameUI;
    bool m_uiAvailable = false; // UIRendererの初期化に成功したか
    TitleMenu m_titleMenu;             // タイトル画面のスタート・終了のメニュー
    bool m_wasInTitleScene = false;    // 前のフレームで、タイトル画面だったか(始まった瞬間を見つけるため)
    float m_titleTime = 0.0f;          // タイトル画面が始まってからの時間(秒。カメラを回すのに使う)
    DirectX::XMFLOAT3 m_titleFocus = { 0.0f, 0.0f, 0.0f }; // タイトル画面のカメラが見る点(街の中心)
    float m_titleRadius = 10.0f;       // 街の広がり(中心から一番遠い建物・道路の端までの距離)
    bool m_menuWasOpen = false;        // 前のフレームで、ゲーム内メニューが開いていたか(開閉の瞬間を見つけるため)
    float m_scaleBeforeMenu = 1.0f;    // メニューを開く前の、ゲーム内時間の倍率(閉じたら戻す)
    std::string m_menuStatus;          // メニューのシステムタブに出す、直前のセーブ/ロードの結果

    Camera m_camera;

    // 街づくり(Playing)用のカメラの動かし方(平面の移動+ズーム)。Editingは、DebugEditorのフリーカメラ。
    CityCameraController m_cityCamera;
    bool m_wasInGameScene = false;   // 前のフレームで、ゲームシーンだったか(始まった瞬間を見つけるため)
    bool m_wasPlayingInGame = false; // 前のフレームで、ゲームシーンのPlayingだったか(EditingからPlayingへ戻った瞬間を見つけるため)
    bool m_cameraDragging = false;   // 中ボタンで、地面をつかんでいる最中か
    POINT m_prevCameraCursor = { 0, 0 }; // 前のフレームのカーソルの位置(ゲームビューの左上が原点)

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
