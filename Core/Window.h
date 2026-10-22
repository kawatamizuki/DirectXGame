#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

class Window
{
public:
    Window();

    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    using ResizeCallback = std::function<void(UINT width, UINT height)>;

    void SetResizeCallback(ResizeCallback callback);

    // エクスプローラーからウィンドウへファイル・フォルダがドロップされた時に呼ばれる(WM_DROPFILES)。
    // pathsは、ドロップされた物のフルパス(ワイド文字)。コールバックの中では、パスを溜めるだけにして、
    // コピーなどの重い処理はフレームの更新の中で行うこと(メッセージの処理が長く止まらないように)。
    using DropFilesCallback = std::function<void(const std::vector<std::wstring>& paths)>;
    void SetDropFilesCallback(DropFilesCallback callback);

    // マウスのホイールが回された時に呼ばれる(WM_MOUSEWHEEL)。notchesは回転量(1目盛り=1.0。奥へ回すと正)。
    using MouseWheelCallback = std::function<void(float notches)>;
    void SetMouseWheelCallback(MouseWheelCallback callback);

    HWND GetHWND() const;
private:
    HWND m_hwnd;
    const wchar_t* m_className;
  

  
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    ResizeCallback m_resizeCallback;
    DropFilesCallback m_dropFilesCallback;
    MouseWheelCallback m_mouseWheelCallback;


};
