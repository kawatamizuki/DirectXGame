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
    // 鳴っている1つの音。メモリ上のデータを、少しずつ読みながら鳴らす時だけ、デコーダーも持つ(音より後に片付ける)。
    struct Voice
    {
        std::unique_ptr<ma_sound> sound;
        std::unique_ptr<ma_decoder> decoder;
    };

    ma_engine engine{};
    bool engineReady = false;
    std::unordered_map<VoiceId, Voice> sounds;
    VoiceId nextId = 1;
    // メモリに登録した音のデータ(名前→バイト列)。miniaudioは、登録されたデータを、コピーせずに参照するので、エンジンを閉じるまで保つ。
    std::unordered_map<std::string, std::unique_ptr<std::vector<unsigned char>>> memoryFiles;

    void Release(Voice& voice)
    {
        if (voice.sound) { ma_sound_uninit(voice.sound.get()); }
        if (voice.decoder) { ma_decoder_uninit(voice.decoder.get()); }
    }
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
        m_impl->Release(entry.second);
    }
    m_impl->sounds.clear();

    if (m_impl->engineReady)
    {
        ma_engine_uninit(&m_impl->engine);
        m_impl->engineReady = false;
    }
    m_impl->memoryFiles.clear(); // エンジンを閉じた後(もう参照されない)
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

    Impl::Voice voice;
    voice.sound = std::make_unique<ma_sound>();

    // 短い音は、読み込み時に、全部デコードして持つ(鳴らす時の負荷が軽い)。長い曲は、少しずつ読みながら鳴らす(メモリを節約)。
    // 位置による処理(空間化)・音の高さの変更は、使わないので、切って、軽くする。
    ma_uint32 flags = stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    flags |= MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;

    const auto memory = m_impl->memoryFiles.find(file);
    if (stream && memory != m_impl->memoryFiles.end())
    {
        // 音のパックの中の曲: miniaudioの「ストリーミング」は、ディスクのファイルしか扱えないので、メモリ上のデータを、
        // デコーダー(エンジンと同じ形式・標本数に変換して出す)から、少しずつ読む形で鳴らす。
        voice.decoder = std::make_unique<ma_decoder>();
        const ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, ma_engine_get_channels(&m_impl->engine), ma_engine_get_sample_rate(&m_impl->engine));
        if (ma_decoder_init_memory(memory->second->data(), memory->second->size(), &decoderConfig, voice.decoder.get()) != MA_SUCCESS)
        {
            return kInvalidVoiceId;
        }
        if (ma_sound_init_from_data_source(&m_impl->engine, voice.decoder.get(), flags, nullptr, voice.sound.get()) != MA_SUCCESS)
        {
            ma_decoder_uninit(voice.decoder.get());
            return kInvalidVoiceId;
        }
    }
    else if (ma_sound_init_from_file(&m_impl->engine, file.c_str(), flags, nullptr, nullptr, voice.sound.get()) != MA_SUCCESS)
    {
        return kInvalidVoiceId; // ファイルが無い・読めない・対応していない形式
    }

    ma_sound_set_looping(voice.sound.get(), loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(voice.sound.get(), std::max(0.0f, gain));
    if (ma_sound_start(voice.sound.get()) != MA_SUCCESS)
    {
        m_impl->Release(voice);
        return kInvalidVoiceId;
    }

    const VoiceId id = m_impl->nextId++;
    m_impl->sounds[id] = std::move(voice);
    return id;
}

bool MiniaudioBackend::RegisterMemoryFile(const std::string& name, std::vector<unsigned char> data)
{
    if (!IsAvailable() || name.empty() || data.empty())
    {
        return false;
    }
    if (m_impl->memoryFiles.count(name) != 0)
    {
        return false; // 同じ名前は、1つだけ
    }
    auto blob = std::make_unique<std::vector<unsigned char>>(std::move(data));
    ma_resource_manager* manager = ma_engine_get_resource_manager(&m_impl->engine);
    if (manager == nullptr || ma_resource_manager_register_encoded_data(manager, name.c_str(), blob->data(), blob->size()) != MA_SUCCESS)
    {
        return false;
    }
    m_impl->memoryFiles[name] = std::move(blob);
    return true;
}

void MiniaudioBackend::SetGain(VoiceId voice, float gain)
{
    auto it = m_impl->sounds.find(voice);
    if (it != m_impl->sounds.end())
    {
        ma_sound_set_volume(it->second.sound.get(), (std::isfinite(gain) && gain > 0.0f) ? gain : 0.0f);
    }
}

void MiniaudioBackend::Stop(VoiceId voice)
{
    auto it = m_impl->sounds.find(voice);
    if (it != m_impl->sounds.end())
    {
        m_impl->Release(it->second);
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
    return ma_sound_is_playing(it->second.sound.get()) && !ma_sound_at_end(it->second.sound.get());
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
