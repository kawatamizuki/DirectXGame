#include "MiniaudioBackend.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

// miniaudioは、自分のヘッダーで多くの警告を出すので、このファイルでは、警告を止めて読み込む(外部のコードのため)。
#pragma warning(push, 0)
#include "miniaudio.h"
#pragma warning(pop)

struct MiniaudioBackend::Impl
{
    ma_engine engine{};
    bool engineReady = false;
    std::unordered_map<VoiceId, std::unique_ptr<ma_sound>> sounds;
    VoiceId nextId = 1;
};

MiniaudioBackend::MiniaudioBackend()
    : m_impl(std::make_unique<Impl>())
{
}

MiniaudioBackend::MiniaudioBackend(const Options& options)
    : m_impl(std::make_unique<Impl>())
    , m_options(options)
{
}

MiniaudioBackend::~MiniaudioBackend()
{
    Shutdown();
}

bool MiniaudioBackend::Initialize()
{
    if (m_impl->engineReady)
    {
        return true;
    }

    ma_engine_config config = ma_engine_config_init();
    if (m_options.noDevice)
    {
        config.noDevice = MA_TRUE;
        config.channels = m_options.channels;
        config.sampleRate = m_options.sampleRate;
    }

    // 音のデバイスが無い・開けない場合は、失敗を返すだけ(ゲームは、無音で続ける)。
    if (ma_engine_init(&config, &m_impl->engine) != MA_SUCCESS)
    {
        return false;
    }
    m_impl->engineReady = true;
    return true;
}

void MiniaudioBackend::Shutdown()
{
    if (!m_impl)
    {
        return;
    }
    for (auto& entry : m_impl->sounds)
    {
        ma_sound_uninit(entry.second.get());
    }
    m_impl->sounds.clear();

    if (m_impl->engineReady)
    {
        ma_engine_uninit(&m_impl->engine);
        m_impl->engineReady = false;
    }
}

bool MiniaudioBackend::IsAvailable() const
{
    return m_impl && m_impl->engineReady;
}

VoiceId MiniaudioBackend::Play(const std::string& file, bool loop, bool stream, float gain)
{
    if (!IsAvailable())
    {
        return kInvalidVoiceId;
    }

    auto sound = std::make_unique<ma_sound>();

    // 短い音は、読み込み時に、全部デコードして持つ(鳴らす時の負荷が軽い)。長い曲は、少しずつ読みながら鳴らす(メモリを節約)。
    // 位置による処理(空間化)・音の高さの変更は、使わないので、切って、軽くする。
    ma_uint32 flags = stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    flags |= MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;

    if (ma_sound_init_from_file(&m_impl->engine, file.c_str(), flags, nullptr, nullptr, sound.get()) != MA_SUCCESS)
    {
        return kInvalidVoiceId; // ファイルが無い・読めない・対応していない形式
    }

    ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(sound.get(), std::max(0.0f, gain));
    if (ma_sound_start(sound.get()) != MA_SUCCESS)
    {
        ma_sound_uninit(sound.get());
        return kInvalidVoiceId;
    }

    const VoiceId id = m_impl->nextId++;
    m_impl->sounds[id] = std::move(sound);
    return id;
}

void MiniaudioBackend::SetGain(VoiceId voice, float gain)
{
    auto it = m_impl->sounds.find(voice);
    if (it != m_impl->sounds.end())
    {
        ma_sound_set_volume(it->second.get(), (std::isfinite(gain) && gain > 0.0f) ? gain : 0.0f);
    }
}

void MiniaudioBackend::Stop(VoiceId voice)
{
    auto it = m_impl->sounds.find(voice);
    if (it != m_impl->sounds.end())
    {
        ma_sound_uninit(it->second.get());
        m_impl->sounds.erase(it);
    }
}

bool MiniaudioBackend::IsPlaying(VoiceId voice) const
{
    auto it = m_impl->sounds.find(voice);
    if (it == m_impl->sounds.end())
    {
        return false;
    }
    // 繰り返さない音は、最後まで鳴ると、終わった状態になる。
    return ma_sound_is_playing(it->second.get()) && !ma_sound_at_end(it->second.get());
}

unsigned int MiniaudioBackend::ReadFrames(float* output, unsigned int frames)
{
    if (!IsAvailable() || !m_options.noDevice || output == nullptr)
    {
        return 0;
    }
    ma_uint64 framesRead = 0;
    if (ma_engine_read_pcm_frames(&m_impl->engine, output, frames, &framesRead) != MA_SUCCESS)
    {
        return static_cast<unsigned int>(framesRead);
    }
    return static_cast<unsigned int>(framesRead);
}
