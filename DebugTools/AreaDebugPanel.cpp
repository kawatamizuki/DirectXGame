#include "AreaDebugPanel.h"
#include <algorithm>
#include <string>
#include "AreaIndicators.h"
#include "AreaLedger.h"
#include "DemandSystem.h"
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

void AreaDebugPanel::Draw(CommercialAreaManager& areas, const GameClock& clock, const DemandSystem& demand)
{
    const AreaLedger& ledger = demand.GetAreaLedger();
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
        if (const char* conflict = DescribeAreaSettingsConflict(s)) { ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "設定の矛盾: %s", conflict); }
        ImGui::SliderInt("開発の猶予(週)", &s.graceWeeks, 0, 52);
        ImGui::TextDisabled("P2-A では、残りの週を見せるだけ(猶予が過ぎた後の評判の変化は P2-D)");
        ImGui::Separator();
        ImGui::SliderFloat("「強い適性」の元の値(施設構成の評価)", &s.strongSuitability, 0.0f, 3.0f, "%.2f");
        ImGui::TextDisabled("施設データの適性(元の値)と比べる。今の施設は、強い用途が1つずつ(1.6 / 1.6 / 2.0 / 1.8)で、次に高いのは 0.8");
        ImGui::SliderFloat("日用品特化の、日用品寄りの割合", &s.dailyFocusedShare, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("娯楽特化の、日用品寄りの割合", &s.leisureFocusedShare, 0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("ラベルを付けるだけで、専門型・汎用型のどちらも、有利・不利にはしない");
        ImGui::Separator();
        ImGui::TextDisabled("評判(P2-D): 週末に、目標値(1.0 + 計画の履行の項 + 実際の利用の項)へ、少しずつ近づける");
        ImGui::SliderFloat("評判の下限", &s.reputationMin, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("評判の上限", &s.reputationMax, 1.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("1週に下がる量の上限", &s.reputationDropPerWeek, 0.0f, 0.2f, "%.3f");
        ImGui::SliderFloat("1週に回復する量の上限", &s.reputationRecoverPerWeek, 0.0f, 0.2f, "%.3f");
        ImGui::SliderFloat("計画未達の減点(店舗数0の時)", &s.planPenaltyMax, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("計画規模がそろっている時の加点", &s.planCompleteBonus, 0.0f, 0.3f, "%.2f");
        ImGui::SliderInt("再整備の猶予(週。最低進捗を下回ってから)", &s.reorganizeGraceWeeks, 0, 52);
        ImGui::SliderInt("猶予の計測を取り消すのに必要な、連続の週数", &s.recoveryHoldWeeks, 1, 12);
        ImGui::SliderInt("利用評価の最低サンプル数", &s.useMinSamples, 1, 50);
        ImGui::SliderFloat("利用成功率の項の最大(±)", &s.useSuccessWeight, 0.0f, 0.3f, "%.3f");
        ImGui::SliderFloat("利用成功率が0になる値", &s.useSuccessNeutral, 0.1f, 0.99f, "%.2f");
        ImGui::SliderFloat("需要への貢献の項の最大", &s.useNeedWeight, 0.0f, 0.3f, "%.3f");
        ImGui::SliderFloat("需要への貢献が0になる割合", &s.useNeedNeutral, 0.0f, 0.95f, "%.2f");
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
            {
                // 4つの指標(P2-C2)。予測が何を基準にした値かを、内訳つきで見せる。実績の割り算は、分母が0なら「未計測」。
                const AreaIndicators ind = ComputeAreaIndicators(areas.CompositionOf(area.id), area.published, ledger.Find(area.id), demand.GetSettings(), area.reputation);
                auto show = [](const IndicatorValue& v, const char* format) -> std::string
                {
                    if (!v.measured) { return "未計測"; }
                    char text[48]; std::snprintf(text, sizeof(text), format, v.value); return text;
                };
                ImGui::Text("集客力(予測) %.3f = 適合 %.3f × 充実度の魅力 %.3f × 周知の倍率 %.2f × 評判 %.2f", ind.attractionPredicted, ind.predictedFit, ind.predictedCoverage, ind.predictedPublish, ind.predictedReputation);
                ImGui::TextDisabled("  予測の基準: 区画の抽選(ChooseArea)の重みから、距離(住民ごと)と今の目的(1つの用途)を除いた物。適合 = 4用途の「供給量÷頭打ち(下限あり、上限1)」の平均");
                ImGui::Text("集客力(実績) 先週の来訪者 %s 人 / 通算 %s 人", show(ind.visitorsLastWeek, "%.0f").c_str(), show(ind.visitorsTotal, "%.0f").c_str());
                ImGui::Text("回遊性 豊富さ(予測) %.2f(%d軒・用途 %d種類) / 実績: 1人あたりの利用施設数 先週 %s・通算 %s / 区画内の立ち寄り(通算) %d 回", ind.richness, ind.storeCount, ind.coveredPurposes,
                            show(ind.visitsPerVisitorLastWeek, "%.2f").c_str(), show(ind.visitsPerVisitorTotal, "%.2f").c_str(), ind.stopoversTotal);
                ImGui::Text("消費効率 1人あたりの実売上 先週 %s 円・通算 %s 円", show(ind.salesPerVisitorLastWeek, "%.0f").c_str(), show(ind.salesPerVisitorTotal, "%.0f").c_str());
                {
                    // 評判(P2-D)。過去の実績(履歴)・今の進捗・猶予の残りを、区別して見せる。
                    const AreaProgress pr = areas.ProgressOf(area.id);
                    const AreaReputationResult live = EvaluateAreaReputation(area, week, pr.storeCount, AreaUsageWeek{}, s);
                    ImGui::Text("評判 %.3f(目標 %.3f / 直近の変化 %+.3f)", area.reputation, area.lastReputationTarget, area.lastReputationDelta);
                    ImGui::Text("  直近の更新: 計画の項 %+.3f / 利用の項 %+.3f(状態: %s)", area.lastPlanTerm, area.lastUseTerm, AreaPlanStateName(static_cast<AreaPlanState>(area.lastPlanState)));
                    ImGui::Text("  いま: %s / 店舗 %d(最低 %d) / 過去の最高到達(履歴) %s / 今の段階 %s", AreaPlanStateName(live.planState), pr.storeCount, s.minProgressStores,
                                AreaMilestoneName(area.achieved), AreaMilestoneName(pr.current));
                    if (area.published)
                    {
                        ImGui::Text("  初回の開発猶予の残り %d 週 / 再整備の猶予: %s(下回り始めた週 %d、最低進捗を連続で保った週 %d)",
                                    live.initialGraceLeft > 0 ? live.initialGraceLeft : 0,
                                    live.reorganizeGraceLeft > 0 ? (std::to_string(live.reorganizeGraceLeft) + " 週").c_str() : (area.belowMinimumSinceWeek > 0 ? "終了(または対象外)" : "なし"),
                                    area.belowMinimumSinceWeek, area.metMinimumStreakWeeks);
                    }
                }
                if (!ind.consistent) { ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "集計の不整合: 来訪者 > 延べ利用、または 目的の来訪者 > 来訪者(重複して数えている疑い)"); }
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

    // 立ち寄りの判定の集計(P2-C2。診断用。保存しない・世界を読み込み直すとリセット)。補正の効き方を、放置して確かめる。
    if (ImGui::CollapsingHeader("立ち寄りの補正の効き方(P2-C2の診断)"))
    {
        const StopoverTally& t = demand.GetStopoverTally();
        ImGui::Text("確率の抽選 %llu 回 / 当選 %llu 回(%.1f%%)", static_cast<unsigned long long>(t.rolls), static_cast<unsigned long long>(t.hits), t.rolls ? 100.0 * static_cast<double>(t.hits) / static_cast<double>(t.rolls) : 0.0);
        ImGui::Text("期待される当選 %.1f 回(引き寄せ込み) / 引き寄せが無かった場合 %.1f 回 → 補正で増えた期待 %+.1f 回(%+.1f%%)", t.sumChance, t.sumChanceWithoutPull, t.sumChance - t.sumChanceWithoutPull,
                    t.sumChanceWithoutPull > 0.0 ? 100.0 * (t.sumChance / t.sumChanceWithoutPull - 1.0) : 0.0);
        ImGui::Text("引き寄せの平均 %.3f", t.rolls ? t.sumPull / static_cast<double>(t.rolls) : 0.0);
        ImGui::Text("立ち寄りを始めた %llu 回: 同じ用途の連続 %llu 回(%.1f%%) / 区画の施設から寄り始めた %llu 回、うち同じ区画の施設へ %llu 回(%.1f%%)",
                    static_cast<unsigned long long>(t.started), static_cast<unsigned long long>(t.startedSamePurpose), t.started ? 100.0 * static_cast<double>(t.startedSamePurpose) / static_cast<double>(t.started) : 0.0,
                    static_cast<unsigned long long>(t.startedFromArea), static_cast<unsigned long long>(t.startedSameArea), t.startedFromArea ? 100.0 * static_cast<double>(t.startedSameArea) / static_cast<double>(t.startedFromArea) : 0.0);
        ImGui::Text("寄り先の補完性の倍率の平均 %.3f / 補正(補完性×エリア内、頭打ち後)の平均 %.3f", t.started ? t.sumComplement / static_cast<double>(t.started) : 0.0, t.started ? t.sumAffinity / static_cast<double>(t.started) : 0.0);
    }

    // 区画ごとの、来訪・利用・売上の集計(P2-C1)。「目的にした人数」と「実際に使った人数」は、別の数字。差=エリアに向かったが、使わなかった人。
    if (ImGui::CollapsingHeader("区画ごとの集計(来訪・利用・売上)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled("目的にした人数=理由が「エリア」で、その区画へ出発した人 / 来訪者=区画の施設を実際に使った人(目的が何でも。同じ外出で同じ区画は1人)\n"
                            "目的の来訪者=来訪者のうち、その区画が目的だった人 / 延べ利用=施設を使った回数 / 売上=施設の利用が起きた時の値段の合計(利用が無ければ増えない)\n"
                            "週=今の週、先週=直前の週、月=今の月、通算=全期間。人数は、最初の利用が起きた週で数える。区画を消しても、記録は残る");
        const std::vector<AreaRecord>& records = ledger.GetRecords();
        if (records.empty())
        {
            ImGui::TextDisabled("まだ集計がありません(エリアを目的にした外出も、区画の施設の利用も、起きていません)");
        }
        else if (ImGui::BeginTable("##areaLedger", 15, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("区画");
            ImGui::TableSetupColumn("期間");
            ImGui::TableSetupColumn("目的にした人数");
            ImGui::TableSetupColumn("来訪者");
            ImGui::TableSetupColumn("目的の来訪者");
            ImGui::TableSetupColumn("使えなかった(結果が決まった外出)");
            ImGui::TableSetupColumn("延べ利用");
            ImGui::TableSetupColumn("売上");
            ImGui::TableSetupColumn("1人あたり利用");
            ImGui::TableSetupColumn("1人あたり売上");
            ImGui::TableSetupColumn("需要回復(標準)");
            ImGui::TableSetupColumn("需要回復(実際)");
            ImGui::TableSetupColumn("回復の割合");
            ImGui::TableSetupColumn("状態");
            ImGui::TableSetupColumn("点検");
            ImGui::TableHeadersRow();
            struct Row { const char* label; const AreaTally* tally; };
            for (const AreaRecord& record : records)
            {
                const bool exists = areas.Find(record.areaId) != nullptr;
                const Row rows[] = { { "今週", &record.thisWeek }, { "先週", &record.lastWeek }, { "今月", &record.thisMonth }, { "通算", &record.total } };
                for (const Row& row : rows)
                {
                    const AreaTally& t = *row.tally;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("#%u", record.areaId);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(row.label);
                    ImGui::TableNextColumn(); ImGui::Text("%d", t.areaOutings);
                    ImGui::TableNextColumn(); ImGui::Text("%d", t.visitors);
                    ImGui::TableNextColumn(); ImGui::Text("%d", t.purposeVisitors);
                    // 使えなかった人 = 目的にして出発したが、まだ(または結局)使っていない人。通算でだけ、確定した数になる(外出中の人は、まだ数えない)。
                    ImGui::TableNextColumn(); ImGui::Text("%d", (std::max)(0, t.areaResolved - t.areaSucceeded));
                    ImGui::TableNextColumn(); ImGui::Text("%d", t.visits);
                    ImGui::TableNextColumn(); ImGui::Text("%d", t.sales);
                    ImGui::TableNextColumn(); if (t.visitors > 0) { ImGui::Text("%.2f", static_cast<float>(t.visits) / static_cast<float>(t.visitors)); } else { ImGui::TextDisabled("-"); }
                    ImGui::TableNextColumn(); if (t.visitors > 0) { ImGui::Text("%.1f", static_cast<float>(t.sales) / static_cast<float>(t.visitors)); } else { ImGui::TextDisabled("-"); }
                    ImGui::TableNextColumn(); ImGui::Text("%.0f", t.needNominal);
                    ImGui::TableNextColumn(); ImGui::Text("%.0f", t.needActual);
                    ImGui::TableNextColumn(); if (t.needNominal > 0.0f) { ImGui::Text("%.0f%%", 100.0f * t.needActual / t.needNominal); } else { ImGui::TextDisabled("未計測"); }
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(exists ? "あり" : "削除済み");
                    ImGui::TableNextColumn();
                    // 整合: 来訪者 <= 延べ利用 / 目的の来訪者 <= 来訪者 / (通算以外は、期間をまたぐ外出があるので、ここでは見ない)
                    const bool ok = t.visitors <= t.visits && t.purposeVisitors <= t.visitors;
                    if (ok) { ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "OK"); } else { ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "不整合"); }
                }
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
