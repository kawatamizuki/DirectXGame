#include "Game.h"
#include "Debug.h"

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

    m_renderer.SetVSyncEnabled(false);
    m_timeManager.SetTargetFPS(60);

    m_context.renderer = &m_renderer;
    m_context.input = &m_inputManager;
    m_context.time = &m_timeManager;

    if (!m_debugEditor.Initialize(hwnd, &m_context))
    {
        Debug::Error("Game::Initialize failed : DebugEditor initialize failed");
        return false;
    }

    m_sceneManager.Init(&m_context);
    

    //“§Ž‹“Š‰e
    m_camera.SetPosition(0.0f, 0.0f, -10.0f);
    m_camera.SetTarget(0.0f, 0.0f, 0.0f);
    m_camera.SetProjection(
        //Ž‹–ìŠp
        DirectX::XMConvertToRadians(45.0f),
        800.0f / 600.0f,
        0.1f,
        100.0f
    );


    if (!m_cubeModel.LoadFromObj(m_renderer.GetDevice(), "Models/cube.obj"))
    {
        Debug::Error("Game::Initialize failed : Model load failed");
        return false;
    }

    if (!m_kennyModel.LoadFromObj(m_renderer.GetDevice(), "Models/character-female-f.obj"))
    {
        Debug::Error("Game::Initialize failed : KennyModel load failed");
        return false;
    }

    m_cubeObject.model = &m_cubeModel;
    m_cubeObject.transform.position = { 0.0f, -1.0f, 0.0f };
    m_cubeObject.transform.scale = { 0.5f, 0.5f, 0.5f };
    m_objects.push_back(m_cubeObject);

    m_kennyObject.model = &m_kennyModel;
    m_kennyObject.transform.position = { 0.0f, 0.0f, 0.0f };
    m_kennyObject.transform.scale = { 5.0f, 5.0f, 5.0f };
    m_objects.push_back(m_kennyObject);

   

    return true;
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
    for (auto& obj : m_objects)
    {
        obj.transform.rotation.y += 0.01f;
    }
  
   
}

void Game::Draw()
{
    m_renderer.BeginFrame();

    m_sceneManager.Draw();

    for (auto& obj : m_objects)
    {
        if (obj.model)
        {
            m_renderer.DrawModel(*obj.model, obj.transform, m_camera);
        }
    }

    m_debugEditor.BeginFrame();
    m_debugEditor.Draw();
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
    // FPS•\Ž¦
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