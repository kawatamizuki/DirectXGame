#pragma once
#include <Windows.h>
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")
#include <array>
#include <vector>
#include <unordered_map>
#include "InputAction.h"
#include "EditorInputAction.h"
#include"KeyCode.h"

//========================================
// キーボード入力状態
//========================================
//
// current:
// 現在フレームの入力状態
//
// previous:
// 前フレームの入力状態
//
// current と previous を比較することで:
//
// - 押されている
// - 押された瞬間
// - 離された瞬間
//
// を判定する。
//
// 例:
//
// current = true
// previous = false
// ↓
// 今フレームで押された瞬間
//
//========================================

class InputManager
{
public:
    InputManager();
    ~InputManager();

    void Update();

    void SetWindowHandle(HWND hwnd);
    POINT GetMousePosition() const;

    bool IsAnyKeyPressed() const;//キーを問わず押されたときだけ
    bool IsAnyGamePadButtonPressed() const;//ボタンを問わず押されたときだけ
    bool IsAnyInputPressed() const;//キーボード or コントローラーの何かが押されたときだけ

    bool IsGamePadButtonDown(WORD button) const;//コントローラーのボタンが押されている間ずっと
    bool IsGamePadButtonPressed(WORD button) const;//コントローラーのボタンが押されたときだけ
    bool IsGamePadButtonReleased(WORD button) const;//コントローラーのボタンが離されたときだけ

    bool IsKeyDown(int key) const;//キーボードが押されている間ずっと
    bool IsKeyPressed(int key) const;//キーボードが押されたときだけ
    bool IsKeyReleased(int key) const;//キーボードが離されたときだけ

    bool IsKeyDown(KeyCode key) const;
    bool IsKeyPressed(KeyCode key) const;
    bool IsKeyReleased(KeyCode key) const;

    bool IsActionDown(InputAction action) const;
    bool IsActionPressed(InputAction action) const;
    bool IsActionReleased(InputAction action) const;

    //デバッグ用
    bool IsEditorActionDown(EditorInputAction action) const;
    bool IsEditorActionPressed(EditorInputAction action) const;
    bool IsEditorActionReleased(EditorInputAction action) const;




private:
    HWND m_hwnd;

    std::array<bool, 256> m_currentKeys;
    std::array<bool, 256> m_previousKeys;
    XINPUT_STATE m_currentGamePadState;
    XINPUT_STATE m_previousGamePadState;
    bool m_isGamePadConnected;
    int m_gamePadCheckFrame;

    std::unordered_map<InputAction, std::vector<KeyCode>> m_keyboardMapping;
    std::unordered_map<EditorInputAction, std::vector<KeyCode>> m_editorKeyboardMapping;//デバッグ用
    std::unordered_map<InputAction, std::vector<WORD>> m_gamePadMapping;


};