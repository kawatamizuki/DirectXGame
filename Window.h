#pragma once
#include <windows.h>

class Window
{
public:
    Window();

    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    HWND GetHWND() const;

private:
    HWND m_hwnd;
    const wchar_t* m_className;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
};
