#include "GameClock.h"
#include <cmath>
#include "imgui.h"

void GameClock::Update(float realDeltaSeconds)
{
    m_scaledDeltaSeconds = realDeltaSeconds * m_timeScale;

    // 1ゲーム内時間あたりの実秒数が0以下だと割り算が壊れるため、最小値で守る。
    float secondsPerHour = (m_secondsPerGameHour > 0.001f) ? m_secondsPerGameHour : 0.001f;
    m_timeOfDayHours += m_scaledDeltaSeconds / secondsPerHour;

    // 倍速やフレーム落ちで1フレームに24時間を超えることは通常ないが、念のためループで繰り越す。
    while (m_timeOfDayHours >= 24.0f)
    {
        m_timeOfDayHours -= 24.0f;
        m_dayCount++;
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

void GameClock::DrawDebugUI()
{
    // 他の常設パネル(Debug/Build/City Stats/Profiler/Log)と重ならない上部中央に配置する
    ImGui::SetNextWindowPos(ImVec2(490.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Time");

    ImGui::Text("Day %d  %02d:%02d", m_dayCount, GetHour(), GetMinute());

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

    // 時刻のスクラブ(見た目の昼夜の移り変わりを確認するための操作)。
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
    if (ImGui::DragFloat("Sec / game hour", &secondsPerHour, 0.5f, 1.0f, 120.0f, "%.1f"))
    {
        SetSecondsPerGameHour(secondsPerHour);
    }

    ImGui::End();
}
