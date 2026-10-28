#include "SoundTable.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "Json.h"

namespace
{
    constexpr float kMaxSoundVolume = 2.0f;     // 音ごとの音量の上限(大きすぎる設定で、音が割れるのを防ぐ)
    constexpr int kMaxVoicesLimit = 32;         // 同時に鳴らせる数の上限

    bool IsFinite(float value)
    {
        return std::isfinite(value);
    }
}

bool SoundTable::Add(const SoundDefinition& definition)
{
    if (definition.id.empty() || IndexOf(definition.id) >= 0)
    {
        return false;
    }
    m_sounds.push_back(definition);
    return true;
}

int SoundTable::IndexOf(const std::string& id) const
{
    for (size_t i = 0; i < m_sounds.size(); ++i)
    {
        if (m_sounds[i].id == id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const SoundDefinition* SoundTable::Find(const std::string& id) const
{
    int index = IndexOf(id);
    return (index >= 0) ? &m_sounds[static_cast<size_t>(index)] : nullptr;
}

int SoundTable::LoadFromJson(const Json& root, std::vector<std::string>& warnings)
{
    m_sounds.clear();

    const Json* sounds = root.Find("sounds");
    if (!sounds || !sounds->IsArray())
    {
        warnings.push_back("音の表に \"sounds\" の配列がありません");
        return 0;
    }

    for (size_t i = 0; i < sounds->Size(); ++i)
    {
        const Json& entry = sounds->At(i);
        const std::string prefix = "sounds[" + std::to_string(i) + "]";
        if (!entry.IsObject())
        {
            warnings.push_back(prefix + " がオブジェクトではないため、飛ばしました");
            continue;
        }

        SoundDefinition def;
        def.id = entry.GetString("id");
        def.file = entry.GetString("file");
        if (def.id.empty())
        {
            warnings.push_back(prefix + " に id が無いため、飛ばしました");
            continue;
        }
        if (def.file.empty())
        {
            warnings.push_back(prefix + " (" + def.id + ") に file が無いため、飛ばしました");
            continue;
        }
        if (IndexOf(def.id) >= 0)
        {
            warnings.push_back(prefix + " の id \"" + def.id + "\" が重複しているため、飛ばしました");
            continue;
        }

        std::string categoryKey = entry.GetString("category", "Se");
        if (!SoundCategoryFromKey(categoryKey, def.category))
        {
            warnings.push_back(prefix + " (" + def.id + ") の種類 \"" + categoryKey + "\" は不明なため、飛ばしました");
            continue;
        }

        def.tag = entry.GetString("tag");
        def.loop = entry.GetBool("loop", false);
        def.stream = entry.GetBool("stream", false);
        def.spatial = entry.GetBool("spatial", false);

        // 数値は、範囲に収める(壊れた値・極端な値で、音が割れたり、計算が壊れたりしないように)。
        float volume = entry.GetFloat("volume", 1.0f);
        if (!IsFinite(volume) || volume < 0.0f || volume > kMaxSoundVolume)
        {
            warnings.push_back(prefix + " (" + def.id + ") の volume が範囲外のため、収めました");
            volume = IsFinite(volume) ? (volume < 0.0f ? 0.0f : kMaxSoundVolume) : 1.0f;
        }
        def.volume = volume;

        float minDistance = entry.GetFloat("minDistance", def.minDistance);
        float maxDistance = entry.GetFloat("maxDistance", def.maxDistance);
        if (!IsFinite(minDistance) || minDistance < 0.0f)
        {
            warnings.push_back(prefix + " (" + def.id + ") の minDistance が不正なため、既定値にしました");
            minDistance = def.minDistance;
        }
        if (!IsFinite(maxDistance) || maxDistance <= minDistance)
        {
            warnings.push_back(prefix + " (" + def.id + ") の maxDistance が minDistance 以下のため、広げました");
            maxDistance = minDistance + 1.0f;
        }
        def.minDistance = minDistance;
        def.maxDistance = maxDistance;

        float rolloff = entry.GetFloat("rolloff", 1.0f);
        if (!IsFinite(rolloff) || rolloff <= 0.0f)
        {
            warnings.push_back(prefix + " (" + def.id + ") の rolloff が不正なため、1にしました");
            rolloff = 1.0f;
        }
        def.rolloff = rolloff;

        int maxVoices = entry.GetInt("maxVoices", 4);
        if (maxVoices < 1 || maxVoices > kMaxVoicesLimit)
        {
            warnings.push_back(prefix + " (" + def.id + ") の maxVoices が範囲外のため、収めました");
            maxVoices = (maxVoices < 1) ? 1 : kMaxVoicesLimit;
        }
        def.maxVoices = maxVoices;

        m_sounds.push_back(def);
    }

    return static_cast<int>(m_sounds.size());
}

bool SoundTable::LoadFromFile(const std::string& path, std::vector<std::string>& warnings, std::string& message)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        message = "音の表を開けません: " + path;
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    Json root;
    std::string error;
    if (!Json::Parse(text, root, error))
    {
        message = "音の表のJSONが壊れています: " + error;
        return false;
    }

    LoadFromJson(root, warnings);
    message = "音の表を読みました: " + std::to_string(m_sounds.size()) + " 件";
    return true;
}

SoundDefinition* SoundTable::FindMutable(const std::string& id)
{
    int index = IndexOf(id);
    return (index >= 0) ? &m_sounds[static_cast<size_t>(index)] : nullptr;
}

namespace
{
    // 小数を、読みやすい桁(3桁)に丸めて、JSONの数にする(0.8fが 0.800000011920929 と書かれないように)。
    Json Rounded(float value)
    {
        return Json(std::round(static_cast<double>(value) * 1000.0) / 1000.0);
    }
}

Json SoundTable::ToJson() const
{
    const SoundDefinition defaults;
    Json root = Json::MakeObject();
    root.Set("version", 1);
    Json list = Json::MakeArray();
    for (const SoundDefinition& def : m_sounds)
    {
        Json entry = Json::MakeObject();
        entry.Set("id", def.id);
        entry.Set("file", def.file);
        entry.Set("category", SoundCategoryKey(def.category));
        if (!def.tag.empty()) { entry.Set("tag", def.tag); }
        entry.Set("volume", Rounded(def.volume));
        if (def.loop) { entry.Set("loop", true); }
        if (def.stream) { entry.Set("stream", true); }
        if (def.spatial)
        {
            entry.Set("spatial", true);
            entry.Set("minDistance", Rounded(def.minDistance));
            entry.Set("maxDistance", Rounded(def.maxDistance));
            if (def.rolloff != defaults.rolloff) { entry.Set("rolloff", Rounded(def.rolloff)); }
        }
        if (def.maxVoices != defaults.maxVoices) { entry.Set("maxVoices", def.maxVoices); }
        list.Push(entry);
    }
    root.Set("sounds", list);
    return root;
}

bool SoundTable::SaveToFile(const std::string& path, std::string& message) const
{
    const std::string text = ToJson().Dump(true);
    const std::string tempPath = path + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            message = "音の表を書けません: " + tempPath;
            return false;
        }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file)
        {
            file.close();
            std::error_code ignored;
            std::filesystem::remove(tempPath, ignored);
            message = "音の表の書き込みに失敗しました: " + path;
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tempPath, path, ec);
    if (ec)
    {
        std::filesystem::remove(tempPath, ec);
        message = "音の表を置き換えられません: " + path;
        return false;
    }
    message = "音の表を保存しました: " + path;
    return true;
}
