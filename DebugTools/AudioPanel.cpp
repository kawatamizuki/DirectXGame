#include "AudioPanel.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include "imgui.h"
#include "AudioSystem.h"
#include "AudioSettings.h"

namespace
{
    constexpr const char* kWindowName = "Audio";
    constexpr int kMaxSoundPercent = 200;       // 音ごとの音量の上限(%)。SoundTableの上限(2.0)と同じ

    int ToPercent(float volume)
    {
        return static_cast<int>(std::lround(volume * 100.0f));
    }
}

void AudioPanel::Initialize(AudioSystem* audio, const std::string& tablePath)
{
    m_audio = audio;
    m_tablePath = tablePath;
    RememberSavedVolumes();
}

void AudioPanel::RememberSavedVolumes()
{
    m_savedVolumes.clear();
    if (!m_audio)
    {
        return;
    }
    for (const SoundDefinition& def : m_audio->Table().GetAll())
    {
        m_savedVolumes[def.id] = def.volume;
    }
}

bool AudioPanel::HasUnsavedChanges() const
{
    if (!m_audio)
    {
        return false;
    }
    for (const SoundDefinition& def : m_audio->Table().GetAll())
    {
        const auto it = m_savedVolumes.find(def.id);
        if (it == m_savedVolumes.end() || ToPercent(it->second) != ToPercent(def.volume))
        {
            return true;
        }
    }
    return false;
}

void AudioPanel::DrawDebugUI()
{
    if (!*m_openFlag)
    {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(320.0f, 140.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kWindowName, m_openFlag))
    {
        ImGui::End();
        return;
    }
    if (!m_audio)
    {
        ImGui::TextUnformatted("音の仕組みがつながっていません");
        ImGui::End();
        return;
    }

    AudioMixer& mixer = m_audio->Mixer();

    // ---- 全体の音量(ゲーム内「オプション」と同じ値) ----
    ImGui::TextUnformatted("音量(0〜100。ゲーム内メニューの「オプション」と同じ。自動で保存されます)");
    AudioSettings volumes = AudioSettings::FromMixer(mixer);
    bool changed = false;
    bool masterMuted = mixer.IsMasterMuted();
    bool bgmMuted = mixer.IsCategoryMuted(SoundCategory::Bgm);
    bool seMuted = mixer.IsCategoryMuted(SoundCategory::Se);

    auto volumeRow = [&](const char* label, int* value, bool* muted, const char* id)
    {
        ImGui::PushID(id);
        ImGui::SetNextItemWidth(110.0f);
        changed |= ImGui::InputInt("##num", value, 1, 10);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        changed |= ImGui::SliderInt("##slider", value, AudioSettings::kMin, AudioSettings::kMax);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("ミュート", muted);
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        ImGui::PopID();
    };
    volumeRow("全体", &volumes.master, &masterMuted, "master");
    volumeRow("BGM", &volumes.bgm, &bgmMuted, "bgm");
    volumeRow("効果音", &volumes.se, &seMuted, "se");
    if (changed)
    {
        volumes.master = AudioSettings::Clamp(volumes.master);
        volumes.bgm = AudioSettings::Clamp(volumes.bgm);
        volumes.se = AudioSettings::Clamp(volumes.se);
        volumes.ApplyTo(mixer);
        mixer.SetMasterMuted(masterMuted);
        mixer.SetCategoryMuted(SoundCategory::Bgm, bgmMuted);
        mixer.SetCategoryMuted(SoundCategory::Se, seMuted);
    }

    ImGui::Separator();
    ImGui::Text("状態: %s / 鳴っている音: %d / 今の曲: %s",
        m_audio->IsAvailable() ? "音が出せます" : "音が出せません(無音で動作中)",
        m_audio->ActiveVoiceCount(),
        m_audio->CurrentBgmId().empty() ? "なし" : m_audio->CurrentBgmId().c_str());

    // ---- 音の一覧 ----
    SoundTable& table = m_audio->Table();
    ImGui::Text("音の一覧(%d 件)。音量は %% で、100=素材のまま、最大 %d", static_cast<int>(table.Size()), kMaxSoundPercent);

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    if (ImGui::BeginTable("##sounds", 6, flags, ImVec2(0.0f, -footerHeight)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("試聴", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("種類", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("音量(%)", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("スライダー", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("ファイル / 属性", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const std::vector<SoundDefinition>& all = table.GetAll();
        for (size_t i = 0; i < all.size(); ++i)
        {
            const SoundDefinition& def = all[i];
            SoundDefinition* editable = table.FindMutable(def.id);
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            const bool isBgm = (def.category == SoundCategory::Bgm);
            const bool playingThis = isBgm && m_audio->CurrentBgmId() == def.id;
            if (ImGui::Button(playingThis ? "Stop" : "Play", ImVec2(40.0f, 0.0f)))
            {
                if (isBgm)
                {
                    if (playingThis) { m_audio->StopBgm(0.3f); }
                    else { m_audio->PlayBgm(def.id, 0.3f, 0.3f); }
                }
                else
                {
                    m_audio->PlaySe(def.id);
                }
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(def.id.c_str());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(isBgm ? "BGM" : "SE");

            ImGui::TableNextColumn();
            int percent = ToPercent(def.volume);
            bool edited = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            edited |= ImGui::InputInt("##num", &percent, 1, 10);

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            edited |= ImGui::SliderInt("##slider", &percent, 0, kMaxSoundPercent);
            if (edited && editable)
            {
                percent = std::clamp(percent, 0, kMaxSoundPercent);
                editable->volume = static_cast<float>(percent) / 100.0f;
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(def.file.c_str());
            std::string attributes;
            if (!def.tag.empty()) { attributes += "tag:" + def.tag + "  "; }
            if (def.loop) { attributes += "ループ  "; }
            if (def.stream) { attributes += "ストリーミング  "; }
            if (def.spatial) { attributes += "距離で減衰  "; }
            if (!attributes.empty())
            {
                ImGui::TextDisabled("%s", attributes.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // ---- 保存 ----
    const bool dirty = HasUnsavedChanges();
    if (ImGui::Button("音の表に保存"))
    {
        std::string message;
        if (table.SaveToFile(m_tablePath, message))
        {
            RememberSavedVolumes();
        }
        m_status = message;
    }
    ImGui::SameLine();
    if (dirty)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "未保存の変更があります");
    }
    else
    {
        ImGui::TextDisabled("保存済み");
    }
    if (!m_status.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_status.c_str());
    }

    ImGui::End();
}
