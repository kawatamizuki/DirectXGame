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

    // 今動いているシーンの名前(ゲームプレイ用UIをゲームシーンの時だけ出すために使う)。
    SceneName GetCurrentSceneName() const { return m_currentSceneName; }

private:
    std::unique_ptr<Scene> CreateScene(SceneName sceneName);

private:
    std::unique_ptr<Scene> m_currentScene;
    SceneName m_currentSceneName = SceneName::Title;
    GameContext* m_context;
};
