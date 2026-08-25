#include "Game.h"
#include "Debug.h"
#include <algorithm>
#include <cmath>


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
    m_context.input = &m_inputManager;
    m_context.time = &m_timeManager;
    m_context.objects = &m_objects;
#ifdef ENABLE_EDITOR

    if (!m_debugEditor.Initialize(hwnd, &m_context))
    {
        Debug::Error("Game::Initialize failed : DebugEditor initialize failed");
        return false;
    }
#endif

   

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


    if (!m_cubeModel.LoadFromObj(m_renderer.GetDevice(), "Models/cube.obj"))
    {
        Debug::Error("Game::Initialize failed : Model load failed");
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
    UpdateBuildingPlacement();
  
   
}

void Game::Draw()
{
    m_renderer.BeginFrame();

    m_sceneManager.Draw();

    for (int z = 0; z < kGridHeight; ++z)
    {
        for (int x = 0; x < kGridWidth; ++x)
        {
            const bool selected = x == m_selectedGridX && z == m_selectedGridZ;
            const bool occupied = m_occupiedCells[z * kGridWidth + x];

            Transform tileTransform;
            tileTransform.position = GridToWorld(x, z);
            tileTransform.position.y = -0.08f;
            tileTransform.scale = { 0.94f, 0.08f, 0.94f };

            DirectX::XMFLOAT4 tileColor = occupied
                ? DirectX::XMFLOAT4{ 0.28f, 0.30f, 0.34f, 1.0f }
                : DirectX::XMFLOAT4{ 0.38f, 0.48f, 0.38f, 1.0f };

            if (selected)
            {
                tileColor = occupied
                    ? DirectX::XMFLOAT4{ 0.85f, 0.18f, 0.12f, 1.0f }
                    : DirectX::XMFLOAT4{ 0.20f, 0.85f, 0.30f, 1.0f };
            }

            m_renderer.DrawModel(m_cubeModel, tileTransform, m_camera, tileColor);

            if (occupied)
            {
                Transform buildingTransform;
                buildingTransform.position = GridToWorld(x, z);
                buildingTransform.position.y = 0.75f;
                buildingTransform.scale = { 0.72f, 1.5f, 0.72f };
                m_renderer.DrawModel(
                    m_cubeModel,
                    buildingTransform,
                    m_camera,
                    { 0.72f, 0.52f, 0.28f, 1.0f }
                );
            }
        }
    }

    if (!m_occupiedCells[m_selectedGridZ * kGridWidth + m_selectedGridX])
    {
        Transform previewTransform;
        previewTransform.position = GridToWorld(m_selectedGridX, m_selectedGridZ);
        previewTransform.position.y = 0.5f;
        previewTransform.scale = { 0.68f, 1.0f, 0.68f };
        m_renderer.DrawModel(
            m_cubeModel,
            previewTransform,
            m_camera,
            { 0.25f, 0.75f, 0.95f, 1.0f }
        );
    }
#ifdef ENABLE_EDITOR
    m_debugEditor.BeginFrame();
    m_debugEditor.Draw();
    m_debugEditor.EndFrame();
#endif
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
#ifdef ENABLE_EDITOR
    m_debugEditor.Finalize();
#endif
    m_sceneManager.Finalize();
    m_renderer.Finalize();
}

DirectX::XMFLOAT3 Game::GridToWorld(int gridX, int gridZ) const
{
    return {
        static_cast<float>(gridX) - (static_cast<float>(kGridWidth) - 1.0f) * 0.5f,
        0.0f,
        static_cast<float>(gridZ) - (static_cast<float>(kGridHeight) - 1.0f) * 0.5f
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
    gridX = static_cast<int>(std::floor(worldX + static_cast<float>(kGridWidth) * 0.5f));
    gridZ = static_cast<int>(std::floor(worldZ + static_cast<float>(kGridHeight) * 0.5f));

    return gridX >= 0 && gridX < kGridWidth && gridZ >= 0 && gridZ < kGridHeight;
}

void Game::UpdateBuildingPlacement()
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
        m_selectedGridX = (std::min)(kGridWidth - 1, m_selectedGridX + 1);
    }
    if (m_inputManager.IsGamePadButtonPressed(XINPUT_GAMEPAD_DPAD_UP))
    {
        m_selectedGridZ = (std::min)(kGridHeight - 1, m_selectedGridZ + 1);
    }
    if (m_inputManager.IsGamePadButtonPressed(XINPUT_GAMEPAD_DPAD_DOWN))
    {
        m_selectedGridZ = (std::max)(0, m_selectedGridZ - 1);
    }

    const int selectedIndex = m_selectedGridZ * kGridWidth + m_selectedGridX;
    if (m_inputManager.IsActionPressed(InputAction::Decide) && !m_occupiedCells[selectedIndex])
    {
        m_occupiedCells[selectedIndex] = true;
    }
    if (m_inputManager.IsActionPressed(InputAction::Cancel) && m_occupiedCells[selectedIndex])
    {
        m_occupiedCells[selectedIndex] = false;
    }
}

