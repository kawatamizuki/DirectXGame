#pragma once
#include<vector>
//========================================
// GameContext
//========================================
//
// Game全体で共有するシステムへの参照をまとめる構造体。
//
// Scene や SceneManager へ:
//
// - Renderer
// - InputManager
// - AudioManager
//
// などを個別引数で大量に渡さないために使用する。
//
// 目的:
//
// - 引数爆発防止
// - 依存関係の整理
// - Gameが各Managerを所有し、
//   必要なシステムだけをSceneへ共有する
//
// 将来的には:
//
// - AudioManager
// - TextureManager
// - PhysicsManager
//
// なども追加予定。
//
// Game
//  ├ Renderer
//  ├ InputManager
//  ├ AudioManager
//  └ GameContext
//       ↓
//   SceneManager
//       ↓
//      Scene
//
//========================================
#include"GameObject.h"//Vector型のコンテナが実際のサイズを要求するため
#include"GameMode.h"
#include"PlacementTool.h"
#include"RoadType.h"
#include"RoadDrawMode.h"
#include"CostPreview.h"
#include"AssetDragState.h"

class Renderer;
class ModelCache;
class InputManager;
class TimeManager;
class Camera;
class DebugRenderer;
class DemandSystem;
class GameClock;
class GameFlags;
class GameEconomy;
class ScenarioManager;

struct GameContext
{
    Renderer* renderer = nullptr;
    DebugRenderer* debugRenderer = nullptr;
    InputManager* input = nullptr;
    TimeManager* time=nullptr;

    // ゲーム内の時刻(昼夜・日数)。実時間のTimeManagerとは別。住民の1日の行動など、
    // 時刻に従って動くシステムが共有して参照する。
    GameClock* clock = nullptr;

    // ゲームの進行を表す名前付きの値(フラグ・マイルストーン・解放状態など)。
    GameFlags* flags = nullptr;

    // 所持金。建物・道路の建設費は、ここから払う(足りなければ置けない)。
    GameEconomy* economy = nullptr;

    // セーブ/ロード。ゲームシーンの開始時に初期マップを読み込むために、シーンからも使う。
    ScenarioManager* scenario = nullptr;
    Camera* camera = nullptr;
    std::vector<GameObject>* objects=nullptr;

    // Inspectorから代表住民の状態を問い合わせるために使う
    DemandSystem* demandSystem = nullptr;

    // Editing(オブジェクト選択・ギズモ操作) / Playing(配置などプレイヤー操作)
    GameMode mode = GameMode::Playing;

    // Playing中に「今何を配置しようとしているか」。
    // BuildController/RoadSystemが同じ左クリックを取り合わないよう、この値で処理を振り分ける。
    // 初期値はNone: ゲーム内メニュー(右下の「メニュー」→建築)で選ぶまでは、クリックしても何も置かれない。
    PlacementTool placementTool = PlacementTool::None;

    // Roadツール選択中に「今から配置する道路の種類」。BuildパネルのUIから設定される。
    RoadType roadType = RoadType::Normal;

    // Roadツール選択中に「今から道路をどう引くか」(直線/曲線)。BuildパネルのUIから設定される。
    RoadDrawMode roadDrawMode = RoadDrawMode::Straight;

    // 毎フレーム先頭でリセットされる。
    // UIやEditorが今フレームのクリックを処理したらtrueにすることで、
    // 同じフレームの同じクリックが別なシステムに二重処理されるのを防ぐ。
    bool inputConsumed = false;

    // 毎フレーム先頭でリセットされる。ゲームプレイ用UI(上部HUDなど。ImGuiではない独自描画)の
    // 上にカーソルがある時にtrue。ImGuiのWantCaptureMouseと同じ役割で、UIの上のクリックや
    // ホバーが、建物・道路の配置やオブジェクト選択に抜けないようにするために各システムが見る。
    bool mouseOverUI = false;

    // ゲームプレイ用UIが、ゲームビューの上端に占めている高さ(ピクセル。HUDが出ていない時は0)。
    // HUDはゲームビューの中に描くようになったので、エディタのパネルの配置には使わない(情報として残している)。
    float hudTopInset = 0.0f;

    // 毎フレーム先頭でリセットされる。建物・道路の配置が、今カーソルの位置で建設した場合の費用を書き込み、
    // HUDがカーソルの横に表示する。
    CostPreview costPreview;

    // 実行中に必要になったモデル(飾りPropなど)をパスで持つ置き場。Propが Model* を持ち続けても住所が変わらない。
    ModelCache* modelCache = nullptr;

    // Assetsパネルからモデルをゲームの画面へドラッグしている状態(AssetBrowserが書き、PropPlacerが読む)。
    AssetDragState assetDrag;
};
