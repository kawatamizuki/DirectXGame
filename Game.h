#pragma once
#include <windows.h>
#include "Renderer.h"
#include "Model.h"
#include "GameObject.h"

class Game
{
public:
    Game();
    ~Game();

    bool Initialize(HWND hwnd);
    void Update();
    void Draw();
    void Finalize();

private:
    Renderer m_renderer;

    Model m_cubeModel;
    Model m_kennyModel;
    GameObject m_cubeObject;
    GameObject m_kennyObject;
    std::vector<GameObject> m_objects;
};