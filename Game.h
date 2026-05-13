#pragma once
#include <windows.h>
#include "Renderer.h"
#include"InputManager.h"
#include"SceneManager.h"
#include "Model.h"
#include "GameObject.h"
#include"Camera.h"

#include "GameContext.h"
#include "TimeManager.h"
class Game
{
public:
    Game();
    ~Game();

    bool Initialize(HWND hwnd);
    void RunFrame();
    void Update();
    void Draw();
    void UpdateWindowTitle();
    void Finalize();

private:
    HWND m_hwnd;
    Renderer m_renderer;
    InputManager m_inputManager;
    SceneManager m_sceneManager;
    TimeManager m_timeManager;
    Camera m_camera;

    GameContext m_context;

    Model m_cubeModel;
    Model m_kennyModel;
    GameObject m_cubeObject;
    GameObject m_kennyObject;
    std::vector<GameObject> m_objects;
};