#include "InputManager.h"

InputManager::InputManager()
    : m_hwnd(nullptr)
    , m_gamePadCheckFrame(0)
    , m_isGamePadConnected(false)
{
    
    m_currentKeys.fill(false);
    m_previousKeys.fill(false);
    ZeroMemory(&m_currentGamePadState, sizeof(XINPUT_STATE));
    ZeroMemory(&m_previousGamePadState, sizeof(XINPUT_STATE));
    m_isGamePadConnected = false;

    //========================================
    // キーボード割り当て
    //========================================

    m_keyboardMapping[InputAction::Decide] =
    {
        KeyCode::Enter,
        KeyCode::Space,
        KeyCode::MouseLeft
    };

    m_keyboardMapping[InputAction::Cancel] =
    {
        KeyCode::Escape,
        KeyCode::MouseRight
    };

    m_keyboardMapping[InputAction::Pause] =
    {
        KeyCode::Escape
    };

    //========================================
    // Editor用キーボード割り当て
    //========================================

    m_editorKeyboardMapping[EditorInputAction::FreeCameraLook] =
    {
        KeyCode::MouseRight
    };

    m_editorKeyboardMapping[EditorInputAction::FreeCameraForward] =
    {
        KeyCode::W
    };

    m_editorKeyboardMapping[EditorInputAction::FreeCameraBackward] =
    {
        KeyCode::S
    };

    m_editorKeyboardMapping[EditorInputAction::FreeCameraLeft] =
    {
        KeyCode::A
    };

    m_editorKeyboardMapping[EditorInputAction::FreeCameraRight] =
    {
        KeyCode::D
    };

    m_editorKeyboardMapping[EditorInputAction::FreeCameraUp] =
    {
        KeyCode::E
    };

    m_editorKeyboardMapping[EditorInputAction::FreeCameraDown] =
    {
        KeyCode::Q
    };

    m_editorKeyboardMapping[EditorInputAction::FreeCameraFast] =
    {
        KeyCode::Shift
    };

    m_editorKeyboardMapping[EditorInputAction::FocusSelected] =
    {
        KeyCode::F
    };
    m_editorKeyboardMapping[EditorInputAction::GizmoMove] =
    {
        KeyCode::W
    };

    m_editorKeyboardMapping[EditorInputAction::GizmoRotate] =
    {
        KeyCode::R
    };

    m_editorKeyboardMapping[EditorInputAction::GizmoScale] =
    {
        KeyCode::E
    };
    //========================================
    // GamePad割り当て
    //========================================

    m_gamePadMapping[InputAction::Decide] =
    {
        XINPUT_GAMEPAD_A,
        XINPUT_GAMEPAD_START
    };
    m_gamePadMapping[InputAction::Cancel] = 
    { 
        XINPUT_GAMEPAD_B 
    };
    m_gamePadMapping[InputAction::Pause] = 
    { 
        XINPUT_GAMEPAD_START 
    };
}

InputManager::~InputManager()
{
}

void InputManager::Update()
{
    m_previousKeys = m_currentKeys;

    for (int i = 0; i < 256; ++i)
    {
        m_currentKeys[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    }

    m_previousGamePadState = m_currentGamePadState;

    //========================================
    // 未接続時は毎フレーム確認しない
    //========================================

    if (!m_isGamePadConnected)
    {
        ++m_gamePadCheckFrame;

        if (m_gamePadCheckFrame < 60)
        {
            return;
        }

        m_gamePadCheckFrame = 0;
    }

    ZeroMemory(
        &m_currentGamePadState,
        sizeof(XINPUT_STATE)
    );

    DWORD result =
        XInputGetState(0, &m_currentGamePadState);

    m_isGamePadConnected =
        (result == ERROR_SUCCESS);
}

void InputManager::SetWindowHandle(HWND hwnd)
{
    m_hwnd = hwnd;
}

POINT InputManager::GetMousePosition() const
{
    POINT point{};
    GetCursorPos(&point);

    if (m_hwnd)
    {
        ScreenToClient(m_hwnd, &point);
    }

    return point;
}

bool InputManager::IsAnyKeyPressed() const
{
    for (int i = 0; i < 256; ++i)
    {
        if (IsKeyPressed(i))
        {
            return true;
        }
    }

    return false;
}

bool InputManager::IsAnyGamePadButtonPressed() const
{
    if (!m_isGamePadConnected)
    {
        return false;
    }

    WORD current = m_currentGamePadState.Gamepad.wButtons;
    WORD previous = m_previousGamePadState.Gamepad.wButtons;

    return (current & ~previous) != 0;
}

bool InputManager::IsAnyInputPressed() const
{
    return IsAnyKeyPressed() || IsAnyGamePadButtonPressed();
}

bool InputManager::IsGamePadButtonDown(WORD button) const
{
    if (!m_isGamePadConnected)
    {
        return false;
    }

    return (m_currentGamePadState.Gamepad.wButtons & button) != 0;
}

bool InputManager::IsGamePadButtonPressed(WORD button) const
{
    if (!m_isGamePadConnected)
    {
        return false;
    }

    bool current = (m_currentGamePadState.Gamepad.wButtons & button) != 0;
    bool previous = (m_previousGamePadState.Gamepad.wButtons & button) != 0;

    return current && !previous;
}

bool InputManager::IsGamePadButtonReleased(WORD button) const
{
    if (!m_isGamePadConnected)
    {
        return false;
    }

    bool current = (m_currentGamePadState.Gamepad.wButtons & button) != 0;
    bool previous = (m_previousGamePadState.Gamepad.wButtons & button) != 0;

    return !current && previous;
}

bool InputManager::IsKeyDown(int key) const
{
    if (key < 0 || key >= 256)
    {
        return false;
    }
    return m_currentKeys[key];
}

bool InputManager::IsKeyPressed(int key) const
{
    if (key < 0 || key >= 256)
    {
        return false;
    }
    return m_currentKeys[key] && !m_previousKeys[key];
}

bool InputManager::IsKeyReleased(int key) const
{
    if (key < 0 || key >= 256)
    {
        return false;
    }
    return !m_currentKeys[key] && m_previousKeys[key];
}

bool InputManager::IsKeyDown(KeyCode key) const
{
    return IsKeyDown(static_cast<int>(key));
}

bool InputManager::IsKeyPressed(KeyCode key) const
{
    return IsKeyPressed(static_cast<int>(key));
}

bool InputManager::IsKeyReleased(KeyCode key) const
{
    return IsKeyReleased(static_cast<int>(key));
}

bool InputManager::IsEditorActionDown(EditorInputAction action) const
{
    auto it =
        m_editorKeyboardMapping.find(action);

    if (it == m_editorKeyboardMapping.end())
    {
        return false;
    }

    for (KeyCode key : it->second)
    {
        if (IsKeyDown(key))
        {
            return true;
        }
    }

    return false;
}

bool InputManager::IsEditorActionPressed(EditorInputAction action) const
{
    auto it =
        m_editorKeyboardMapping.find(action);

    if (it == m_editorKeyboardMapping.end())
    {
        return false;
    }

    for (KeyCode key : it->second)
    {
        if (IsKeyPressed(key))
        {
            return true;
        }
    }

    return false;
}

bool InputManager::IsEditorActionReleased(EditorInputAction action) const
{
    auto it =
        m_editorKeyboardMapping.find(action);

    if (it == m_editorKeyboardMapping.end())
    {
        return false;
    }

    for (KeyCode key : it->second)
    {
        if (IsKeyReleased(key))
        {
            return true;
        }
    }

    return false;
}

bool InputManager::IsActionDown(InputAction action) const
{
    bool keyboard = false;
    bool gamePad = false;

    auto keyIt = m_keyboardMapping.find(action);

    for (KeyCode key : keyIt->second)
    {
        if (IsKeyDown(key))
        {
            keyboard = true;
            break;
        }
    }

    auto gamePadIt = m_gamePadMapping.find(action);

    if (gamePadIt != m_gamePadMapping.end())
    {
        for (WORD button : gamePadIt->second)
        {
            if (IsGamePadButtonDown(button))
            {
                gamePad = true;
                break;
            }
        }
    }

    return keyboard || gamePad;
}



bool InputManager::IsActionPressed(InputAction action) const
{
    bool keyboard = false;
    bool gamePad = false;

    auto keyIt = m_keyboardMapping.find(action);

    if (keyIt != m_keyboardMapping.end())
    {
        for (KeyCode key : keyIt->second)
        {
            if (IsKeyPressed(key))
            {
                keyboard = true;
                break;
            }
        }
    }

    auto gamePadIt = m_gamePadMapping.find(action);

    if (gamePadIt != m_gamePadMapping.end())
    {
        for (WORD button : gamePadIt->second)
        {
            if (IsGamePadButtonPressed(button))
            {
                gamePad = true;
                break;
            }
        }
    }

    return keyboard || gamePad;
}

bool InputManager::IsActionReleased(InputAction action) const
{
    bool keyboard = false;
    bool gamePad = false;

    auto keyIt = m_keyboardMapping.find(action);

    if (keyIt != m_keyboardMapping.end())
    {
        for (KeyCode key : keyIt->second)
        {
            if (IsKeyReleased(key))
            {
                keyboard = true;
                break;
            }
        }
    }

    auto gamePadIt = m_gamePadMapping.find(action);

    if (gamePadIt != m_gamePadMapping.end())
    {
        for (WORD button : gamePadIt->second)
        {
            if (IsGamePadButtonReleased(button))
            {
                gamePad = true;
                break;
            }
        }
    }

    return keyboard || gamePad;
}


