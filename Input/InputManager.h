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
    // マウスの位置(ピクセル)。ゲームビューの左上を原点にした値(SetViewOrigin で設定。無ければウィンドウの左上が原点)。
    // ビューの外にカーソルがあれば、負の値やビューより大きい値になる。
    POINT GetMousePosition() const;

    // ゲームビュー(ウィンドウの中のゲームの絵の四角)の左上の位置(ウィンドウのクライアント座標)。
    void SetViewOrigin(int x, int y) { m_viewOriginX = x; m_viewOriginY = y; }

    // マウスのホイール。ウィンドウのメッセージ(WM_MOUSEWHEEL)で届いた回転量を溜めておき(AddMouseWheel)、
    // 1フレームに1回、取り出す(ConsumeMouseWheel。取り出すと0に戻る)。単位は「ノッチ」(1目盛り=1.0。奥へ回すと正)。
    void AddMouseWheel(float notches) { m_wheelNotches += notches; }
    float ConsumeMouseWheel() { float value = m_wheelNotches; m_wheelNotches = 0.0f; return value; }

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
	bool IsEditorActionComboPressed(EditorInputAction action) const;//複数のキーの同時押しを判定する関数 




private:
    HWND m_hwnd;
    int m_viewOriginX = 0;   // ゲームビューの左上(ウィンドウのクライアント座標)
    int m_viewOriginY = 0;
    float m_wheelNotches = 0.0f;   // まだ取り出していないホイールの回転量(ノッチ)

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