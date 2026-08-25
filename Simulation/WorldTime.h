#pragma once

class WorldTime
{
public:
    void Update(float deltaSeconds);
    int GetDay() const;
    int GetHour() const;
    int GetMinute() const;
    float GetTimeOfDay01() const;
    float GetDaylight01() const;
    void SetHour(float hour);
    float GetMinutesPerRealSecond() const { return m_minutesPerRealSecond; }
    void SetMinutesPerRealSecond(float value);
    bool IsPaused() const { return m_paused; }
    void SetPaused(bool paused) { m_paused = paused; }

private:
    double m_totalMinutes = 8.0 * 60.0;
    float m_minutesPerRealSecond = 10.0f;
    bool m_paused = false;
};
