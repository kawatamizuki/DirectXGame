#include "AudioMixer.h"
#include <algorithm>
#include <cmath>

float DistanceGain(float distance, float minDistance, float maxDistance, float rolloff)
{
    // 壊れた値(NaN・負の距離)は、「そのまま聞こえる」側に倒さず、近い扱い(減衰なし)にする。
    // (距離が分からない音を、勝手に消さないため)
    if (!std::isfinite(distance) || distance <= 0.0f)
    {
        return 1.0f;
    }
    if (!(rolloff > 0.0f) || !std::isfinite(rolloff))
    {
        rolloff = 1.0f;
    }
    if (minDistance < 0.0f || !std::isfinite(minDistance))
    {
        minDistance = 0.0f;
    }

    // 最大が最小以下なら、「最小を超えたら、聞こえない」とみなす(0で割らない)。
    if (!std::isfinite(maxDistance) || maxDistance <= minDistance)
    {
        return (distance <= minDistance) ? 1.0f : 0.0f;
    }

    if (distance <= minDistance) { return 1.0f; }
    if (distance >= maxDistance) { return 0.0f; }

    const float t = (distance - minDistance) / (maxDistance - minDistance); // 0〜1
    return std::pow(1.0f - t, rolloff);
}

float AudioMixer::Clamp01(float value)
{
    if (!std::isfinite(value)) { return 0.0f; }
    return std::max(0.0f, std::min(1.0f, value));
}

bool AudioMixer::Matches(const MuteTarget& target, const SoundDefinition& sound)
{
    switch (target.kind)
    {
    case MuteTarget::Kind::All:
        return true;
    case MuteTarget::Kind::Category:
        return sound.category == target.category;
    case MuteTarget::Kind::Sound:
        return sound.id == target.name;
    case MuteTarget::Kind::Tag:
        // タグが空の音は、どのタグ指定にも当たらない(空のタグを指定しても、全部が消えないように)。
        return !target.name.empty() && sound.tag == target.name;
    }
    return false;
}

MuteHandle AudioMixer::AddMute(const MuteTarget& target, float durationSeconds)
{
    MuteRequest request;
    request.handle = m_nextHandle++;
    request.target = target;
    request.timed = (durationSeconds > 0.0f) && std::isfinite(durationSeconds);
    request.remainingSeconds = request.timed ? durationSeconds : 0.0f;
    m_mutes.push_back(request);
    return request.handle;
}

bool AudioMixer::RemoveMute(MuteHandle handle)
{
    for (size_t i = 0; i < m_mutes.size(); ++i)
    {
        if (m_mutes[i].handle == handle)
        {
            m_mutes.erase(m_mutes.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

void AudioMixer::ClearMutes()
{
    m_mutes.clear();
}

void AudioMixer::Update(float deltaSeconds)
{
    if (!(deltaSeconds > 0.0f) || !std::isfinite(deltaSeconds))
    {
        return;
    }
    for (size_t i = 0; i < m_mutes.size();)
    {
        MuteRequest& request = m_mutes[i];
        if (request.timed)
        {
            request.remainingSeconds -= deltaSeconds;
            if (request.remainingSeconds <= 0.0f)
            {
                m_mutes.erase(m_mutes.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
        }
        ++i;
    }
}

float AudioMixer::BaseGain(const SoundDefinition& sound) const
{
    // 音ごとの音量は、0〜2に収める(表の値が壊れていても、音が割れないように)。
    const float soundVolume = std::isfinite(sound.volume) ? std::max(0.0f, std::min(sound.volume, 2.0f)) : 1.0f;
    return m_masterVolume * m_categoryVolume[Index(sound.category)] * soundVolume;
}

bool AudioMixer::IsMuted(const SoundDefinition& sound) const
{
    if (m_masterMuted || m_categoryMuted[Index(sound.category)])
    {
        return true;
    }
    for (const MuteRequest& request : m_mutes)
    {
        if (Matches(request.target, sound))
        {
            return true;
        }
    }
    return false;
}
