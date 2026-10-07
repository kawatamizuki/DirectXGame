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

class Renderer;
class InputManager;
class TimeManager;
class Camera;
class DebugRenderer;
class DemandSystem;
class GameClock;
class GameFlags;
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
    PlacementTool placementTool = PlacementTool::Building;

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

    // ゲームプレイ用UIが画面の上端に占めている高さ(ピクセル。HUDが出ていない時は0)。
    // デバッグ用GUI(ImGui)の固定配置のパネルが、HUDの下から始まるように避けるために使う。
    float hudTopInset = 0.0f;
};
