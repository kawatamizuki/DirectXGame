#include "ClickDragGesture.h"

ClickDragGesture::Result ClickDragGesture::Update(bool pressedNow, bool canStart, bool down, float x, float y)
{
    Result result;

    if (pressedNow && canStart)
    {
        m_active = true;
        m_dragging = false;
        m_pressX = x;
        m_pressY = y;
    }
    result.pressX = m_pressX;
    result.pressY = m_pressY;

    if (!m_active)
    {
        return result;
    }

    if (down)
    {
        if (!m_dragging)
        {
            const float dx = x - m_pressX;
            const float dy = y - m_pressY;
            if (dx * dx + dy * dy > m_threshold * m_threshold)
            {
                m_dragging = true;
                result.dragStarted = true;
            }
        }
        result.active = true;
        result.dragging = m_dragging;
        return result;
    }

    // 離した。
    result.clicked = !m_dragging;
    result.dragEnded = m_dragging;
    m_active = false;
    m_dragging = false;
    return result;
}

void ClickDragGesture::Reset()
{
    m_active = false;
    m_dragging = false;
}
