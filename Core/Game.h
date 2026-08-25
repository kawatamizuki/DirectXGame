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
#include "PlacementDefinition.h"
#include "WorldTime.h"
#include "HudElementLayout.h"
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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
    void UpdatePlacement();
    void UpdateNpc();
    void RebuildNpcPath();
    bool FindFirstRoad(int& gridX, int& gridZ) const;
    int CellIndex(int gridX, int gridZ) const;
    const PlacementDefinition& GetSelectedDefinition() const;
    const Model& GetPlacementModel(const PlacementDefinition& definition) const;
    bool LoadPlacementModels();
    void DrawTimeHud();
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
    PlacementCatalog m_placementCatalog;
    std::unordered_map<std::string, std::unique_ptr<Model>> m_placementModels;
    GameObject m_cubeObject;
    std::vector<GameObject> m_objects;

    std::array<int, kMaxGridWidth * kMaxGridHeight> m_buildingCells{};
    std::array<int, kMaxGridWidth * kMaxGridHeight> m_roadCells{};
    PlacementKind m_placementMode = PlacementKind::Building;
    int m_selectedBuildingDefinition = 0;
    int m_selectedRoadDefinition = 2;
    int m_gridWidth = 10;
    int m_gridHeight = 10;
    int m_selectedGridX = 5;
    int m_selectedGridZ = 5;
    POINT m_lastMousePosition = { -1, -1 };

    bool m_npcActive = false;
    int m_npcGridX = 0;
    int m_npcGridZ = 0;
    DirectX::XMFLOAT3 m_npcWorldPosition = { 0.0f, 0.0f, 0.0f };
    std::vector<int> m_npcPath;
    size_t m_npcPathIndex = 0;
    float m_npcMoveSpeed = 2.0f;

    WorldTime m_worldTime;
    HudElementLayout m_timeHudLayout = {
        "world_time", "", HudAnchor::TopRight,
        { -12.0f, 12.0f }, { 1.0f, 0.0f }, { 180.0f, 48.0f }, 0.75f, true
    };
};

