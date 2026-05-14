#pragma once
#include <windows.h>
#include <functional>

class Window
{
public:
    Window();

    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    using ResizeCallback = std::function<void(UINT width, UINT height)>;

    void SetResizeCallback(ResizeCallback callback);
    HWND GetHWND() const;
private:
    HWND m_hwnd;
    const wchar_t* m_className;
  

  
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    ResizeCallback m_resizeCallback;


};
