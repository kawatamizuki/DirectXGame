#pragma once
#include <windows.h>
#include <d3d11.h>

#include"GameContext.h"

class DebugEditor
{
public:
    DebugEditor();
    ~DebugEditor();
    bool Initialize(HWND hwnd, GameContext* context);
    void BeginFrame();
    void Draw();

    void DrawPerformance();
    void DrawObjects();
    void DrawInspector();

    void EndFrame();
    void Finalize();
private:
    GameContext* m_context = nullptr;

    int m_selectedObjectIndex;
};