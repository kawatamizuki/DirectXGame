#pragma once
#include <windows.h>
#include <d3d11.h>

#include"GameContext.h"

class DebugEditor
{
public:
    bool Initialize(HWND hwnd, GameContext* context);
    void BeginFrame();
    void Draw();
    void EndFrame();
    void Finalize();
private:
    GameContext* m_context = nullptr;
};