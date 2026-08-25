#include "Game.h"
#include "Debug.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include "imgui.h"


Game::Game()
    : m_hwnd(nullptr)
{
    m_buildingCells.fill(-1);
    m_roadCells.fill(-1);
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
    m_context.input = &m_inputManager;
    m_context.time = &m_timeManager;
    m_context.objects = &m_objects;
    if (!m_debugEditor.Initialize(hwnd, &m_context))
    {
        Debug::Error("Game::Initialize failed : DebugEditor initialize failed");
        return false;
    }
    m_sceneManager.Init(&m_context);
    

    //透視投影
    m_camera.SetPosition(10.0f, 12.0f, -12.0f);
    m_camera.SetTarget(0.0f, 0.0f, 0.0f);
    m_camera.SetProjection(
        //視野角
        DirectX::XMConvertToRadians(45.0f),
        static_cast<float>(m_renderer.GetWindowWidth()) /
        static_cast<float>(m_renderer.GetWindowHeight()),
        0.1f,
        100.0f
    );
    UpdateCameraForGrid();


    if (!m_cubeModel.LoadFromObj(m_renderer.GetDevice(), "Models/cube.obj"))
    {
        Debug::Error("Game::Initialize failed : Model load failed");
        return false;
    }

    if (!LoadPlacementModels())
    {
        Debug::Error("Game::Initialize failed : Placement model load failed");
        return false;
    }

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
   
    m_inputManager.Update();
    m_sceneManager.Update();
    m_worldTime.Update(m_timeManager.GetDeltaTime());
    UpdatePlacement();
    UpdateNpc();
  
   
}

void Game::Draw()
{
    m_renderer.BeginFrame();
    m_renderer.SetWorldTime(m_worldTime.GetTimeOfDay01(), m_worldTime.GetDaylight01());

    m_sceneManager.Draw();

    for (int z = 0; z < m_gridHeight; ++z)
    {
        for (int x = 0; x < m_gridWidth; ++x)
        {
            const bool selected = x == m_selectedGridX && z == m_selectedGridZ;
            const int cellIndex = CellIndex(x, z);
            const bool occupied = m_buildingCells[cellIndex] >= 0;
            const bool road = m_roadCells[cellIndex] >= 0;

            Transform tileTransform;
            tileTransform.position = GridToWorld(x, z);
            tileTransform.position.y = -0.08f;
            tileTransform.scale = { 0.94f, 0.08f, 0.94f };

            DirectX::XMFLOAT4 tileColor = { 0.38f, 0.48f, 0.38f, 1.0f };

            if (selected)
            {
                const bool blocked = m_placementMode == PlacementKind::Building
                    ? occupied || road
                    : occupied;
                tileColor = blocked
                    ? DirectX::XMFLOAT4{ 0.85f, 0.18f, 0.12f, 1.0f }
                    : DirectX::XMFLOAT4{ 0.20f, 0.85f, 0.30f, 1.0f };
            }

            m_renderer.DrawModel(m_cubeModel, tileTransform, m_camera, tileColor);

            if (road)
            {
                const PlacementDefinition& definition = m_placementCatalog.Get(m_roadCells[cellIndex]);
                Transform roadTransform;
                roadTransform.position = GridToWorld(x, z);
                roadTransform.position.y = definition.heightOffset;
                roadTransform.scale = definition.scale;
                m_renderer.DrawModel(
                    GetPlacementModel(definition),
                    roadTransform,
                    m_camera,
                    definition.tint
                );
            }

            if (occupied)
            {
                const PlacementDefinition& definition = m_placementCatalog.Get(m_buildingCells[cellIndex]);
                Transform buildingTransform;
                buildingTransform.position = GridToWorld(x, z);
                buildingTransform.position.y = definition.heightOffset;
                buildingTransform.scale = definition.scale;
                m_renderer.DrawModel(
                    GetPlacementModel(definition),
                    buildingTransform,
                    m_camera,
                    definition.tint
                );
            }
        }
    }

    const int selectedIndex = CellIndex(m_selectedGridX, m_selectedGridZ);
    if (m_placementMode == PlacementKind::Building &&
        m_buildingCells[selectedIndex] < 0 && m_roadCells[selectedIndex] < 0)
    {
        const PlacementDefinition& definition = GetSelectedDefinition();
        Transform previewTransform;
        previewTransform.position = GridToWorld(m_selectedGridX, m_selectedGridZ);
        previewTransform.position.y = definition.heightOffset;
        previewTransform.scale = definition.scale;
        m_renderer.DrawModel(
            GetPlacementModel(definition),
            previewTransform,
            m_camera,
            definition.previewTint
        );
    }
    else if (m_placementMode == PlacementKind::Road &&
        m_buildingCells[selectedIndex] < 0 && m_roadCells[selectedIndex] < 0)
    {
        const PlacementDefinition& definition = GetSelectedDefinition();
        Transform roadPreviewTransform;
        roadPreviewTransform.position = GridToWorld(m_selectedGridX, m_selectedGridZ);
        roadPreviewTransform.position.y = definition.heightOffset + 0.02f;
        roadPreviewTransform.scale = definition.scale;
        m_renderer.DrawModel(
            GetPlacementModel(definition),
            roadPreviewTransform,
            m_camera,
            definition.previewTint
        );
    }

    if (m_npcActive)
    {
        Transform npcTransform;
        npcTransform.position = m_npcWorldPosition;
        npcTransform.position.y = 0.38f;
        npcTransform.scale = { 0.32f, 0.72f, 0.32f };
        m_renderer.DrawModel(
            m_cubeModel,
            npcTransform,
            m_camera,
            { 0.95f, 0.72f, 0.24f, 1.0f }
        );
    }
    m_debugEditor.BeginFrame();
#ifdef ENABLE_EDITOR
    m_debugEditor.Draw();
#endif

    ImGui::Begin("Grid Settings");
    bool gridSizeChanged = false;
    gridSizeChanged |= ImGui::SliderInt("Width", &m_gridWidth, 1, kMaxGridWidth);
    gridSizeChanged |= ImGui::SliderInt("Depth", &m_gridHeight, 1, kMaxGridHeight);
    ImGui::Text("Current size: %d x %d", m_gridWidth, m_gridHeight);
    ImGui::Separator();
    ImGui::TextUnformatted("Placement Mode");
    if (ImGui::RadioButton("Building", m_placementMode == PlacementKind::Building))
    {
        m_placementMode = PlacementKind::Building;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Road", m_placementMode == PlacementKind::Road))
    {
        m_placementMode = PlacementKind::Road;
    }
    int& selectedDefinition = m_placementMode == PlacementKind::Building
        ? m_selectedBuildingDefinition
        : m_selectedRoadDefinition;
    const auto availableDefinitions = m_placementCatalog.GetIndices(m_placementMode);
    const char* selectedName = m_placementCatalog.Get(selectedDefinition).displayName.c_str();
    if (ImGui::BeginCombo("Type", selectedName))
    {
        for (const int definitionIndex : availableDefinitions)
        {
            const PlacementDefinition& item = m_placementCatalog.Get(definitionIndex);
            const bool isSelected = definitionIndex == selectedDefinition;
            if (ImGui::Selectable(item.displayName.c_str(), isSelected))
            {
                selectedDefinition = definitionIndex;
            }
            if (isSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    const PlacementDefinition& selectedItem = m_placementCatalog.Get(selectedDefinition);
    ImGui::Text("Model: %s", selectedItem.modelPath.c_str());
    ImGui::Text("Texture: %s", selectedItem.texturePath.empty()
        ? "From model material" : selectedItem.texturePath.c_str());
    ImGui::SliderFloat("NPC Speed", &m_npcMoveSpeed, 0.2f, 6.0f, "%.1f");
    ImGui::Separator();
    ImGui::TextUnformatted("World Time");
    float hour = static_cast<float>(m_worldTime.GetHour()) +
        static_cast<float>(m_worldTime.GetMinute()) / 60.0f;
    if (ImGui::SliderFloat("Hour", &hour, 0.0f, 23.99f, "%05.2f"))
    {
        m_worldTime.SetHour(hour);
    }
    float timeScale = m_worldTime.GetMinutesPerRealSecond();
    if (ImGui::SliderFloat("Minutes / real second", &timeScale, 0.0f, 120.0f, "%.1f"))
    {
        m_worldTime.SetMinutesPerRealSecond(timeScale);
    }
    bool paused = m_worldTime.IsPaused();
    if (ImGui::Checkbox("Pause time", &paused))
    {
        m_worldTime.SetPaused(paused);
    }
    ImGui::DragFloat2("Time HUD offset", &m_timeHudLayout.offset.x, 1.0f);
    ImGui::SliderFloat("Time HUD opacity", &m_timeHudLayout.opacity, 0.0f, 1.0f);
    ImGui::TextUnformatted("Left click / A: Place");
    ImGui::TextUnformatted("Right click / B: Remove");
    ImGui::End();

    if (gridSizeChanged)
    {
        m_selectedGridX = (std::min)(m_selectedGridX, m_gridWidth - 1);
        m_selectedGridZ = (std::min)(m_selectedGridZ, m_gridHeight - 1);
        m_npcPath.clear();
        m_npcPathIndex = 0;
        if (m_npcActive && m_npcGridX < m_gridWidth && m_npcGridZ < m_gridHeight)
        {
            m_npcWorldPosition = GridToWorld(m_npcGridX, m_npcGridZ);
        }
        UpdateCameraForGrid();
    }
    DrawTimeHud();
    m_debugEditor.EndFrame();
    if (!m_renderer.IsVSyncEnabled())
    {
        m_timeManager.WaitForTargetFPS();
    }

    m_renderer.EndFrame();

    m_timeManager.Update();

  
   
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
    m_debugEditor.Finalize();
    m_sceneManager.Finalize();
    m_renderer.Finalize();
}

DirectX::XMFLOAT3 Game::GridToWorld(int gridX, int gridZ) const
{
    return {
        static_cast<float>(gridX) - (static_cast<float>(m_gridWidth) - 1.0f) * 0.5f,
        0.0f,
        static_cast<float>(gridZ) - (static_cast<float>(m_gridHeight) - 1.0f) * 0.5f
    };
}

bool Game::TryGetMouseGridCell(int& gridX, int& gridZ) const
{
    using namespace DirectX;

    const UINT width = m_renderer.GetWindowWidth();
    const UINT height = m_renderer.GetWindowHeight();
    if (width == 0 || height == 0)
    {
        return false;
    }

    const POINT mouse = m_inputManager.GetMousePosition();
    if (mouse.x < 0 || mouse.y < 0 ||
        mouse.x >= static_cast<LONG>(width) || mouse.y >= static_cast<LONG>(height))
    {
        return false;
    }

    const XMMATRIX view = m_camera.GetViewMatrix();
    const XMMATRIX projection = m_camera.GetProjectionMatrix();
    const XMMATRIX world = XMMatrixIdentity();

    const XMVECTOR nearPoint = XMVector3Unproject(
        XMVectorSet(static_cast<float>(mouse.x), static_cast<float>(mouse.y), 0.0f, 1.0f),
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
        0.0f, 1.0f, projection, view, world
    );
    const XMVECTOR farPoint = XMVector3Unproject(
        XMVectorSet(static_cast<float>(mouse.x), static_cast<float>(mouse.y), 1.0f, 1.0f),
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
        0.0f, 1.0f, projection, view, world
    );

    XMFLOAT3 rayStart{};
    XMFLOAT3 rayDirection{};
    XMStoreFloat3(&rayStart, nearPoint);
    XMStoreFloat3(&rayDirection, XMVectorSubtract(farPoint, nearPoint));

    if (std::abs(rayDirection.y) < 0.0001f)
    {
        return false;
    }

    const float distance = -rayStart.y / rayDirection.y;
    if (distance < 0.0f)
    {
        return false;
    }

    const float worldX = rayStart.x + rayDirection.x * distance;
    const float worldZ = rayStart.z + rayDirection.z * distance;
    gridX = static_cast<int>(std::floor(worldX + static_cast<float>(m_gridWidth) * 0.5f));
    gridZ = static_cast<int>(std::floor(worldZ + static_cast<float>(m_gridHeight) * 0.5f));

    return gridX >= 0 && gridX < m_gridWidth && gridZ >= 0 && gridZ < m_gridHeight;
}

void Game::UpdatePlacement()
{
    const POINT mousePosition = m_inputManager.GetMousePosition();
    const bool mouseMoved =
        mousePosition.x != m_lastMousePosition.x ||
        mousePosition.y != m_lastMousePosition.y;

    if (mouseMoved)
    {
        int mouseGridX = 0;
        int mouseGridZ = 0;
        if (TryGetMouseGridCell(mouseGridX, mouseGridZ))
        {
            m_selectedGridX = mouseGridX;
            m_selectedGridZ = mouseGridZ;
        }
        m_lastMousePosition = mousePosition;
    }

    if (m_inputManager.IsGamePadButtonPressed(XINPUT_GAMEPAD_DPAD_LEFT))
    {
        m_selectedGridX = (std::max)(0, m_selectedGridX - 1);
    }
    if (m_inputManager.IsGamePadButtonPressed(XINPUT_GAMEPAD_DPAD_RIGHT))
    {
        m_selectedGridX = (std::min)(m_gridWidth - 1, m_selectedGridX + 1);
    }
    if (m_inputManager.IsGamePadButtonPressed(XINPUT_GAMEPAD_DPAD_UP))
    {
        m_selectedGridZ = (std::min)(m_gridHeight - 1, m_selectedGridZ + 1);
    }
    if (m_inputManager.IsGamePadButtonPressed(XINPUT_GAMEPAD_DPAD_DOWN))
    {
        m_selectedGridZ = (std::max)(0, m_selectedGridZ - 1);
    }

    if (ImGui::GetIO().WantCaptureMouse &&
        (m_inputManager.IsKeyPressed(KeyCode::MouseLeft) ||
         m_inputManager.IsKeyPressed(KeyCode::MouseRight)))
    {
        return;
    }

    const int selectedIndex = CellIndex(m_selectedGridX, m_selectedGridZ);
    if (m_inputManager.IsActionPressed(InputAction::Decide))
    {
        if (m_placementMode == PlacementKind::Building &&
            m_buildingCells[selectedIndex] < 0 && m_roadCells[selectedIndex] < 0)
        {
            m_buildingCells[selectedIndex] = m_selectedBuildingDefinition;
        }
        else if (m_placementMode == PlacementKind::Road &&
            m_buildingCells[selectedIndex] < 0 && m_roadCells[selectedIndex] < 0)
        {
            m_roadCells[selectedIndex] = m_selectedRoadDefinition;
            m_npcPath.clear();
            m_npcPathIndex = 0;
        }
    }

    if (m_inputManager.IsActionPressed(InputAction::Cancel))
    {
        if (m_placementMode == PlacementKind::Building)
        {
            m_buildingCells[selectedIndex] = -1;
        }
        else if (m_placementMode == PlacementKind::Road)
        {
            m_roadCells[selectedIndex] = -1;
            m_npcPath.clear();
            m_npcPathIndex = 0;
        }
    }
}

int Game::CellIndex(int gridX, int gridZ) const
{
    return gridZ * kMaxGridWidth + gridX;
}

const PlacementDefinition& Game::GetSelectedDefinition() const
{
    const int index = m_placementMode == PlacementKind::Building
        ? m_selectedBuildingDefinition
        : m_selectedRoadDefinition;
    return m_placementCatalog.Get(index);
}

const Model& Game::GetPlacementModel(const PlacementDefinition& definition) const
{
    return *m_placementModels.at(definition.id);
}

bool Game::LoadPlacementModels()
{
    for (const PlacementDefinition& definition : m_placementCatalog.GetAll())
    {
        if (m_placementModels.find(definition.id) != m_placementModels.end())
        {
            continue;
        }

        auto model = std::make_unique<Model>();
        if (!model->LoadFromObj(m_renderer.GetDevice(), definition.modelPath))
        {
            Debug::Error("Failed to load placement model: " + definition.modelPath);
            return false;
        }
        if (!definition.texturePath.empty() &&
            !model->LoadTextureOverride(m_renderer.GetDevice(), definition.texturePath))
        {
            Debug::Error("Failed to load placement texture: " + definition.texturePath);
            return false;
        }
        m_placementModels.emplace(definition.id, std::move(model));
    }
    return true;
}

void Game::DrawTimeHud()
{
    if (!m_timeHudLayout.visible)
    {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 anchor = viewport->WorkPos;
    switch (m_timeHudLayout.anchor)
    {
    case HudAnchor::TopRight:
        anchor.x += viewport->WorkSize.x;
        break;
    case HudAnchor::BottomLeft:
        anchor.y += viewport->WorkSize.y;
        break;
    case HudAnchor::BottomRight:
        anchor.x += viewport->WorkSize.x;
        anchor.y += viewport->WorkSize.y;
        break;
    case HudAnchor::Center:
        anchor.x += viewport->WorkSize.x * 0.5f;
        anchor.y += viewport->WorkSize.y * 0.5f;
        break;
    case HudAnchor::TopLeft:
    default:
        break;
    }

    anchor.x += m_timeHudLayout.offset.x;
    anchor.y += m_timeHudLayout.offset.y;
    ImGui::SetNextWindowPos(
        anchor,
        ImGuiCond_Always,
        { m_timeHudLayout.pivot.x, m_timeHudLayout.pivot.y });
    ImGui::SetNextWindowSize(
        { m_timeHudLayout.size.x, m_timeHudLayout.size.y },
        ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(m_timeHudLayout.opacity);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;

    ImGui::Begin(m_timeHudLayout.id.c_str(), nullptr, flags);
    ImGui::Text("Day %d", m_worldTime.GetDay());
    ImGui::SameLine();
    ImGui::Text("%02d:%02d", m_worldTime.GetHour(), m_worldTime.GetMinute());
    ImGui::End();
}

bool Game::FindFirstRoad(int& gridX, int& gridZ) const
{
    for (int z = 0; z < m_gridHeight; ++z)
    {
        for (int x = 0; x < m_gridWidth; ++x)
        {
            if (m_roadCells[CellIndex(x, z)] >= 0)
            {
                gridX = x;
                gridZ = z;
                return true;
            }
        }
    }
    return false;
}

void Game::RebuildNpcPath()
{
    m_npcPath.clear();
    m_npcPathIndex = 0;

    if (!m_npcActive || m_roadCells[CellIndex(m_npcGridX, m_npcGridZ)] < 0)
    {
        return;
    }

    constexpr int kCellCount = kMaxGridWidth * kMaxGridHeight;
    std::array<int, kCellCount> previous{};
    std::array<int, kCellCount> distance{};
    previous.fill(-1);
    distance.fill(-1);

    const int start = CellIndex(m_npcGridX, m_npcGridZ);
    std::queue<int> open;
    open.push(start);
    distance[start] = 0;
    int destination = start;

    constexpr int directions[4][2] =
    {
        { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }
    };

    while (!open.empty())
    {
        const int current = open.front();
        open.pop();
        const int currentX = current % kMaxGridWidth;
        const int currentZ = current / kMaxGridWidth;

        if (distance[current] > distance[destination])
        {
            destination = current;
        }

        for (const auto& direction : directions)
        {
            const int nextX = currentX + direction[0];
            const int nextZ = currentZ + direction[1];
            if (nextX < 0 || nextX >= m_gridWidth || nextZ < 0 || nextZ >= m_gridHeight)
            {
                continue;
            }

            const int next = CellIndex(nextX, nextZ);
            if (m_roadCells[next] < 0 || distance[next] >= 0)
            {
                continue;
            }

            previous[next] = current;
            distance[next] = distance[current] + 1;
            open.push(next);
        }
    }

    if (destination == start)
    {
        return;
    }

    std::vector<int> reversedPath;
    for (int cell = destination; cell != start; cell = previous[cell])
    {
        reversedPath.push_back(cell);
    }
    m_npcPath.assign(reversedPath.rbegin(), reversedPath.rend());
}

void Game::UpdateNpc()
{
    if (!m_npcActive ||
        m_npcGridX < 0 || m_npcGridX >= m_gridWidth ||
        m_npcGridZ < 0 || m_npcGridZ >= m_gridHeight ||
        m_roadCells[CellIndex(m_npcGridX, m_npcGridZ)] < 0)
    {
        int roadX = 0;
        int roadZ = 0;
        if (!FindFirstRoad(roadX, roadZ))
        {
            m_npcActive = false;
            m_npcPath.clear();
            return;
        }

        m_npcActive = true;
        m_npcGridX = roadX;
        m_npcGridZ = roadZ;
        m_npcWorldPosition = GridToWorld(roadX, roadZ);
        RebuildNpcPath();
    }

    if (m_npcPathIndex >= m_npcPath.size())
    {
        RebuildNpcPath();
        if (m_npcPath.empty())
        {
            return;
        }
    }

    const int nextCell = m_npcPath[m_npcPathIndex];
    if (m_roadCells[nextCell] < 0)
    {
        RebuildNpcPath();
        return;
    }

    const int nextX = nextCell % kMaxGridWidth;
    const int nextZ = nextCell / kMaxGridWidth;
    const DirectX::XMFLOAT3 target = GridToWorld(nextX, nextZ);
    const float deltaX = target.x - m_npcWorldPosition.x;
    const float deltaZ = target.z - m_npcWorldPosition.z;
    const float distance = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
    const float step = m_npcMoveSpeed * m_timeManager.GetDeltaTime();

    if (distance <= step || distance < 0.001f)
    {
        m_npcWorldPosition = target;
        m_npcGridX = nextX;
        m_npcGridZ = nextZ;
        ++m_npcPathIndex;
    }
    else
    {
        m_npcWorldPosition.x += deltaX / distance * step;
        m_npcWorldPosition.z += deltaZ / distance * step;
    }
}

void Game::UpdateCameraForGrid()
{
    const float size = static_cast<float>((std::max)(m_gridWidth, m_gridHeight));
    m_camera.SetPosition(size, size * 1.2f, -size * 1.2f);
    m_camera.SetTarget(0.0f, 0.0f, 0.0f);
}

