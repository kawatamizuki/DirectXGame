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
    static constexpr int kMaxGridWidth = 30;
    static constexpr int kMaxGridHeight = 30;

    bool TryGetMouseGridCell(int& gridX, int& gridZ) const;
    void UpdateBuildingPlacement();
    void UpdateCameraForGrid();
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

    std::array<bool, kMaxGridWidth * kMaxGridHeight> m_occupiedCells{};
    int m_gridWidth = 10;
    int m_gridHeight = 10;
    int m_selectedGridX = 5;
    int m_selectedGridZ = 5;
    POINT m_lastMousePosition = { -1, -1 };
};

