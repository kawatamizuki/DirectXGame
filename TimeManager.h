#pragma once

#include <chrono>

class TimeManager
{
public:
    TimeManager();
    ~TimeManager();
    void BeginFrame();
    void Update();

    float GetDeltaTime() const;
    float GetFPS() const;

    void SetTargetFPS(int fps);
    void WaitForTargetFPS();


private:
    using Clock = std::chrono::steady_clock;


    Clock::time_point m_previousTime;
    Clock::time_point m_frameStartTime;

    float m_deltaTime;
    float m_fps;

    int m_frameCount;
    float m_fpsTimer;

    int m_targetFPS;

};