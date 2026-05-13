#pragma once
#include <windows.h>
#include <d3d11.h>

class DebugEditor
{
public:
    bool Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
    void BeginFrame();
    void Draw();
    void EndFrame();
    void Finalize();
};