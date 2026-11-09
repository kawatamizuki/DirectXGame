#include "AreaDebugPanel.h"
#include <string>
#include "CommercialAreaManager.h"
#include "GameClock.h"
#include "imgui.h"

void AreaDebugPanel::Draw(CommercialAreaManager& areas, const GameClock& clock)
{
    if (m_open && !*m_open)
    {
        return;
    }
    if (!ImGui::Begin("商業エリア", m_open))
    {
        ImGui::End();
        return;
    }
    CommercialAreaSettings& s = areas.Settings();
    ImGui::TextDisabled("仮の数値。ここで変えると、すぐ反映される(保存はしない。初期値は CommercialArea.h)");

    if (ImGui::CollapsingHeader("設定", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("所属に必要な、敷地の重なり", &s.membershipMinOverlap, 0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("建物の敷地面積のうち、区画の中にある割合がこれ以上なら所属(中心の位置では判定しない)。1軒は、1つの区画にだけ所属");
        ImGui::SliderFloat("区画の一辺(マス)", &s.sideLegacyCells, 2.0f, 30.0f, "%.0f");
        ImGui::TextDisabled("これから置く区画の大きさ。置いた後の区画は、変わらない");
        ImGui::SliderInt("計画規模(店舗数)", &s.plannedCapacity, 1, 30);
        ImGui::TextDisabled("周知した時点の値で、その区画に固定される");
        ImGui::SliderInt("最低進捗の店舗数", &s.minProgressStores, 0, 30);
        ImGui::SliderInt("来訪範囲の拡大の店舗数", &s.expandedRangeStores, 0, 30);
        ImGui::SliderInt("開発の猶予(週)", &s.graceWeeks, 0, 52);
        ImGui::TextDisabled("P2-A では、残りの週を見せるだけ(猶予が過ぎた後の評判の変化は P2-D)");
    }

    const std::vector<const char*> problems = areas.Validate();
    if (problems.empty())
    {
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "点検: 問題なし(区画の重なり・ID・二重所属)");
    }
    else
    {
        for (const char* text : problems)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "点検: %s", text);
        }
    }

    const int week = clock.GetWeekCount();
    ImGui::Text("区画 %d 個 / 現在 第%d週", static_cast<int>(areas.Areas().size()), week);
    for (const CommercialArea& area : areas.Areas())
    {
        const AreaProgress progress = areas.ProgressOf(area.id);
        const std::string header = "区画 #" + std::to_string(area.id) + (area.published ? "  [周知済み]" : "  [非公開]")
            + "  店舗 " + std::to_string(progress.storeCount) + "/" + std::to_string(progress.plannedCapacity);
        if (ImGui::TreeNode(header.c_str()))
        {
            ImGui::Text("矩形: X %.2f〜%.2f / Z %.2f〜%.2f(一辺 %.2f)", area.rect.minX, area.rect.maxX, area.rect.minZ, area.rect.maxZ, area.rect.Width());
            ImGui::Text("今の段階: %s / 達成した最高の段階: %s", AreaMilestoneName(progress.current), AreaMilestoneName(area.achieved));
            ImGui::Text("計画達成率: %.0f%%(店の数だけ。集客力・売上とは別)", progress.ratio * 100.0f);
            if (area.published)
            {
                const int left = GraceWeeksLeft(area, week, s);
                ImGui::Text("周知した週: 第%d週 / 猶予の残り: %d週 / 計画規模(周知時): %d", area.publishedWeek, left, area.plannedCapacity);
            }
            const AreaMembers* members = areas.MembersOf(area.id);
            if (members && !members->facilityIds.empty())
            {
                ImGui::Text("所属する施設(敷地のうち、区画の中にある割合):");
                for (size_t i = 0; i < members->facilityIds.size(); ++i)
                {
                    ImGui::BulletText("施設 #%u  %.0f%%", members->facilityIds[i], members->overlapRatios[i] * 100.0f);
                }
            }
            else
            {
                ImGui::TextDisabled("所属する施設は、ありません");
            }
            for (const AreaHistoryEntry& entry : area.history)
            {
                ImGui::BulletText("履歴: 第%d週 %s", entry.week, AreaHistoryKindName(entry.kind));
            }
            ImGui::TreePop();
        }
    }
    if (!areas.Withdrawn().empty() && ImGui::CollapsingHeader("撤回した周知済みの区画(記録)"))
    {
        for (const WithdrawnAreaRecord& record : areas.Withdrawn())
        {
            ImGui::BulletText("区画 #%u: 第%d週に周知 → 第%d週に撤回 / 計画規模 %d / 達成 %s", record.areaId, record.publishedWeek, record.withdrawnWeek,
                              record.plannedCapacity, AreaMilestoneName(record.achieved));
        }
    }
    ImGui::End();
}
