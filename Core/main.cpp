
#include "Window.h"
#include "Game.h"
#include <cwchar>

#ifdef _DEBUG
#include "DebugMemory.h"

#endif

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ PWSTR pCmdLine,
    _In_ int nCmdShow)
{

#ifdef _DEBUG
    DebugMemory::EnableLeakCheck();
#endif

#ifndef ENABLE_EDITOR
    // 実行用のビルド(Release): exeのあるフォルダを、作業フォルダにする。
    // モデル・シェーダー・初期マップは、作業フォルダからの相対パスで読むため、ショートカットや別の場所から
    // 起動しても、exeの隣の素材を、正しく読める(ダブルクリックだけで動く)。
    // 開発用のビルド(Debug)は、プロジェクトのフォルダの素材を読むので、変えない。
    {
        wchar_t exePath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (length > 0 && length < MAX_PATH)
        {
            wchar_t* lastSeparator = wcsrchr(exePath, L'\\');
            if (lastSeparator)
            {
                *lastSeparator = L'\0';
                SetCurrentDirectoryW(exePath);
            }
        }
    }
#endif

    Window window;
    if (!window.Initialize(hInstance, nCmdShow))
    {
        return -1;
    }

    Game game;
   
    if (!game.Initialize(window.GetHWND()))
    {
        return -1;
    }

    window.SetResizeCallback(
        [&game](UINT width, UINT height)
        {
            game.OnResize(width, height);
        });

    // エクスプローラーからドロップされたファイル・フォルダは、Gameが溜めて、次のフレームの更新で取り込む。
    window.SetDropFilesCallback(
        [&game](const std::vector<std::wstring>& paths)
        {
            game.OnFilesDropped(paths);
        });

    // マウスのホイール(街づくりのカメラのズームなど)。
    window.SetMouseWheelCallback(
        [&game](float notches)
        {
            game.OnMouseWheel(notches);
        });

    MSG msg = {};

    while (true)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                break;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            game.RunFrame();
           
        }
    }

    game.Finalize();

    return 0;
}