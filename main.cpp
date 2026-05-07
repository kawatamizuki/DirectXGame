
#include "Window.h"
#include "Game.h"

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ PWSTR pCmdLine,
    _In_ int nCmdShow)
{
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
            game.Update();
            game.Draw();
           
        }
    }

    game.Finalize();

    return 0;
}