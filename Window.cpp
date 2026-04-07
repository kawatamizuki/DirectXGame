#include "Window.h"

Window::Window()
    : m_hwnd(nullptr), m_className(L"SampleWindowClass")
{
}

bool Window::Initialize(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = m_className;

    if (!RegisterClass(&wc))
    {
        return false;
    }

    m_hwnd = CreateWindowEx(
        0,
        m_className,
        L"GAME",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!m_hwnd)
    {
        return false;
    }

    ShowWindow(m_hwnd, nCmdShow);

    return true;
}

HWND Window::GetHWND() const
{
    return m_hwnd;
}

LRESULT CALLBACK Window::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}