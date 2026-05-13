#pragma once

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


class Renderer;
class InputManager;
class TimeManager;

struct GameContext
{
    Renderer* renderer = nullptr;
    InputManager* input = nullptr;
    TimeManager* time=nullptr;

}; 
