#include "SceneManager.h"
#include "Scene.h"
#include "Renderer.h"
#include "Debug.h"

 
 #include "TitleScene.h"
 #include "GameScene.h"

SceneManager::SceneManager()
    : m_currentScene(nullptr)
    , m_context(nullptr)
{
}

SceneManager::~SceneManager()
{
    Finalize();
}

void SceneManager::Init(GameContext* context)
{
    m_context = context;

    ChangeScene(SceneName::Title);
}

void SceneManager::Update()
{
    if (m_currentScene)
    {
        m_currentScene->Update();
    }
}

void SceneManager::Draw()
{
    if (m_currentScene)
    {
        m_currentScene->Draw();
    }
}

void SceneManager::Finalize()
{
    if (m_currentScene)
    {
        m_currentScene->Finalize();
        m_currentScene.reset();
    }

    m_context = nullptr;
}

void SceneManager::ChangeScene(SceneName nextScene)
{
    if (m_currentScene)
    {
        m_currentScene->Finalize();
        m_currentScene.reset();
    }

    m_currentScene = CreateScene(nextScene);

    if (m_currentScene)
    {
        m_currentScene->Init();
    }
}

GameContext* SceneManager::GetContext() const
{
    return m_context;
}

std::unique_ptr<Scene> SceneManager::CreateScene(SceneName sceneName)
{
    switch (sceneName)
    {
    case SceneName::Title:
        Debug::Log("Create TitleScene");
        return std::make_unique<TitleScene>(this, m_context);
        break;

    case SceneName::Game:
        Debug::Log("Create GameScene");
        return std::make_unique<GameScene>(this, m_context);
        break;

    case SceneName::Clear:
        Debug::Log("Create ClearScene");
        break;

    case SceneName::GameOver:
        Debug::Log("Create GameOverScene");
        break;
    }

    return nullptr;
}