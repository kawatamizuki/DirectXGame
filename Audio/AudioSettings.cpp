#include "AudioSettings.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "AudioMixer.h"
#include "Json.h"

namespace
{
    int ToPercent(float volume)
    {
        if (!std::isfinite(volume)) { return AudioSettings::kMax; }
        return AudioSettings::Clamp(static_cast<int>(std::lround(volume * 100.0f)));
    }
}

int AudioSettings::Clamp(int value)
{
    return value < kMin ? kMin : (value > kMax ? kMax : value);
}

bool AudioSettings::LoadFromFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    Json json;
    std::string error;
    if (!Json::Parse(text, json, error) || !json.IsObject())
    {
        return false;
    }

    // 数値が小数や範囲外でも、落とさずに0〜100へ収める。無い項目は今の値のまま。
    master = Clamp(static_cast<int>(std::lround(json.GetNumber("master", master))));
    bgm = Clamp(static_cast<int>(std::lround(json.GetNumber("bgm", bgm))));
    se = Clamp(static_cast<int>(std::lround(json.GetNumber("se", se))));
    return true;
}

bool AudioSettings::SaveToFile(const std::string& path) const
{
    Json json = Json::MakeObject();
    json.Set("version", 1);
    json.Set("master", master);
    json.Set("bgm", bgm);
    json.Set("se", se);
    const std::string text = json.Dump(true);

    std::error_code ec;
    const std::filesystem::path target(path);
    if (target.has_parent_path())
    {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) { return false; }
    }

    // 一時ファイルに書き切ってから置き換える(書き込み中に落ちても、前の設定が壊れない)。
    const std::string tempPath = path + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file) { return false; }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file)
        {
            file.close();
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }
    std::filesystem::rename(tempPath, target, ec);
    if (ec)
    {
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    return true;
}

void AudioSettings::ApplyTo(AudioMixer& mixer) const
{
    mixer.SetMasterVolume(static_cast<float>(Clamp(master)) / 100.0f);
    mixer.SetCategoryVolume(SoundCategory::Bgm, static_cast<float>(Clamp(bgm)) / 100.0f);
    mixer.SetCategoryVolume(SoundCategory::Se, static_cast<float>(Clamp(se)) / 100.0f);
}

AudioSettings AudioSettings::FromMixer(const AudioMixer& mixer)
{
    AudioSettings settings;
    settings.master = ToPercent(mixer.GetMasterVolume());
    settings.bgm = ToPercent(mixer.GetCategoryVolume(SoundCategory::Bgm));
    settings.se = ToPercent(mixer.GetCategoryVolume(SoundCategory::Se));
    return settings;
}
