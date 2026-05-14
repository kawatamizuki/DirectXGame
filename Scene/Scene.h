#pragma once



class SceneManager;
struct GameContext;

class Scene
{
public:
    Scene(SceneManager* sceneManager, GameContext* context)
        : m_sceneManager(sceneManager)
        , m_context(context)
    {
    }

    virtual ~Scene() = default;

    virtual void Init() = 0;
    virtual void Update() = 0;
    virtual void Draw() = 0;
    virtual void Finalize() = 0;

protected:
    SceneManager* m_sceneManager;
    GameContext* m_context;
};
