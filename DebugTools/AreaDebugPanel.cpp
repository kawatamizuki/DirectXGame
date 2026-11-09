#include "AreaDebugPanel.h"
#include <string>
#include "CommercialAreaManager.h"
#include "GameClock.h"
#include "imgui.h"

namespace
{
    // 施設構成の評価を、表で見せる。「供給量」(適性の元の値の合計)と「割合」(合計1の分布)を、別の列にして、混同しない。
    void DrawCompositionTable(const AreaComposition& c, const char* id)
    {
        ImGui::Text("特色: %s(日用品寄り %.0f%%) / 専門性 %.2f(汎用性 %.2f) / 用途の充実度 %d / %d 種類 / 同種の重なり %d 軒",
                    AreaCharacterName(c.character), c.dailyShare * 100.0f, c.specialization, 1.0f - c.specialization,
                    c.coveredPurposes, kUsagePurposeCount, c.overlapStores);
        if (ImGui::BeginTable(id, 4, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("用途");
            ImGui::TableSetupColumn("供給量(適性の元の値の合計)");
            ImGui::TableSetupColumn("割合(合計1の分布)");
            ImGui::TableSetupColumn("強い店の数");
            ImGui::TableHeadersRow();
            for (int p = 0; p < kUsagePurposeCount; ++p)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(UsagePurposeName(static_cast<UsagePurpose>(p)));
                ImGui::TableNextColumn(); ImGui::Text("%.2f", c.supply[p]);
                ImGui::TableNextColumn(); ImGui::Text("%.0f%%", c.share[p] * 100.0f);
                ImGui::TableNextColumn(); ImGui::Text("%d", c.strongCount[p]);
            }
            ImGui::EndTable();
        }
    }
}

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
        ImGui::Separator();
        ImGui::SliderFloat("「強い適性」の元の値(施設構成の評価)", &s.strongSuitability, 0.0f, 3.0f, "%.2f");
        ImGui::TextDisabled("施設データの適性(元の値)と比べる。今の施設は、強い用途が1つずつ(1.6 / 1.6 / 2.0 / 1.8)で、次に高いのは 0.8");
        ImGui::SliderFloat("日用品特化の、日用品寄りの割合", &s.dailyFocusedShare, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("娯楽特化の、日用品寄りの割合", &s.leisureFocusedShare, 0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("ラベルを付けるだけで、専門型・汎用型のどちらも、有利・不利にはしない");
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
            const std::string tableId = "構成##" + std::to_string(area.id);
            DrawCompositionTable(areas.CompositionOf(area.id), tableId.c_str());
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
    // 構成の比較(P2-B): 施設の種類ごとの軒数を決めると、評価がどう変わるかを、区画を置かずに見られる。見本は、よくある構成。
    if (ImGui::CollapsingHeader("施設構成の比較(軒数を変えて、評価を見る)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static int counts[4] = { 1, 1, 1, 1 };
        static const BuildingType kTypes[4] = { BuildingType::Shop, BuildingType::Supermarket, BuildingType::Cinema, BuildingType::Cafe };
        auto labelOf = [](BuildingType type) { const FacilityDefinition* f = FindFacilityDefinition(type); return f ? f->name : "?"; };
        for (int i = 0; i < 4; ++i)
        {
            ImGui::SliderInt(labelOf(kTypes[i]), &counts[i], 0, 8);
        }
        std::vector<BuildingType> custom;
        for (int i = 0; i < 4; ++i) { for (int n = 0; n < counts[i]; ++n) { custom.push_back(kTypes[i]); } }
        DrawCompositionTable(ComputeAreaComposition(custom, s), "構成の比較##custom");

        ImGui::TextDisabled("見本(同じ割合で店の数が違う構成・偏った構成・全部そろった構成)");
        struct Sample { const char* name; std::vector<BuildingType> types; };
        const BuildingType S = BuildingType::Shop, M = BuildingType::Supermarket, C = BuildingType::Cinema, F = BuildingType::Cafe;
        const Sample samples[] = {
            { "コンビニ+カフェ(2軒)", { S, F } },
            { "コンビニ+カフェ(6軒)", { S, F, S, F, S, F } },
            { "コンビニ×3", { S, S, S } },
            { "コンビニ+スーパー", { S, M } },
            { "映画館+カフェ", { C, F } },
            { "4種類を1軒ずつ", { S, M, C, F } },
            { "4種類を2軒ずつ(8軒)", { S, M, C, F, S, M, C, F } },
        };
        if (ImGui::BeginTable("##samples", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("構成");
            ImGui::TableSetupColumn("店数");
            ImGui::TableSetupColumn("特色");
            ImGui::TableSetupColumn("日用品寄り");
            ImGui::TableSetupColumn("専門性");
            ImGui::TableSetupColumn("充実度");
            ImGui::TableSetupColumn("同種の重なり");
            ImGui::TableHeadersRow();
            for (const Sample& sample : samples)
            {
                const AreaComposition c = ComputeAreaComposition(sample.types, s);
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(sample.name);
                ImGui::TableNextColumn(); ImGui::Text("%d", c.storeCount);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(AreaCharacterName(c.character));
                ImGui::TableNextColumn(); ImGui::Text("%.0f%%", c.dailyShare * 100.0f);
                ImGui::TableNextColumn(); ImGui::Text("%.2f", c.specialization);
                ImGui::TableNextColumn(); ImGui::Text("%d / %d", c.coveredPurposes, kUsagePurposeCount);
                ImGui::TableNextColumn(); ImGui::Text("%d", c.overlapStores);
            }
            ImGui::EndTable();
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
