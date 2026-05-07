#include "Game.h"
#include "Debug.h"

Game::Game()
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
    m_cubeObject.transform.position = { 0.0f, 0.0f, 0.0f };
    m_cubeObject.transform.scale = { 0.5f, 0.5f, 0.5f };
    //m_objects.push_back(m_cubeObject);

    m_kennyObject.model = &m_kennyModel;
    m_kennyObject.transform.position = { 0.0f, 0.0f, 0.0f };
    m_kennyObject.transform.scale = { 5.0f, 5.0f, 5.0f };
    m_objects.push_back(m_kennyObject);

   

    return true;
}

void Game::Update()
{
    for (auto& obj : m_objects)
    {
        obj.transform.rotation.y += 0.01f;
    }
}

void Game::Draw()
{
    m_renderer.BeginFrame();

    for (auto& obj : m_objects)
    {
        if (obj.model)
        {
            m_renderer.DrawModel(*obj.model, obj.transform);
        }
    }

    m_renderer.EndFrame();
}

void Game::Finalize()
{
    m_renderer.Finalize();
}