#include "AudioSystem.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include "Debug.h"
#include "SoundPack.h"

namespace
{
    // 聞こえない大きさ。これ以下の、繰り返さない音は、鳴らし始めない(無駄に声を使わない)。
    constexpr float kInaudibleGain = 0.0005f;
}

void AudioSystem::SetBackend(std::unique_ptr<IAudioBackend> backend)
{
    StopAll();
    m_backend = std::move(backend);
}

bool AudioSystem::Initialize()
{
    if (!m_backend)
    {
        m_backend = std::make_unique<NullAudioBackend>();
    }
    return m_backend->Initialize();
}

void AudioSystem::Shutdown()
{
    StopAll();
    if (m_backend)
    {
        m_backend->Shutdown();
    }
}

bool AudioSystem::IsAvailable() const
{
    return m_backend && m_backend->IsAvailable();
}

bool AudioSystem::LoadTable(const std::string& path)
{
    std::vector<std::string> warnings;
    std::string message;
    const bool ok = m_table.LoadFromFile(path, warnings, message);
    if (!ok)
    {
        Debug::Warning("AudioSystem: " + message);
        return false;
    }
    for (const std::string& warning : warnings)
    {
        Debug::Warning("AudioSystem: " + warning);
    }
    Debug::Info("AudioSystem: " + message);
    return true;
}

int AudioSystem::LoadPack(const std::string& path)
{
    if (!IsAvailable())
    {
        return 0;
    }
    std::vector<SoundPackEntry> entries;
    std::string message;
    if (!SoundPack::Read(path, entries, message))
    {
        // パックが無いのは、開発中は普通のこと。壊れている時だけ、警告する。
        if (std::filesystem::exists(path))
        {
            Debug::Warning("AudioSystem: " + message);
        }
        return 0;
    }
    int registered = 0;
    for (SoundPackEntry& entry : entries)
    {
        if (m_backend->RegisterMemoryFile(entry.name, std::move(entry.data)))
        {
            ++registered;
        }
        else
        {
            Debug::Warning("AudioSystem: 音のパックの中の音を登録できませんでした: " + entry.name);
        }
    }
    Debug::Info("AudioSystem: " + message + "、登録 " + std::to_string(registered) + " 件");
    return registered;
}

void AudioSystem::WarnOnce(const std::string& key, const std::string& message)
{
    if (m_warned.insert(key).second)
    {
        Debug::Warning("AudioSystem: " + message);
    }
}

float AudioSystem::ComputeGain(const Voice& voice, const SoundDefinition& def) const
{
    float gain = m_mixer.BaseGain(def) * voice.muteFactor * voice.fade;

    // 位置を持つ音は、聞く位置(カメラ)からの距離で小さくなる。位置が渡されていない音は、距離の影響を受けない。
    if (def.spatial && voice.hasPosition)
    {
        const float dx = voice.position.x - m_listener.x;
        const float dy = voice.position.y - m_listener.y;
        const float dz = voice.position.z - m_listener.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        gain *= DistanceGain(distance, def.minDistance, def.maxDistance, def.rolloff);
    }
    return gain;
}

AudioSystem::Voice* AudioSystem::FindVoice(VoiceId id)
{
    for (Voice& voice : m_voices)
    {
        if (voice.id == id) { return &voice; }
    }
    return nullptr;
}

const AudioSystem::Voice* AudioSystem::FindVoice(VoiceId id) const
{
    for (const Voice& voice : m_voices)
    {
        if (voice.id == id) { return &voice; }
    }
    return nullptr;
}

void AudioSystem::StopVoiceAt(size_t index)
{
    if (index >= m_voices.size())
    {
        return;
    }
    const VoiceId id = m_voices[index].id;
    if (m_backend)
    {
        m_backend->Stop(id);
    }
    if (id == m_bgmVoice)
    {
        m_bgmVoice = kInvalidVoiceId;
        m_currentBgmId.clear();
    }
    m_voices.erase(m_voices.begin() + static_cast<std::ptrdiff_t>(index));
}

void AudioSystem::StartFade(Voice& voice, float target, float seconds, bool stopWhenSilent)
{
    voice.fadeTarget = target;
    voice.stopWhenSilent = stopWhenSilent;
    if (seconds <= 0.0f || !std::isfinite(seconds))
    {
        voice.fade = target;        // 時間が0なら、すぐに切り替える
        voice.fadeRate = 0.0f;
    }
    else
    {
        voice.fadeRate = 1.0f / seconds;
    }
}

AudioSystem::Voice* AudioSystem::StartVoice(int defIndex, const DirectX::XMFLOAT3* position, float initialFade)
{
    if (!IsAvailable() || defIndex < 0 || static_cast<size_t>(defIndex) >= m_table.Size())
    {
        return nullptr;
    }
    const SoundDefinition& def = m_table.GetAll()[static_cast<size_t>(defIndex)];

    // ミュート中の、繰り返さない音は、鳴らし始めない(鳴らしても、聞こえないまま、声を使うだけ)。
    // 繰り返す音(曲・環境音)は、ミュート中でも、無音のまま鳴らし始める(解除した時に、途中から聞こえる)。
    const bool muted = m_mixer.IsMuted(def);
    if (muted && !def.loop)
    {
        return nullptr;
    }

    // 同時に鳴らせる数を超えるなら、同じ音の一番古いものを止める。
    int sameCount = 0;
    for (const Voice& voice : m_voices)
    {
        if (voice.defIndex == defIndex) { ++sameCount; }
    }
    while (sameCount >= def.maxVoices)
    {
        for (size_t i = 0; i < m_voices.size(); ++i)
        {
            if (m_voices[i].defIndex == defIndex)
            {
                StopVoiceAt(i);
                --sameCount;
                break;
            }
        }
    }

    Voice voice;
    voice.defIndex = defIndex;
    voice.hasPosition = (position != nullptr);
    if (position) { voice.position = *position; }
    voice.muteFactor = muted ? 0.0f : 1.0f;
    voice.fade = initialFade;
    voice.fadeTarget = initialFade;

    const float gain = ComputeGain(voice, def);

    // 繰り返さない音が、遠すぎて聞こえないなら、鳴らし始めない。
    // (フェードインで始める音は、始めの大きさが0なので、フェードを1として、判定する)
    if (!def.loop)
    {
        Voice probe = voice;
        probe.fade = 1.0f;
        if (ComputeGain(probe, def) <= kInaudibleGain)
        {
            return nullptr;
        }
    }

    const VoiceId id = m_backend->Play(def.file, def.loop, def.stream, gain);
    if (id == kInvalidVoiceId)
    {
        WarnOnce("play:" + def.file, "音を鳴らせませんでした(ファイルが無い・読めない?): " + def.file);
        return nullptr;
    }

    voice.id = id;
    voice.lastGain = gain;
    m_voices.push_back(voice);
    return &m_voices.back();
}

VoiceId AudioSystem::PlaySe(const std::string& id, const DirectX::XMFLOAT3* position)
{
    const int defIndex = m_table.IndexOf(id);
    if (defIndex < 0)
    {
        WarnOnce("unknown:" + id, "音の表に無いidです: " + id);
        return kInvalidVoiceId;
    }
    Voice* voice = StartVoice(defIndex, position, 1.0f);
    return voice ? voice->id : kInvalidVoiceId;
}

bool AudioSystem::PlayBgm(const std::string& id, float fadeInSeconds, float fadeOutSeconds)
{
    const int defIndex = m_table.IndexOf(id);
    if (defIndex < 0)
    {
        WarnOnce("unknown:" + id, "音の表に無いidです: " + id);
        return false;
    }

    // 今鳴っている曲と同じなら、最初からやり直さない。
    if (m_bgmVoice != kInvalidVoiceId && m_currentBgmId == id)
    {
        const Voice* current = FindVoice(m_bgmVoice);
        if (current && !(current->stopWhenSilent))
        {
            return true;
        }
    }

    // 今の曲は、フェードアウトして止める。
    if (m_bgmVoice != kInvalidVoiceId)
    {
        if (Voice* old = FindVoice(m_bgmVoice))
        {
            StartFade(*old, 0.0f, fadeOutSeconds, true);
        }
        m_bgmVoice = kInvalidVoiceId;
        m_currentBgmId.clear();
    }

    const bool fadeIn = (fadeInSeconds > 0.0f) && std::isfinite(fadeInSeconds);
    Voice* voice = StartVoice(defIndex, nullptr, fadeIn ? 0.0f : 1.0f);
    if (!voice)
    {
        return false;
    }
    if (fadeIn)
    {
        StartFade(*voice, 1.0f, fadeInSeconds, false);
    }
    m_bgmVoice = voice->id;
    m_currentBgmId = id;
    return true;
}

void AudioSystem::StopBgm(float fadeOutSeconds)
{
    if (m_bgmVoice == kInvalidVoiceId)
    {
        return;
    }
    if (Voice* voice = FindVoice(m_bgmVoice))
    {
        StartFade(*voice, 0.0f, fadeOutSeconds, true);
        if (voice->fade <= 0.0f)
        {
            // 時間0なら、その場で止める(StopVoiceAtが、曲の記録も消す)
            for (size_t i = 0; i < m_voices.size(); ++i)
            {
                if (m_voices[i].id == m_bgmVoice) { StopVoiceAt(i); break; }
            }
            return;
        }
    }
    m_bgmVoice = kInvalidVoiceId;
    m_currentBgmId.clear();
}

void AudioSystem::SetVoicePosition(VoiceId voice, const DirectX::XMFLOAT3& position)
{
    if (Voice* v = FindVoice(voice))
    {
        v->hasPosition = true;
        v->position = position;
    }
}

void AudioSystem::StopSound(const std::string& id)
{
    const int defIndex = m_table.IndexOf(id);
    if (defIndex < 0) { return; }
    for (size_t i = 0; i < m_voices.size();)
    {
        if (m_voices[i].defIndex == defIndex) { StopVoiceAt(i); }
        else { ++i; }
    }
}

void AudioSystem::StopAll()
{
    while (!m_voices.empty())
    {
        StopVoiceAt(m_voices.size() - 1);
    }
    m_bgmVoice = kInvalidVoiceId;
    m_currentBgmId.clear();
}

void AudioSystem::Update(float deltaSeconds)
{
    if (!m_backend)
    {
        return;
    }
    const float dt = (deltaSeconds > 0.0f && std::isfinite(deltaSeconds)) ? deltaSeconds : 0.0f;
    m_mixer.Update(dt);

    for (size_t i = 0; i < m_voices.size();)
    {
        Voice& voice = m_voices[i];
        const SoundDefinition& def = m_table.GetAll()[static_cast<size_t>(voice.defIndex)];

        // 鳴り終わった音は、片付ける(実際に鳴らす側にも、片付けを伝える。伝えないと、鳴り終わった音の分のメモリが残り続ける)。
        if (!m_backend->IsPlaying(voice.id))
        {
            if (voice.id == m_bgmVoice)
            {
                m_bgmVoice = kInvalidVoiceId;
                m_currentBgmId.clear();
            }
            m_backend->Stop(voice.id);
            m_voices.erase(m_voices.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        // ミュートの大きさを、少しずつ目標へ近づける(プツッというノイズを避ける)。
        const float muteTarget = m_mixer.IsMuted(def) ? 0.0f : 1.0f;
        const float muteStep = dt / kMuteFadeSeconds;
        if (voice.muteFactor < muteTarget) { voice.muteFactor = std::min(muteTarget, voice.muteFactor + muteStep); }
        else if (voice.muteFactor > muteTarget) { voice.muteFactor = std::max(muteTarget, voice.muteFactor - muteStep); }

        // フェード(曲の切り替え・停止)。
        if (voice.fade < voice.fadeTarget) { voice.fade = std::min(voice.fadeTarget, voice.fade + voice.fadeRate * dt); }
        else if (voice.fade > voice.fadeTarget) { voice.fade = std::max(voice.fadeTarget, voice.fade - voice.fadeRate * dt); }

        if (voice.stopWhenSilent && voice.fade <= 0.0f)
        {
            StopVoiceAt(i);
            continue;
        }

        const float gain = ComputeGain(voice, def);
        if (std::fabs(gain - voice.lastGain) > 0.00001f)
        {
            m_backend->SetGain(voice.id, gain);
            voice.lastGain = gain;
        }
        ++i;
    }
}

int AudioSystem::ActiveVoiceCount(const std::string& id) const
{
    const int defIndex = m_table.IndexOf(id);
    int count = 0;
    for (const Voice& voice : m_voices)
    {
        if (voice.defIndex == defIndex) { ++count; }
    }
    return count;
}

float AudioSystem::GetVoiceGain(VoiceId voice) const
{
    const Voice* v = FindVoice(voice);
    return v ? v->lastGain : -1.0f;
}
