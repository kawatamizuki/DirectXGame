#include "WorldTime.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double kMinutesPerDay = 24.0 * 60.0;
}

void WorldTime::Update(float deltaSeconds)
{
    if (!m_paused && deltaSeconds > 0.0f)
    {
        m_totalMinutes += static_cast<double>(deltaSeconds) * m_minutesPerRealSecond;
    }
}

int WorldTime::GetDay() const
{
    return static_cast<int>(m_totalMinutes / kMinutesPerDay) + 1;
}

int WorldTime::GetHour() const
{
    const double minutes = std::fmod(m_totalMinutes, kMinutesPerDay);
    return static_cast<int>(minutes / 60.0);
}

int WorldTime::GetMinute() const
{
    const double minutes = std::fmod(m_totalMinutes, kMinutesPerDay);
    return static_cast<int>(minutes) % 60;
}

float WorldTime::GetTimeOfDay01() const
{
    return static_cast<float>(std::fmod(m_totalMinutes, kMinutesPerDay) / kMinutesPerDay);
}

float WorldTime::GetDaylight01() const
{
    constexpr float kPi = 3.14159265358979323846f;
    const float sunHeight = std::sin((GetTimeOfDay01() - 0.25f) * 2.0f * kPi);
    return (std::max)(0.0f, (std::min)(1.0f, sunHeight * 0.5f + 0.5f));
}

void WorldTime::SetHour(float hour)
{
    hour = (std::max)(0.0f, (std::min)(23.999f, hour));
    const double dayStart = std::floor(m_totalMinutes / kMinutesPerDay) * kMinutesPerDay;
    m_totalMinutes = dayStart + static_cast<double>(hour) * 60.0;
}

void WorldTime::SetMinutesPerRealSecond(float value)
{
    m_minutesPerRealSecond = (std::max)(0.0f, value);
}
