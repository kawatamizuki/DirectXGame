// DemandSystem の、Editorのデバッグパネル(「City Stats」)。住民・施設・設定・施設選びの記録を、実行中に見て、調整できる。
// ゲームの動作には関わらない(見る・設定を変える)。
#include <cstdio>
#include "DemandSystem.h"
#include "FacilityData.h"
#include "imgui.h"

namespace
{
    const char* StateName(ResidentAgentState state)
    {
        switch (state)
        {
        case ResidentAgentState::AtHome:        return "AtHome";
        case ResidentAgentState::GoingOut:      return "GoingOut";
        case ResidentAgentState::Visiting:      return "Visiting";
        case ResidentAgentState::ReturningHome: return "ReturningHome";
        case ResidentAgentState::Homeless:      return "Homeless";
        default:                                return "(unused)";
        }
    }

    const char* RejectName(CandidateReject reject)
    {
        switch (reject)
        {
        case CandidateReject::None:             return "-";
        case CandidateReject::Unreachable:      return "到達不可";
        case CandidateReject::NotEnoughStamina: return "体力不足";
        case CandidateReject::WrongCategory:    return "カテゴリ違い";
        }
        return "?";
    }

    const char* FacilityNameOf(BuildingType type)
    {
        const FacilityDefinition* definition = FindFacilityDefinition(type);
        return definition ? definition->name : GetBuildingDefinition(type).name;
    }
}

void DemandSystem::DrawDebugUI(bool* open)
{
    if (open && !*open)
    {
        return;
    }

    // Debug/Hierarchy/Inspector/Editor Settings(既存パネル)と重ならない中央上部に配置する
    ImGui::SetNextWindowPos(ImVec2(310.0f, 200.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 460.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("City Stats", open))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("##citystats"))
    {
        if (ImGui::BeginTabItem("Summary"))
        {
            const DemandStats stats = GetStats();
            ImGui::Text("Week #%d  (weeks ended: %d)", m_lastWeekCount, m_weeksEnded);
            ImGui::Separator();
            ImGui::Text("Houses: %d    Facilities: %d    (Offices: %d, unused)", stats.houseCount, stats.facilityCount, stats.officeCount);
            ImGui::Text("Population: %d    Residents: %d", stats.populationTotal, stats.agentCount);
            ImGui::Text("Residents with unmet need: %d", stats.unmetDemand);
            ImGui::Separator();
            ImGui::Text("This week : %d visits, %d yen", stats.visitsThisWeek, stats.salesThisWeek);
            ImGui::Text("Last week : %d visits, %d yen", stats.visitsLastWeek, stats.salesLastWeek);
            ImGui::Text("This month: %d visits, %d yen", stats.visitsThisMonth, stats.salesThisMonth);
            const FacilityTally total = m_ledger.SumTotal();
            ImGui::Text("Total     : %d visits, %d yen", total.visits, total.sales);
            ImGui::TextDisabled("Sales share to funds: %.0f%%", m_settings.salesShareToFunds * 100.0f);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Facilities"))
        {
            DrawFacilitiesTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Residents"))
        {
            DrawResidentsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Decisions"))
        {
            DrawDecisionsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings"))
        {
            DrawSettingsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void DemandSystem::DrawFacilitiesTab()
{
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("##facilities", 8, flags, ImVec2(0.0f, 0.0f)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#id");
        ImGui::TableSetupColumn("type");
        ImGui::TableSetupColumn("wk visits");
        ImGui::TableSetupColumn("wk sales");
        ImGui::TableSetupColumn("last wk");
        ImGui::TableSetupColumn("month");
        ImGui::TableSetupColumn("total visits");
        ImGui::TableSetupColumn("total sales");
        ImGui::TableHeadersRow();
        for (const FacilityRecord& record : m_ledger.GetRecords())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (record.demolished) { ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%u (demolished)", record.objectId); } else { ImGui::Text("%u", record.objectId); }
            ImGui::TableNextColumn(); ImGui::TextUnformatted(FacilityNameOf(record.type));
            ImGui::TableNextColumn(); ImGui::Text("%d", record.thisWeek.visits);
            ImGui::TableNextColumn(); ImGui::Text("%d", record.thisWeek.sales);
            ImGui::TableNextColumn(); ImGui::Text("%d / %d", record.lastWeek.visits, record.lastWeek.sales);
            ImGui::TableNextColumn(); ImGui::Text("%d / %d", record.thisMonth.visits, record.thisMonth.sales);
            ImGui::TableNextColumn(); ImGui::Text("%d", record.total.visits);
            ImGui::TableNextColumn(); ImGui::Text("%d", record.total.sales);
        }
        ImGui::EndTable();
    }
    if (m_ledger.GetRecords().empty())
    {
        ImGui::TextDisabled("まだ、利用された施設がありません");
    }
}

void DemandSystem::DrawResidentsTab()
{
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("##residents", 8, flags, ImVec2(0.0f, 0.0f)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("home");
        ImGui::TableSetupColumn("state");
        ImGui::TableSetupColumn("life");
        ImGui::TableSetupColumn("activity");
        ImGui::TableSetupColumn("stamina");
        ImGui::TableSetupColumn("pen");
        ImGui::TableSetupColumn("traits (q/p/d/s/decay)");
        ImGui::TableSetupColumn("last decision");
        ImGui::TableHeadersRow();
        for (const ResidentAgent& agent : m_agents)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (agent.homeObjectId == 0) { ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "(none)"); } else { ImGui::Text("#%u", agent.homeObjectId); }
            ImGui::TableNextColumn(); ImGui::TextUnformatted(StateName(agent.state));
            ImGui::TableNextColumn();
            if (IsLifeNeedUnmet(agent, m_settings)) { ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%.0f", agent.lifeNeed); } else { ImGui::Text("%.0f", agent.lifeNeed); }
            ImGui::TableNextColumn();
            if (IsActivityNeedUnmet(agent, m_settings)) { ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%.0f", agent.activityNeed); } else { ImGui::Text("%.0f", agent.activityNeed); }
            ImGui::TableNextColumn(); ImGui::Text("%.0f/%.0f", agent.stamina, EffectiveStaminaCap(agent, m_settings));
            ImGui::TableNextColumn(); ImGui::Text("%d", agent.penaltyLevel);
            ImGui::TableNextColumn(); ImGui::Text("%.2f/%.2f/%.2f/%.2f/%.2f", agent.traits.qualityWeight, agent.traits.priceSensitivity,
                agent.traits.distanceTolerance, agent.traits.spontaneity, agent.traits.needDecayScale);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(agent.lastDecisionNote.empty() ? "-" : agent.lastDecisionNote.c_str());
        }
        ImGui::EndTable();
    }
}

void DemandSystem::DrawDecisionsTab()
{
    ImGui::TextDisabled("外出を決めた時の、候補・重み・確率・選ばれたもの(新しい順。最大 %d 件)", m_settings.decisionLogCapacity);
    if (ImGui::Button("Clear")) { m_decisionLog.clear(); }
    ImGui::Separator();
    ImGui::BeginChild("##decisionlist");
    for (int i = static_cast<int>(m_decisionLog.size()) - 1; i >= 0; --i)
    {
        const DecisionLogEntry& entry = m_decisionLog[static_cast<size_t>(i)];
        char title[160];
        std::snprintf(title, sizeof(title), "week %d %05.2fh  home #%u  %s / %s (%s)  %s###decision%d",
            entry.weekCount, entry.hour, entry.homeObjectId,
            entry.needDriven ? "need" : "spontaneous", FacilityCategoryName(entry.category), UsagePurposeName(entry.purpose),
            entry.chosenIndex >= 0 ? "" : "[no pick]", i);
        if (ImGui::TreeNode(title))
        {
            ImGui::Text("life %.0f  activity %.0f  stamina %.0f", entry.lifeNeed, entry.activityNeed, entry.stamina);
            if (ImGui::BeginTable("##candidates", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("facility");
                ImGui::TableSetupColumn("path");
                ImGui::TableSetupColumn("weight");
                ImGui::TableSetupColumn("chance");
                ImGui::TableSetupColumn("excluded");
                ImGui::TableSetupColumn("picked");
                ImGui::TableHeadersRow();
                for (const CandidateEvaluation& evaluation : entry.evaluations)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("#%u %s", evaluation.candidate.objectId, FacilityNameOf(evaluation.candidate.type));
                    ImGui::TableNextColumn();
                    if (evaluation.candidate.reachable) { ImGui::Text("%.1f", evaluation.candidate.pathLength); } else { ImGui::TextDisabled("-"); }
                    ImGui::TableNextColumn(); ImGui::Text("%.4f", evaluation.weight);
                    ImGui::TableNextColumn(); ImGui::Text("%.1f%%", evaluation.chance * 100.0f);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(RejectName(evaluation.reject));
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(evaluation.chosen ? "YES" : "");
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
}

void DemandSystem::DrawSettingsTab()
{
    ResidentSimSettings& s = m_settings;
    ImGui::TextDisabled("仮の数値。ここで変えると、すぐ反映される(保存はしない。初期値は ResidentSimSettings.h)");

    if (ImGui::CollapsingHeader("乱数", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int seed = static_cast<int>(s.seed);
        if (ImGui::InputInt("seed", &seed)) { s.seed = static_cast<uint32_t>(seed); }
        ImGui::SameLine();
        if (ImGui::Button("Reseed")) { m_random.Seed(s.seed); }
    }
    if (ImGui::CollapsingHeader("需要"))
    {
        ImGui::SliderFloat("充足のしきい値", &s.needSatisfiedThreshold, 0.0f, 100.0f, "%.0f");
        ImGui::SliderFloat("週末に減る(生活)", &s.weeklyDecayLife, 0.0f, 100.0f, "%.0f");
        ImGui::SliderFloat("週末に減る(アクティビティ)", &s.weeklyDecayActivity, 0.0f, 100.0f, "%.0f");
        ImGui::TextDisabled("初期の需要の範囲は、新しく生まれる住民から反映");
        ImGui::SliderFloat("初期の需要(最小)", &s.initialNeedMin, 0.0f, 100.0f, "%.0f");
        ImGui::SliderFloat("初期の需要(最大)", &s.initialNeedMax, 0.0f, 100.0f, "%.0f");
    }
    if (ImGui::CollapsingHeader("外出"))
    {
        ImGui::SliderFloat("不足時の外出確率(/判定)", &s.needDrivenChancePerTick, 0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat("自発の外出確率(/判定)", &s.spontaneousChancePerTick, 0.0f, 0.5f, "%.3f");
        ImGui::SliderFloat("自発に必要な体力", &s.spontaneousMinStamina, 0.0f, 100.0f, "%.0f");
        ImGui::SliderFloat("これ以降は新しく外出しない(時)", &s.lastDepartureHour, 6.0f, 22.0f, "%.1f");
    }
    if (ImGui::CollapsingHeader("体力"))
    {
        ImGui::SliderFloat("体力の上限の基準", &s.staminaMax, 20.0f, 200.0f, "%.0f");
        ImGui::SliderFloat("移動1マスの消費", &s.staminaPerLegacyCell, 0.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("家での回復(/時間)", &s.staminaHomeRecoveryPerHour, 0.0f, 20.0f, "%.1f");
        ImGui::SliderFloat("就寝での回復", &s.staminaSleepRecovery, 0.0f, 200.0f, "%.0f");
    }
    if (ImGui::CollapsingHeader("週末のペナルティ"))
    {
        ImGui::SliderFloat("不足の猶予(時間)", &s.unmetGraceHours, 0.0f, 16.0f, "%.1f");
        ImGui::SliderFloat("回復に必要な充足(時間)", &s.recoverSatisfiedHours, 0.0f, 16.0f, "%.1f");
        ImGui::SliderInt("最大の段階", &s.maxPenaltyLevel, 0, 10);
        ImGui::SliderFloat("1段階で下がる体力の上限", &s.penaltyStaminaCapStep, 0.0f, 30.0f, "%.0f");
    }
    if (ImGui::CollapsingHeader("施設の選び方"))
    {
        ImGui::SliderFloat("品質の効き方", &s.qualityExponent, 0.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("値段の基準(円)", &s.priceReferenceYen, 10.0f, 2000.0f, "%.0f");
        ImGui::SliderFloat("値段の効き方", &s.priceStrength, 0.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("距離の目安(マス)", &s.distanceScaleLegacyCells, 1.0f, 60.0f, "%.1f");
        ImGui::SliderFloat("重みの下限", &s.minimumWeight, 0.0f, 0.1f, "%.4f");
    }
    if (ImGui::CollapsingHeader("お金"))
    {
        ImGui::SliderFloat("売上のうち資金に入る割合", &s.salesShareToFunds, 0.0f, 1.0f, "%.2f");
    }
    if (ImGui::CollapsingHeader("施設の表(読み取り専用。FacilityData.cppで変更)"))
    {
        for (int i = 0; i < kBuildingTypeCount; ++i)
        {
            const FacilityDefinition* f = FindFacilityDefinition(static_cast<BuildingType>(i));
            if (!f) { continue; }
            ImGui::Text("%s [%s] 品質%.1f 値段%d 回復(生活%.0f/活動%.0f) 時間%.1fh 体力%.0f  適性(買足%.1f/まとめ%.1f/映画%.1f/軽食%.1f)",
                f->name, FacilityCategoryName(f->category), f->quality, f->pricePerVisit, f->lifeRecovery, f->activityRecovery,
                f->visitDurationHours, f->staminaCost,
                f->purposeSuitability[0], f->purposeSuitability[1], f->purposeSuitability[2], f->purposeSuitability[3]);
        }
    }
}
