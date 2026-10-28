#pragma once

// 左ボタンの「クリック」と「ドラッグ」を見分ける小さな部品。
// ボタンを押した位置から、ほとんど動かさずに離したら「クリック」、押したまま、しきい値より動かしたら「ドラッグ」(離すまで続く)。
// ドラッグだった押下は、離した時に、クリックにならない。画面・入力・ImGuiを知らない(押した・押している・位置を渡すと、結果を返す)ので、
// 単体テストできる。始まる条件(UIの上でないなど)は、呼び出し側が canStart で渡す。
class ClickDragGesture
{
public:
    struct Result
    {
        bool active = false;        // 押下が、始まっていて、まだ続いているか(離したフレームは、false)
        bool dragging = false;      // ドラッグ中か(離したフレームは、false)
        bool dragStarted = false;   // このフレームに、ドラッグが始まったか
        bool clicked = false;       // このフレームに、離して、クリックになったか(ドラッグだった時は、false)
        bool dragEnded = false;     // このフレームに、離して、ドラッグが終わったか
        float pressX = 0.0f;        // 押した位置
        float pressY = 0.0f;
    };

    // 動かしたと見なす距離(ピクセル)。
    void SetThresholdPixels(float pixels) { m_threshold = pixels > 0.0f ? pixels : 0.0f; }
    float GetThresholdPixels() const { return m_threshold; }

    // 1フレームに1回。
    //  pressedNow: 左ボタンを押したフレームか。canStart: 今押したなら、この押下を、扱ってよいか(UIの上でない・他の操作に取られていない)。
    //  down: 押している間。x, y: カーソルの位置。
    Result Update(bool pressedNow, bool canStart, bool down, float x, float y);

    // 途中でやめる(メニューが開いた・配置を始めた時など)。クリックにも、ドラッグにもならない。
    void Reset();

    bool IsActive() const { return m_active; }
    bool IsDragging() const { return m_active && m_dragging; }
    float GetPressX() const { return m_pressX; }
    float GetPressY() const { return m_pressY; }

private:
    float m_threshold = 6.0f;
    bool m_active = false;
    bool m_dragging = false;
    float m_pressX = 0.0f;
    float m_pressY = 0.0f;
};
