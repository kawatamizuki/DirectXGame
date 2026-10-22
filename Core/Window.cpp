#include "Window.h"
#include <shellapi.h> // DragAcceptFiles / DragQueryFileW

#ifdef ENABLE_EDITOR
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

    // エクスプローラーからのファイル・フォルダのドロップ(WM_DROPFILES)を受け付ける。
    // (ゲームを管理者として実行していると、通常の権限のエクスプローラーからのドロップは、Windowsに拒否される)
    DragAcceptFiles(m_hwnd, TRUE);

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
#ifdef ENABLE_EDITOR
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

    case WM_MOUSEWHEEL:
    {
        // 回転量は、1目盛りが WHEEL_DELTA(120)。ノッチ(1目盛り=1.0)にして渡す。
        const float notches = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(WHEEL_DELTA);
        if (m_mouseWheelCallback)
        {
            m_mouseWheelCallback(notches);
        }
        return 0;
    }

    case WM_DROPFILES:
    {
        // ドロップされたパスを全て取り出して、コールバックへ渡す(コピーは、フレームの更新の中で行う)。
        HDROP drop = reinterpret_cast<HDROP>(wparam);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);

        std::vector<std::wstring> paths;
        paths.reserve(count);
        for (UINT i = 0; i < count; ++i)
        {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(static_cast<size_t>(length) + 1, L'\0');
            const UINT copied = DragQueryFileW(drop, i, path.data(), length + 1);
            path.resize(copied);
            if (!path.empty())
            {
                paths.push_back(std::move(path));
            }
        }
        DragFinish(drop);

        if (m_dropFilesCallback && !paths.empty())
        {
            m_dropFilesCallback(paths);
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

void Window::SetDropFilesCallback(DropFilesCallback callback)
{
    m_dropFilesCallback = std::move(callback);
}

void Window::SetMouseWheelCallback(MouseWheelCallback callback)
{
    m_mouseWheelCallback = std::move(callback);
}