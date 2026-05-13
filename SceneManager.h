#pragma once
#include <memory>
#include "SceneName.h"
#include"GameContext.h"

class Scene;
class Renderer;

class SceneManager
{
public:
    SceneManager();
    ~SceneManager();

    void Init(GameContext* context);
    void Update();
    void Draw();
    void Finalize();

    void ChangeScene(SceneName nextScene);

    GameContext* GetContext() const;

private:
    std::unique_ptr<Scene> CreateScene(SceneName sceneName);

private:
    std::unique_ptr<Scene> m_currentScene;
    GameContext* m_context;
};
