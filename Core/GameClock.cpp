#include "GameClock.h"
#include <cmath>
#include "imgui.h"

namespace
{
    constexpr float kEpsilon = 1e-4f;   // 区切りの時刻の、比較の誤差
}

bool GameClock::IsSleeping() const
{
    return m_timeOfDayHours >= kSleepHour || m_timeOfDayHours < kWakeHour;
}

void GameClock::OnSleepStart()
{
    m_pendingWeekEnds++;
}

void GameClock::OnWake()
{
    m_weekCount++;
    m_pendingWeekStarts++;
}

int GameClock::ConsumeWeekEnds()
{
    const int count = m_pendingWeekEnds;
    m_pendingWeekEnds = 0;
    return count;
}

int GameClock::ConsumeWeekStarts()
{
    const int count = m_pendingWeekStarts;
    m_pendingWeekStarts = 0;
    return count;
}

void GameClock::Update(float realDeltaSeconds)
{
    m_scaledDeltaSeconds = realDeltaSeconds * m_timeScale;
    m_activeDeltaHours = 0.0f;

    // 活動時間と就寝で、時間の進む速さが違う。区切り(就寝に入る・24時・起床)ごとに、速さを切り替えて進める。
    // 1フレームで複数の区切りをまたいでも(倍速・フレーム落ち)、区切りの処理を取りこぼさない。
    float remainingSeconds = m_scaledDeltaSeconds;
    int guard = 0;
    while (remainingSeconds > 0.0f && guard++ < 16)
    {
        const bool sleeping = IsSleeping();
        const float secondsPerHour = std::fmax(0.001f, sleeping ? m_sleepSecondsPerGameHour : m_secondsPerGameHour);

        // 次の区切りの時刻。就寝中で22時以降なら24時、0時〜起床前なら起床、活動中なら就寝。
        float boundary = 0.0f;
        if (m_timeOfDayHours >= kSleepHour) { boundary = 24.0f; }
        else if (m_timeOfDayHours < kWakeHour) { boundary = kWakeHour; }
        else { boundary = kSleepHour; }

        const float secondsToBoundary = (boundary - m_timeOfDayHours) * secondsPerHour;
        if (remainingSeconds < secondsToBoundary - kEpsilon)
        {
            const float hours = remainingSeconds / secondsPerHour;
            m_timeOfDayHours += hours;
            if (!sleeping) { m_activeDeltaHours += hours; }
            remainingSeconds = 0.0f;
            break;
        }

        // 区切りまで進めて、区切りの処理を行う。
        const float hours = boundary - m_timeOfDayHours;
        if (!sleeping) { m_activeDeltaHours += hours; }
        remainingSeconds -= std::fmax(0.0f, secondsToBoundary);

        if (boundary == 24.0f)
        {
            m_timeOfDayHours = 0.0f;
            m_dayCount++;
        }
        else if (boundary == kWakeHour)
        {
            m_timeOfDayHours = kWakeHour;
            OnWake();
        }
        else
        {
            m_timeOfDayHours = kSleepHour;
            OnSleepStart();
        }
    }
}

int GameClock::GetHour() const
{
    int hour = static_cast<int>(std::floor(m_timeOfDayHours));
    if (hour < 0) { hour = 0; }
    if (hour > 23) { hour = 23; }
    return hour;
}

int GameClock::GetMinute() const
{
    float fraction = m_timeOfDayHours - std::floor(m_timeOfDayHours);
    int minute = static_cast<int>(fraction * 60.0f);
    if (minute < 0) { minute = 0; }
    if (minute > 59) { minute = 59; }
    return minute;
}

void GameClock::SetTimeOfDayHours(float hours)
{
    // 0〜24に正規化する(範囲外の値でも壊れないように)。
    float wrapped = std::fmod(hours, 24.0f);
    if (wrapped < 0.0f)
    {
        wrapped += 24.0f;
    }
    m_timeOfDayHours = wrapped;
}

void GameClock::SetTimeScale(float scale)
{
    m_timeScale = (scale > 0.0f) ? scale : 0.0f;
}

void GameClock::SetSecondsPerGameHour(float seconds)
{
    m_secondsPerGameHour = (seconds > 0.001f) ? seconds : 0.001f;
}

void GameClock::Save(Json& section, const SaveContext& /*context*/) const
{
    section.Set("timeOfDayHours", m_timeOfDayHours);
    section.Set("dayCount", m_dayCount);
    section.Set("weekCount", m_weekCount);
}

void GameClock::Load(const Json& section, int /*version*/, const LoadContext& /*context*/)
{
    // 足りない項目は既定値(朝8時・1日目・第1週)。週の記録が無い古いセーブは、日数を、週の数として引き継ぐ。
    SetTimeOfDayHours(section.GetFloat("timeOfDayHours", kStartHour));
    int day = section.GetInt("dayCount", 1);
    m_dayCount = (day >= 1) ? day : 1;
    int week = section.GetInt("weekCount", m_dayCount);
    m_weekCount = (week >= 1) ? week : 1;
    m_pendingWeekEnds = 0;
    m_pendingWeekStarts = 0;
}

void GameClock::ResetToDefault()
{
    m_timeOfDayHours = kStartHour;
    m_dayCount = 1;
    m_weekCount = 1;
    m_pendingWeekEnds = 0;
    m_pendingWeekStarts = 0;
}

void GameClock::DrawDebugUI(bool* open)
{
    if (open && !*open)
    {
        return;
    }

    // 他の常設パネル(Debug/Build/City Stats/Profiler/Log)と重ならない上部中央に配置する
    ImGui::SetNextWindowPos(ImVec2(490.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Time", open);

    ImGui::Text("Year %d  Month %d  Week %d/%d  (week #%d)", GetYear(), GetMonth(), GetWeekOfMonth(), kWeeksPerMonth, m_weekCount);
    ImGui::Text("Day %d  %02d:%02d  %s", m_dayCount, GetHour(), GetMinute(), IsSleeping() ? "(sleeping)" : "(active)");

    // 時間の倍率(0=停止)。ラジオボタンは現在の倍率に一致するものを選択状態にする。
    int speedIndex = 1;
    if (m_timeScale <= 0.0f)      { speedIndex = 0; }
    else if (m_timeScale <= 1.0f) { speedIndex = 1; }
    else if (m_timeScale <= 2.0f) { speedIndex = 2; }
    else                          { speedIndex = 3; }

    bool speedChanged = false;
    speedChanged |= ImGui::RadioButton("Pause", &speedIndex, 0);
    ImGui::SameLine();
    speedChanged |= ImGui::RadioButton("1x", &speedIndex, 1);
    ImGui::SameLine();
    speedChanged |= ImGui::RadioButton("2x", &speedIndex, 2);
    ImGui::SameLine();
    speedChanged |= ImGui::RadioButton("4x", &speedIndex, 3);
    if (speedChanged)
    {
        const float kScales[4] = { 0.0f, 1.0f, 2.0f, 4.0f };
        SetTimeScale(kScales[speedIndex]);
    }

    // 時刻のスクラブ(見た目の昼夜の移り変わりを確認するための操作)。区切りを飛び越えても、週の処理は起きない(見た目だけ)。
    float hours = m_timeOfDayHours;
    if (ImGui::SliderFloat("Time of day", &hours, 0.0f, 23.99f, "%.2f h"))
    {
        SetTimeOfDayHours(hours);
    }

    // 時間帯へのジャンプ(朝/昼/夕/夜)。
    if (ImGui::Button("Morning")) { SetTimeOfDayHours(8.0f); }
    ImGui::SameLine();
    if (ImGui::Button("Noon")) { SetTimeOfDayHours(12.0f); }
    ImGui::SameLine();
    if (ImGui::Button("Evening")) { SetTimeOfDayHours(17.5f); }
    ImGui::SameLine();
    if (ImGui::Button("Night")) { SetTimeOfDayHours(22.0f); }

    float secondsPerHour = m_secondsPerGameHour;
    if (ImGui::DragFloat("Sec / game hour (active)", &secondsPerHour, 0.1f, 0.5f, 60.0f, "%.2f"))
    {
        SetSecondsPerGameHour(secondsPerHour);
    }
    ImGui::Text("1 week (active) = %.0f s,  sleep = %.0f s",
        m_secondsPerGameHour * (kSleepHour - kWakeHour), m_sleepSecondsPerGameHour * (24.0f - (kSleepHour - kWakeHour)));

    ImGui::End();
}
