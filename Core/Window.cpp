#include "Window.h"

#ifdef ENABLE_IMGUI
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
);

#endif






Window::Window()
    : m_hwnd(nullptr), m_className(L"WindowClass")
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
        1920, 1080,
        nullptr,
        nullptr,
        hInstance,
        this
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


    Window* window = nullptr;

    if (msg == WM_NCCREATE)
    {
        CREATESTRUCT* createStruct =
            reinterpret_cast<CREATESTRUCT*>(lparam);

        window =
            reinterpret_cast<Window*>(createStruct->lpCreateParams);

        SetWindowLongPtr(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(window)
        );
    }
    else
    {
        window =
            reinterpret_cast<Window*>(
                GetWindowLongPtr(hwnd, GWLP_USERDATA)
                );
    }

    if (window)
    {
        return window->HandleMessage(
            hwnd,
            msg,
            wparam,
            lparam
        );
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

LRESULT Window::HandleMessage(
    HWND hwnd,
    UINT msg,
    WPARAM wparam,
    LPARAM lparam)
{
#ifdef ENABLE_IMGUI
    if (ImGui_ImplWin32_WndProcHandler(
        hwnd,
        msg,
        wparam,
        lparam))
    {
        return true;
    }
#endif

    switch (msg)
    {
    case WM_SIZE:
    {
        UINT width = LOWORD(lparam);
        UINT height = HIWORD(lparam);

        if (m_resizeCallback)
        {
            m_resizeCallback(width, height);
        }

        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}
void Window::SetResizeCallback(ResizeCallback callback)
{
    m_resizeCallback = callback;
}
