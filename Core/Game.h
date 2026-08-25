#pragma once
#include <windows.h>
#include "Renderer.h"
#include"InputManager.h"
#include"SceneManager.h"
#include "Model.h"
#include "DebugEditor.h"
#include "GameContext.h"
#include "TimeManager.h"
#include "GameObject.h"
#include"Camera.h"
#include <array>

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
    static constexpr int kGridWidth = 10;
    static constexpr int kGridHeight = 10;

    bool TryGetMouseGridCell(int& gridX, int& gridZ) const;
    void UpdateBuildingPlacement();
    DirectX::XMFLOAT3 GridToWorld(int gridX, int gridZ) const;

    HWND m_hwnd;
    DebugEditor m_debugEditor;//imgui用
    Renderer m_renderer;
    InputManager m_inputManager;
    SceneManager m_sceneManager;
    TimeManager m_timeManager;
    Camera m_camera;

    GameContext m_context;

    Model m_cubeModel;
    GameObject m_cubeObject;
    std::vector<GameObject> m_objects;

    std::array<bool, kGridWidth * kGridHeight> m_occupiedCells{};
    int m_selectedGridX = kGridWidth / 2;
    int m_selectedGridZ = kGridHeight / 2;
    POINT m_lastMousePosition = { -1, -1 };
};

