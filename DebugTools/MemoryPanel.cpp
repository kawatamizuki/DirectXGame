#include "MemoryPanel.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <windows.h>
#include <psapi.h>
#include "imgui.h"

#pragma comment(lib, "psapi.lib")

namespace
{
    const ImVec4 kColorHeading(0.55f, 0.80f, 1.00f, 1.0f);
    const ImVec4 kColorWarning(1.00f, 0.85f, 0.30f, 1.0f);
    const ImVec4 kColorGood(0.45f, 0.90f, 0.50f, 1.0f);
    const ImVec4 kColorBad(1.00f, 0.45f, 0.40f, 1.0f);
    const ImVec4 kColorNote(0.62f, 0.62f, 0.62f, 1.0f);

    void Heading(const char* text)
    {
        ImGui::Spacing();
        ImGui::TextColored(kColorHeading, "%s", text);
        ImGui::Separator();
    }

    void Note(const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kColorNote);
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    std::string Size(size_t bytes)
    {
        char buffer[48];
        const double kb = static_cast<double>(bytes) / 1024.0;
        if (kb < 1024.0)
        {
            std::snprintf(buffer, sizeof(buffer), "%.1f KB", kb);
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "%.2f MB", kb / 1024.0);
        }
        return buffer;
    }

    // 上限に対する使用率の色(80%以上=黄、95%以上=赤)。
    ImVec4 UsageColor(float ratio)
    {
        if (ratio >= 0.95f) { return kColorBad; }
        if (ratio >= 0.80f) { return kColorWarning; }
        return kColorGood;
    }

    const char* GroupName(MemoryGroup group)
    {
        return group == MemoryGroup::Game ? "ゲーム本体" : "デバッグ・検証の機能";
    }
}

void MemoryPanel::Draw(const std::function<MemoryReport()>& provider)
{
    if (m_open && !*m_open)
    {
        return;
    }
    if (!ImGui::Begin("メモリ", m_open))
    {
        ImGui::End();
        return;
    }

    // 約1秒ごとに、計測する(毎フレーム、全ての機能から集めない)。
    const double now = ImGui::GetTime();
    if (now - m_lastSampleTime >= 1.0)
    {
        m_lastSampleTime = now;
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
        {
            m_workingSetMB = static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
            m_privateMB = static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
            m_peakMB = static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0);
        }
        if (provider)
        {
            m_report = provider();
        }
        Sample sample;
        sample.workingSetMB = static_cast<float>(m_workingSetMB);
        sample.gameMB = static_cast<float>(m_report.gameBytes) / (1024.0f * 1024.0f);
        sample.debugMB = static_cast<float>(m_report.debugBytes) / (1024.0f * 1024.0f);
        m_history.push_back(sample);
        while (m_history.size() > kHistory)
        {
            m_history.pop_front();
        }
    }

    Note("このゲームのメモリ使用量と、その内訳です(ゲーム本体 / デバッグ・検証の機能だけ)。内訳は「要素の数 × 1件の大きさ」の概算で、OSが実際に確保した量の正確な計測ではありません。"
         "規模が大きくなった時に、どこが増えているか・上限に近いか、に気づくために使います。約1秒ごとに更新します。");

    // ---- 1. プロセス全体 ----
    Heading("1. プロセス全体");
    ImGui::Text("ワーキングセット(実際に使っているメモリ): %.1f MB     プライベート(確保している量): %.1f MB     これまでの最大: %.1f MB", m_workingSetMB, m_privateMB, m_peakMB);

    // 増え方(直近5分の、1分あたり)。
    if (m_history.size() < 60)
    {
        ImGui::TextDisabled("増え方: 計測中(あと %d 秒。起動直後は、読み込みで増えるので、1分以上たってから見ます)", static_cast<int>(60 - m_history.size()));
    }
    else
    {
        const size_t span = (std::min)(m_history.size() - 1, static_cast<size_t>(300));
        const Sample& oldest = m_history[m_history.size() - 1 - span];
        const Sample& latest = m_history.back();
        const double minutes = static_cast<double>(span) / 60.0;
        const double growth = (latest.workingSetMB - oldest.workingSetMB) / minutes;
        const double debugGrowth = (latest.debugMB - oldest.debugMB) / minutes;
        const double gameGrowth = (latest.gameMB - oldest.gameMB) / minutes;
        ImGui::Text("直近 %.1f 分の増え方(1分あたり): 全体 %+.2f MB / ゲーム本体 %+.2f MB / デバッグ・検証 %+.2f MB", minutes, growth, gameGrowth, debugGrowth);
        if (growth > 5.0 && span >= 120)
        {
            ImGui::TextColored(kColorWarning, "全体が、1分に %.1f MB のペースで増えています。増え続けるなら、どこかに上限・解放が要ります(下の内訳で、増えている項目を探してください)。", growth);
        }
    }
    if (!m_history.empty())
    {
        std::vector<float> working, game, debug;
        for (const Sample& sample : m_history)
        {
            working.push_back(sample.workingSetMB);
            game.push_back(sample.gameMB);
            debug.push_back(sample.debugMB);
        }
        ImGui::PlotLines("##ws", working.data(), static_cast<int>(working.size()), 0, "全体(ワーキングセット) MB", 0.0f, FLT_MAX, ImVec2(-1.0f, 60.0f));
        ImGui::PlotLines("##game", game.data(), static_cast<int>(game.size()), 0, "ゲーム本体(内訳の合計) MB", 0.0f, FLT_MAX, ImVec2(-1.0f, 40.0f));
        ImGui::PlotLines("##debug", debug.data(), static_cast<int>(debug.size()), 0, "デバッグ・検証(内訳の合計) MB", 0.0f, FLT_MAX, ImVec2(-1.0f, 40.0f));
    }

    // ---- 2. 内訳 ----
    Heading("2. 内訳(概算)");
    ImGui::SetNextItemWidth(160.0f);
    static const char* const kSortLabels[] = { "グループ順", "大きい順" };
    ImGui::Combo("並び", &m_sortMode, kSortLabels, 2);

    std::vector<const MemoryItem*> order;
    for (const MemoryItem& item : m_report.items)
    {
        order.push_back(&item);
    }
    if (m_sortMode == 1)
    {
        std::stable_sort(order.begin(), order.end(), [](const MemoryItem* a, const MemoryItem* b) { return a->bytes > b->bytes; });
    }

    const double processBytes = (std::max)(1.0, m_workingSetMB * 1024.0 * 1024.0);
    std::vector<std::string> warnings;
    if (ImGui::BeginTable("##memory", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthStretch, 2.6f);
        ImGui::TableSetupColumn("使用量", ImGuiTableColumnFlags_WidthFixed, 95.0f);
        ImGui::TableSetupColumn("全体に対する割合", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("上限に対する使用率", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("内訳・説明", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("調整する所", ImGuiTableColumnFlags_WidthStretch, 2.6f);
        ImGui::TableHeadersRow();
        for (const MemoryItem* item : order)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(item->group == MemoryGroup::Game ? ImVec4(0.75f, 0.90f, 1.0f, 1.0f) : ImVec4(1.0f, 0.88f, 0.65f, 1.0f), "[%s]", item->group == MemoryGroup::Game ? "本体" : "デバッグ");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", item->name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", Size(item->bytes).c_str(), item->gpu ? "(GPU)" : "");
            ImGui::TableNextColumn();
            if (item->gpu)
            {
                ImGui::TextDisabled("(GPU。別枠)");
            }
            else
            {
                const float ratio = static_cast<float>(static_cast<double>(item->bytes) / processBytes);
                char label[32];
                std::snprintf(label, sizeof(label), "%.1f%%", ratio * 100.0f);
                ImGui::ProgressBar((std::min)(1.0f, ratio), ImVec2(-1.0f, 0.0f), label);
            }
            ImGui::TableNextColumn();
            if (item->limitBytes > 0)
            {
                const float usage = static_cast<float>(static_cast<double>(item->bytes) / static_cast<double>(item->limitBytes));
                char label[48];
                std::snprintf(label, sizeof(label), "%.0f%% (上限 %s)", usage * 100.0f, Size(item->limitBytes).c_str());
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, UsageColor(usage));
                ImGui::ProgressBar((std::min)(1.0f, usage), ImVec2(-1.0f, 0.0f), label);
                ImGui::PopStyleColor();
                if (usage >= 0.80f)
                {
                    warnings.push_back(item->name + ": 上限の " + std::to_string(static_cast<int>(usage * 100.0f)) + "% を使っています(" + item->limitNote + ")");
                }
            }
            else
            {
                ImGui::TextDisabled("上限なし");
            }
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", item->detail.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", item->limitNote.c_str());
        }
        ImGui::EndTable();
    }

    // ---- 3. 合計 ----
    Heading("3. 合計");
    const double gameMB = static_cast<double>(m_report.gameBytes) / (1024.0 * 1024.0);
    const double debugMB = static_cast<double>(m_report.debugBytes) / (1024.0 * 1024.0);
    const double gpuMB = static_cast<double>(m_report.gpuBytes) / (1024.0 * 1024.0);
    const double unknownMB = (std::max)(0.0, m_workingSetMB - gameMB - debugMB);
    ImGui::Text("ゲーム本体: %.2f MB(全体の %.1f%%)", gameMB, gameMB / (std::max)(1.0, m_workingSetMB) * 100.0);
    ImGui::Text("デバッグ・検証の機能: %.2f MB(全体の %.1f%%)", debugMB, debugMB / (std::max)(1.0, m_workingSetMB) * 100.0);
    ImGui::Text("把握できていない分: %.1f MB(全体の %.1f%%)", unknownMB, unknownMB / (std::max)(1.0, m_workingSetMB) * 100.0);
    ImGui::TextDisabled("  = 全体 − 上の2つ。ランタイム・DLL・ImGui・テクスチャ・GPUドライバ・OSの管理など、このパネルが数えていない物です。");
    ImGui::Text("参考(GPU側。全体には含まれません): %.2f MB", gpuMB);

    // ---- 4. 警告 ----
    Heading("4. 注意が要る所");
    if (warnings.empty())
    {
        ImGui::TextColored(kColorGood, "上限に近い項目は、ありません。");
    }
    else
    {
        for (const std::string& warning : warnings)
        {
            ImGui::TextColored(kColorWarning, "・%s", warning.c_str());
        }
    }
    Note("「上限に対する使用率」は、上限(件数・メモリ)まで使った時の使用量に対する割合です。上限に達すると、古い物から捨てる機能(記録・タイムラインなど)は、遡れる範囲が短くなります。"
         "街が大きくなると、1件あたりが大きくなる項目(タイムラインのキーフレーム・記録のイベント)や、件数が増える項目(住民・施設ごとの統計)が、増えます。");

    ImGui::End();
}
