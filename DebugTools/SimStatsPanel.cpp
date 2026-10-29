#include "SimStatsPanel.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include "imgui.h"
#include "DemandSystem.h"
#include "GameObject.h"
#include "GameObjectFactory.h"
#include "FacilityData.h"
#include "BuildingType.h"

namespace
{
    const ImVec4 kColorHeading(0.55f, 0.80f, 1.00f, 1.0f);
    const ImVec4 kColorWarning(1.00f, 0.85f, 0.30f, 1.0f);
    const ImVec4 kColorNote(0.62f, 0.62f, 0.62f, 1.0f);
    const ImVec4 kColorGood(0.45f, 0.90f, 0.50f, 1.0f);

    std::string Fmt(const char* format, ...)
    {
        char buffer[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        return buffer;
    }

    void Heading(const char* text)
    {
        ImGui::Spacing();
        ImGui::TextColored(kColorHeading, "%s", text);
        ImGui::Separator();
    }

    // 薄い色の説明を、ウィンドウの幅で折り返して出す。
    void Note(const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kColorNote);
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    // 理由の日本語名(統計の行に出す短い名前)。
    const char* ReasonJa(SimReason reason)
    {
        switch (reason)
        {
        case SimReason::HomeNoRoad:             return "自宅の近くに道路が無い";
        case SimReason::NoFacilityInCategory:   return "目的のカテゴリの施設が無い";
        case SimReason::NoPickableCandidate:    return "候補は有ったが全て除外された";
        case SimReason::StopNoHome:             return "帰る家が無い";
        case SimReason::StopMaxReached:         return "立ち寄りの回数が上限";
        case SimReason::StopTooLate:            return "時刻が遅い";
        case SimReason::StopLowStamina:         return "体力が足りない";
        case SimReason::StopRollMiss:           return "確率の抽選に外れた";
        case SimReason::StopHomeMissing:        return "当選したが、帰る家が見つからない";
        case SimReason::StopNoRoad:             return "当選したが、道路が無い";
        case SimReason::StopNoCandidates:       return "当選したが、同じカテゴリの他の施設が無い";
        case SimReason::StopNoPickable:         return "当選したが、候補が全て除外された";
        case SimReason::StopFoodChain:          return "当選したが、食事系が続くので取りやめた";
        case SimReason::StopStarted:            return "立ち寄りを始めた";
        default:                                return "?";
        }
    }

    const char* RejectJa(CandidateReject reject)
    {
        switch (reject)
        {
        case CandidateReject::Unreachable:       return "到達不可";
        case CandidateReject::NotEnoughStamina:  return "体力不足";
        case CandidateReject::WrongCategory:     return "カテゴリ違い";
        case CandidateReject::VisitedThisOuting: return "使用済み";
        case CandidateReject::OutOfRange:        return "範囲外";
        default:                                 return "-";
        }
    }

    // 件数の表の1行。
    struct StatRow
    {
        std::string label;
        uint64_t count = 0;
        uint64_t denominator = 0;       // 率の分母(0=率を出さない)
        int indent = 0;                 // 字下げ(0=親、1=内訳)
        std::string note;
        bool money = false;             // 件数ではなく、金額(円)
        double value = 0.0;             // money の時の値
        bool isValue = false;           // 件数ではなく、実数の値(平均など)
    };

    uint64_t Reason(const std::array<uint64_t, kSimReasonCount>& table, SimReason reason)
    {
        return table[static_cast<size_t>(reason)];
    }

    std::vector<StatRow> BuildOutingRows(const SimStatBucket& b)
    {
        std::vector<StatRow> rows;
        auto add = [&](const char* label, uint64_t count, uint64_t denominator, int indent = 0, const char* note = "")
        {
            StatRow row; row.label = label; row.count = count; row.denominator = denominator; row.indent = indent; row.note = note;
            rows.push_back(row);
        };
        const uint64_t started = b.outingStarted;
        add("外出を考え始めた(外出判定に当選)", started, 0, 0, "以降の率の分母(行動テストで強制した当選は「テスト」の範囲)");
        add("目的を決めた", b.purposeChosen, started);
        add("主目的の施設を選べた", b.mainSelectOk, started);
        add("出られなかった(最初の不成立の理由。重複なし)", b.outingFailed, started);
        add(ReasonJa(SimReason::HomeNoRoad), Reason(b.outingFailReason, SimReason::HomeNoRoad), started, 1);
        add(ReasonJa(SimReason::NoFacilityInCategory), Reason(b.outingFailReason, SimReason::NoFacilityInCategory), started, 1);
        add(ReasonJa(SimReason::NoPickableCandidate), Reason(b.outingFailReason, SimReason::NoPickableCandidate), started, 1, "到達不可・体力不足などで、全て除外");
        add("施設へ歩き始めた(出発)", b.departed, started);
        add("施設に着いた(主目的)", b.arrived, b.departed);
        add("主目的の利用が終わった", b.mainVisits, b.arrived);
        add("家に着いた(外出が終わった)", b.returnedHome, started, 0, "進行中の外出・立ち往生からの帰宅も含むので、100%にならないことがある");
        add("行き先が解体されて中断", b.aborted, started);
        add("立ち往生になった", b.stranded, started);
        add("立ち往生から、帰り始めた", b.strandedRecovered, b.stranded, 1);
        add("迂回して帰った", b.rerouted, started);
        add("住居なしになった", b.homeless, started);
        return rows;
    }

    std::vector<StatRow> BuildStopoverRows(const SimStatBucket& b)
    {
        std::vector<StatRow> rows;
        auto add = [&](const char* label, uint64_t count, uint64_t denominator, int indent = 0, const char* note = "")
        {
            StatRow row; row.label = label; row.count = count; row.denominator = denominator; row.indent = indent; row.note = note;
            rows.push_back(row);
        };
        const uint64_t considered = b.stopConsidered;
        add("立ち寄りを考えた(利用完了ごとの判定)", considered, 0, 0, "下の結果は、どれか1つだけに数える(合計 = この回数)");
        for (SimReason reason : { SimReason::StopNoHome, SimReason::StopMaxReached, SimReason::StopTooLate, SimReason::StopLowStamina, SimReason::StopRollMiss,
                                  SimReason::StopHomeMissing, SimReason::StopNoRoad, SimReason::StopNoCandidates, SimReason::StopNoPickable, SimReason::StopFoodChain, SimReason::StopStarted })
        {
            const char* note = "";
            if (reason == SimReason::StopNoHome || reason == SimReason::StopMaxReached || reason == SimReason::StopTooLate || reason == SimReason::StopLowStamina) { note = "条件で、考えなかった"; }
            if (reason == SimReason::StopRollMiss) { note = "条件を通って、抽選した結果"; }
            if (reason == SimReason::StopStarted) { note = "抽選に当選して、施設も決まった"; }
            add(ReasonJa(reason), Reason(b.stopReason, reason), considered, 1, note);
        }
        add("立ち寄り先の利用が終わった", b.stopVisits, b.stopStarted, 0, "分母は、立ち寄りを始めた回数");
        return rows;
    }

    std::vector<StatRow> BuildUseRows(const SimStatBucket& b)
    {
        std::vector<StatRow> rows;
        auto count = [&](const char* label, uint64_t n, const char* note = "")
        {
            StatRow row; row.label = label; row.count = n; row.note = note; rows.push_back(row);
        };
        auto money = [&](const char* label, double v, const char* note = "")
        {
            StatRow row; row.label = label; row.money = true; row.value = v; row.note = note; rows.push_back(row);
        };
        auto value = [&](const char* label, double v, const char* note = "")
        {
            StatRow row; row.label = label; row.isValue = true; row.value = v; row.note = note; rows.push_back(row);
        };
        count("利用が終わった回数(主目的 + 立ち寄り)", b.visits);
        money("売上の合計(円)", b.revenue);
        value("1回の利用あたりの売上(円)", b.visits > 0 ? b.revenue / static_cast<double>(b.visits) : 0.0);
        value("生活需要の回復量の合計", b.lifeRecovered);
        value("体力の消費量の合計", b.staminaUsed);
        if (b.testApplied > 0 || b.testRejected > 0)
        {
            count("テスト指示を適用した", b.testApplied);
            count("テスト指示を、普通のルールで拒否した", b.testRejected);
        }
        return rows;
    }

    // 件数・分母・率・95%の範囲の表を描く。
    void DrawRowsTable(const char* id, const std::vector<StatRow>& rows)
    {
        if (!ImGui::BeginTable(id, 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
        {
            return;
        }
        ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthStretch, 3.2f);
        ImGui::TableSetupColumn("件数", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("分母", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("率", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("95%の範囲", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("説明", ImGuiTableColumnFlags_WidthStretch, 2.6f);
        ImGui::TableHeadersRow();
        for (const StatRow& row : rows)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (row.indent > 0) { ImGui::Indent(14.0f * static_cast<float>(row.indent)); }
            ImGui::TextUnformatted(row.label.c_str());
            if (row.indent > 0) { ImGui::Unindent(14.0f * static_cast<float>(row.indent)); }
            ImGui::TableNextColumn();
            if (row.money) { ImGui::Text("%.0f", row.value); }
            else if (row.isValue) { ImGui::Text("%.2f", row.value); }
            else { ImGui::Text("%llu", static_cast<unsigned long long>(row.count)); }
            ImGui::TableNextColumn();
            if (row.denominator > 0) { ImGui::Text("%llu", static_cast<unsigned long long>(row.denominator)); } else { ImGui::TextDisabled("-"); }
            ImGui::TableNextColumn();
            if (row.denominator > 0)
            {
                const double rate = static_cast<double>(row.count) / static_cast<double>(row.denominator);
                if (row.denominator < kSimStatsMinReliableTrials)
                {
                    ImGui::TextColored(kColorWarning, "%.1f%% 参考", rate * 100.0);
                }
                else
                {
                    ImGui::Text("%.1f%%", rate * 100.0);
                }
            }
            else { ImGui::TextDisabled("-"); }
            ImGui::TableNextColumn();
            if (row.denominator > 0)
            {
                double low = 0.0, high = 1.0;
                SimWilsonInterval(row.count, row.denominator, low, high);
                ImGui::TextDisabled("%.1f〜%.1f%%", low * 100.0, high * 100.0);
            }
            else { ImGui::TextDisabled("-"); }
            ImGui::TableNextColumn();
            if (!row.note.empty()) { ImGui::TextDisabled("%s", row.note.c_str()); }
        }
        ImGui::EndTable();
    }

    // 確率の判定の、期待と実際の比べ(目視の感覚を数値と照合する)。
    void DrawRollRow(const char* label, const SimRollTally& tally)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(tally.trials));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(tally.hits));
        ImGui::TableNextColumn();
        if (tally.trials == 0) { ImGui::TextDisabled("-"); }
        else
        {
            const double rate = static_cast<double>(tally.hits) / static_cast<double>(tally.trials);
            ImGui::Text("%.2f%%", rate * 100.0);
        }
        ImGui::TableNextColumn();
        if (tally.trials == 0) { ImGui::TextDisabled("-"); }
        else { ImGui::Text("%.2f%%", tally.ExpectedHits() / static_cast<double>(tally.trials) * 100.0); }
        ImGui::TableNextColumn();
        if (tally.trials == 0) { ImGui::TextDisabled("-"); }
        else { ImGui::Text("%.1f", tally.ExpectedHits()); }
        ImGui::TableNextColumn();
        if (tally.trials == 0 || tally.StdDev() <= 0.0) { ImGui::TextDisabled("-"); }
        else
        {
            const double z = (static_cast<double>(tally.hits) - tally.ExpectedHits()) / tally.StdDev();
            if (std::fabs(z) <= 2.0) { ImGui::TextColored(kColorGood, "%+.1fσ 範囲内", z); }
            else { ImGui::TextColored(kColorWarning, "%+.1fσ ずれ大", z); }
        }
    }

    void DrawRollTable(const char* id, const std::vector<std::pair<const char*, const SimRollTally*>>& rolls)
    {
        if (!ImGui::BeginTable(id, 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            return;
        }
        ImGui::TableSetupColumn("判定", ImGuiTableColumnFlags_WidthStretch, 2.4f);
        ImGui::TableSetupColumn("回数");
        ImGui::TableSetupColumn("当選");
        ImGui::TableSetupColumn("実際の率");
        ImGui::TableSetupColumn("確率の平均");
        ImGui::TableSetupColumn("期待される当選");
        ImGui::TableSetupColumn("ずれ", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableHeadersRow();
        for (const auto& roll : rolls)
        {
            DrawRollRow(roll.first, *roll.second);
        }
        ImGui::EndTable();
    }

    std::string FacilityName(const std::vector<GameObject>& objects, uint32_t id)
    {
        const GameObject* object = GameObjectFactory::FindById(objects, id);
        if (!object)
        {
            return Fmt("施設#%u(もう無い)", id);
        }
        const FacilityDefinition* definition = FindFacilityDefinition(object->buildingType);
        return Fmt("%s #%u", definition ? definition->name : GetBuildingDefinition(object->buildingType).name, id);
    }

    const SimStatBucket& PickBucket(const SimStatsScopeData& scope, int period)
    {
        if (period == 1) { return scope.thisWeek; }
        if (period == 2) { return scope.lastWeek; }
        return scope.total;
    }

    const char* PeriodJa(int period)
    {
        return period == 1 ? "今週" : (period == 2 ? "先週" : "通算");
    }

    const char* ScopeJa(SimStatScope scope)
    {
        return scope == SimStatScope::Normal ? "通常" : "テスト";
    }

    void AppendCsvRows(std::string& out, const char* scope, const char* period, const char* section, const std::vector<StatRow>& rows)
    {
        for (const StatRow& row : rows)
        {
            std::string label = row.label;
            std::replace(label.begin(), label.end(), ',', ' ');
            double count = static_cast<double>(row.count);
            if (row.money || row.isValue) { count = row.value; }
            out += Fmt("%s,%s,%s,%s,%.2f,", scope, period, section, label.c_str(), count);
            if (row.denominator > 0)
            {
                out += Fmt("%llu,%.4f\n", static_cast<unsigned long long>(row.denominator), static_cast<double>(row.count) / static_cast<double>(row.denominator));
            }
            else
            {
                out += ",\n";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------
void SimStatsPanel::Draw(DemandSystem& demand, const std::vector<GameObject>& objects)
{
    if (m_open && !*m_open)
    {
        return;
    }
    if (!ImGui::Begin("行動統計", m_open))
    {
        ImGui::End();
        return;
    }

    const SimStats& stats = demand.GetBehaviorStats();
    DrawSummary(demand);

    // 範囲の選択に応じて、通常・テストを描く。
    const bool showNormal = m_scopeSelect == 0 || m_scopeSelect == 2;
    const bool showTest = m_scopeSelect == 1 || m_scopeSelect == 2;
    for (int pass = 0; pass < 2; ++pass)
    {
        const SimStatScope scopeId = pass == 0 ? SimStatScope::Normal : SimStatScope::Test;
        if ((pass == 0 && !showNormal) || (pass == 1 && !showTest))
        {
            continue;
        }
        const SimStatsScopeData& scope = stats.Scope(scopeId);
        ImGui::PushID(pass);
        const std::string title = Fmt("【%s】%s", ScopeJa(scopeId), scopeId == SimStatScope::Normal ? "通常のプレイ" : "行動テストで強制した流れ(通常の数字とは、混ぜない)");
        ImGui::Spacing();
        ImGui::TextColored(kColorWarning, "%s", title.c_str());
        if (ImGui::CollapsingHeader(Fmt("外出・立ち寄りの流れ(%s)##flows", PeriodJa(m_periodSelect)).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawFlows(scope, PickBucket(scope, m_periodSelect), scopeId == SimStatScope::Normal);
        }
        if (ImGui::CollapsingHeader("週ごとの推移##weekly"))
        {
            DrawWeekly(scope, stats.CurrentWeek());
        }
        if (ImGui::CollapsingHeader("施設別(通算)##facilities"))
        {
            DrawFacilities(scope, objects);
        }
        if (ImGui::CollapsingHeader("住民別(通算)##residents"))
        {
            DrawResidents(scope, stats.CurrentWeek());
        }
        ImGui::PopID();
    }

    // CSV(クリップボードへ)。画面の範囲・期間のものを、表計算ソフトへ貼れる形にする。
    Heading("書き出し");
    if (ImGui::Button("CSVをクリップボードへコピー"))
    {
        const std::string csv = BuildCsv(stats, objects);
        ImGui::SetClipboardText(csv.c_str());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(表示中の範囲・期間。表計算ソフトに貼り付けられます)");

    ImGui::End();
}

void SimStatsPanel::DrawSummary(DemandSystem& demand)
{
    SimStats& stats = demand.GetBehaviorStats();
    SimDebugRecorder& recorder = demand.GetDebugRecorder();

    if (!recorder.IsEnabled())
    {
        ImGui::TextColored(kColorWarning, "記録がOFFです。統計は、記録(住民デバッグの「記録する」)がONの間だけ数えます。");
    }
    Note("外出や立ち寄りの「件数」と、その「率」(必ず、分母つき)を見る。目視の感覚を、数字で確かめるための表。件数が少ない(30未満)率は「参考」と出し、95%の範囲も並べる。"
         "【通常】と【テスト】(行動テストで強制した流れ)は、別々に数える。ロード・リセットで、全て消える。");

    ImGui::SetNextItemWidth(180.0f);
    static const char* const kScopeLabels[] = { "通常のみ", "テストのみ", "通常とテストを並べる" };
    ImGui::Combo("範囲", &m_scopeSelect, kScopeLabels, 3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    static const char* const kPeriodLabels[] = { "通算(集計を始めてから)", "今週", "先週" };
    ImGui::Combo("期間", &m_periodSelect, kPeriodLabels, 3);

    if (stats.StartWeek() < 0)
    {
        ImGui::TextDisabled("まだ、何も数えていません(ゲームを進めると、数え始めます)。");
    }
    else
    {
        ImGui::Text("集計を始めた週: %d   今の週: %d   数えた回数: %llu", stats.StartWeek(), stats.CurrentWeek(), static_cast<unsigned long long>(stats.TotalCounted()));
    }
    if (ImGui::Button("集計を始め直す"))
    {
        stats.Reset();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(通算・週ごと・施設別・住民別を、全て0に戻す。住民デバッグの記録は消えない)");
}

void SimStatsPanel::DrawFlows(const SimStatsScopeData& scope, const SimStatBucket& bucket, bool normalScope)
{
    if (m_periodSelect == 2 && !scope.hasLastWeek)
    {
        ImGui::TextDisabled("先週は、まだ終わっていません。");
        return;
    }

    if (normalScope)
    {
        Heading("外出判定の抽選(毎ティック。自然な抽選だけ)");
        DrawRollTable("##outingrolls", {
            { "需要が不足している(不足による外出)", &bucket.outingRollNeed },
            { "足りていて、体力に余裕がある(自発)", &bucket.outingRollSpontaneous },
            { "足りているが、体力が足りない(確率0)", &bucket.outingRollLowStamina } });
        Note("「期待される当選」は、各判定の確率の合計(ずれは、標準偏差の何倍か。±2σの範囲内なら、確率どおりと言える)。"
             "確率が毎回違うので、実際の率が「確率の平均」に近ければ、正しく抽選されている。行動テストで強制した当選は、数えない。");
    }

    Heading("外出の流れ");
    DrawRowsTable("##outing", BuildOutingRows(bucket));

    Heading("立ち寄りの流れ");
    DrawRowsTable("##stopover", BuildStopoverRows(bucket));
    if (normalScope && bucket.stopRoll.trials > 0)
    {
        DrawRollTable("##stoprolls", { { "立ち寄りの確率の抽選(条件を通った時)", &bucket.stopRoll } });
    }
    else if (!normalScope && bucket.stopRoll.trials > 0)
    {
        Note("テストの範囲の抽選は、強制した当選を含むので、期待値とは比べません。");
    }

    Heading("利用・売上");
    DrawRowsTable("##use", BuildUseRows(bucket));
}

void SimStatsPanel::DrawWeekly(const SimStatsScopeData& scope, int currentWeek)
{
    if (scope.weekly.empty() && currentWeek < 0)
    {
        ImGui::TextDisabled("まだ、何も数えていません。");
        return;
    }
    Note("週ごとの、外出・立ち寄り・売上(新しい週が上)。いちばん上は、今の週(途中)。");
    if (!ImGui::BeginTable("##weekly", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 200.0f)))
    {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("週");
    ImGui::TableSetupColumn("外出判定");
    ImGui::TableSetupColumn("外出開始");
    ImGui::TableSetupColumn("出発");
    ImGui::TableSetupColumn("立ち寄り判定");
    ImGui::TableSetupColumn("立ち寄り開始");
    ImGui::TableSetupColumn("利用");
    ImGui::TableSetupColumn("売上(円)");
    ImGui::TableHeadersRow();
    auto row = [&](const std::string& label, const SimStatBucket& b)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(label.c_str());
        const uint64_t rolls = b.outingRollNeed.trials + b.outingRollSpontaneous.trials + b.outingRollLowStamina.trials;
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(rolls));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(b.outingStarted));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(b.departed));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(b.stopConsidered));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(b.stopStarted));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(b.visits));
        ImGui::TableNextColumn(); ImGui::Text("%.0f", b.revenue);
    };
    if (currentWeek >= 0)
    {
        row(Fmt("%d(今・途中)", currentWeek), scope.thisWeek);
    }
    for (auto it = scope.weekly.rbegin(); it != scope.weekly.rend(); ++it)
    {
        row(Fmt("%d", it->week), it->bucket);
    }
    ImGui::EndTable();
}

void SimStatsPanel::DrawFacilities(const SimStatsScopeData& scope, const std::vector<GameObject>& objects)
{
    if (scope.facilities.empty())
    {
        ImGui::TextDisabled("まだ、施設の統計はありません。");
        return;
    }
    Note("「候補に入った」= 施設選びの候補として評価された回数。「選ばれた」= 実際に選ばれた回数(選択率 = 選ばれた / 候補に入った)。"
         "「除外」= 候補から外れた回数(理由の別)。除外された回数は、候補に入った回数の内数。");
    static const char* const kSortLabels[] = { "利用回数", "売上", "選ばれた回数", "候補に入った回数" };
    ImGui::SetNextItemWidth(160.0f);
    ImGui::Combo("並べ替え", &m_facilitySort, kSortLabels, 4);

    std::vector<std::pair<uint32_t, const SimFacilityStat*>> list;
    list.reserve(scope.facilities.size());
    for (const auto& entry : scope.facilities) { list.emplace_back(entry.first, &entry.second); }
    const int sort = m_facilitySort;
    std::sort(list.begin(), list.end(), [sort](const auto& a, const auto& b)
    {
        const SimFacilityStat& x = *a.second;
        const SimFacilityStat& y = *b.second;
        double kx = 0.0, ky = 0.0;
        switch (sort)
        {
        case 0: kx = static_cast<double>(x.visits); ky = static_cast<double>(y.visits); break;
        case 1: kx = x.revenue; ky = y.revenue; break;
        case 2: kx = static_cast<double>(x.chosen); ky = static_cast<double>(y.chosen); break;
        default: kx = static_cast<double>(x.candidateIn); ky = static_cast<double>(y.candidateIn); break;
        }
        if (kx != ky) { return kx > ky; }
        return a.first < b.first;
    });

    if (!ImGui::BeginTable("##facilities", 12, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 240.0f)))
    {
        return;
    }
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("施設", ImGuiTableColumnFlags_WidthStretch, 2.2f);
    ImGui::TableSetupColumn("候補に入った");
    ImGui::TableSetupColumn("選ばれた");
    ImGui::TableSetupColumn("(主/立寄)");
    ImGui::TableSetupColumn("選択率");
    ImGui::TableSetupColumn("除外:到達不可");
    ImGui::TableSetupColumn("体力不足");
    ImGui::TableSetupColumn("範囲外");
    ImGui::TableSetupColumn("使用済み");
    ImGui::TableSetupColumn("着いた");
    ImGui::TableSetupColumn("利用");
    ImGui::TableSetupColumn("売上(円)");
    ImGui::TableHeadersRow();
    for (const auto& entry : list)
    {
        const SimFacilityStat& f = *entry.second;
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(FacilityName(objects, entry.first).c_str());
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(f.candidateIn));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(f.chosen));
        ImGui::TableNextColumn(); ImGui::Text("%llu / %llu", static_cast<unsigned long long>(f.chosenMain), static_cast<unsigned long long>(f.chosenStop));
        ImGui::TableNextColumn();
        if (f.candidateIn == 0) { ImGui::TextDisabled("-"); }
        else if (f.candidateIn < kSimStatsMinReliableTrials) { ImGui::TextColored(kColorWarning, "%.0f%% 参考", 100.0 * static_cast<double>(f.chosen) / static_cast<double>(f.candidateIn)); }
        else { ImGui::Text("%.1f%%", 100.0 * static_cast<double>(f.chosen) / static_cast<double>(f.candidateIn)); }
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(f.rejected[static_cast<size_t>(CandidateReject::Unreachable)]));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(f.rejected[static_cast<size_t>(CandidateReject::NotEnoughStamina)]));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(f.rejected[static_cast<size_t>(CandidateReject::OutOfRange)]));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(f.rejected[static_cast<size_t>(CandidateReject::VisitedThisOuting)]));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(f.arrivals));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(f.visits));
        ImGui::TableNextColumn(); ImGui::Text("%.0f", f.revenue);
    }
    ImGui::EndTable();
}

void SimStatsPanel::DrawResidents(const SimStatsScopeData& scope, int currentWeek)
{
    if (scope.residents.empty())
    {
        ImGui::TextDisabled("まだ、住民の統計はありません。");
        return;
    }
    Note("「外出/週」= 外出した回数 ÷ 数え始めてからの週の数(その住民が初めて現れた週から、今の週まで)。");
    static const char* const kSortLabels[] = { "外出の回数", "利用の回数", "使ったお金", "立ち往生の回数" };
    ImGui::SetNextItemWidth(160.0f);
    ImGui::Combo("並べ替え##res", &m_residentSort, kSortLabels, 4);

    std::vector<std::pair<uint32_t, const SimResidentStat*>> list;
    list.reserve(scope.residents.size());
    for (const auto& entry : scope.residents) { list.emplace_back(entry.first, &entry.second); }
    const int sort = m_residentSort;
    std::sort(list.begin(), list.end(), [sort](const auto& a, const auto& b)
    {
        const SimResidentStat& x = *a.second;
        const SimResidentStat& y = *b.second;
        double kx = 0.0, ky = 0.0;
        switch (sort)
        {
        case 0: kx = static_cast<double>(x.outings); ky = static_cast<double>(y.outings); break;
        case 1: kx = static_cast<double>(x.visits); ky = static_cast<double>(y.visits); break;
        case 2: kx = x.revenue; ky = y.revenue; break;
        default: kx = static_cast<double>(x.stranded); ky = static_cast<double>(y.stranded); break;
        }
        if (kx != ky) { return kx > ky; }
        return a.first < b.first;
    });

    if (!ImGui::BeginTable("##residents", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 220.0f)))
    {
        return;
    }
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("住民");
    ImGui::TableSetupColumn("外出");
    ImGui::TableSetupColumn("外出/週");
    ImGui::TableSetupColumn("立ち寄り");
    ImGui::TableSetupColumn("利用");
    ImGui::TableSetupColumn("使ったお金(円)");
    ImGui::TableSetupColumn("立ち往生");
    ImGui::TableSetupColumn("中断");
    ImGui::TableSetupColumn("住居なし");
    ImGui::TableHeadersRow();
    for (const auto& entry : list)
    {
        const SimResidentStat& r = *entry.second;
        const int weeks = (std::max)(1, (currentWeek >= 0 && r.firstWeek >= 0) ? currentWeek - r.firstWeek + 1 : 1);
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("#%u", entry.first);
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(r.outings));
        ImGui::TableNextColumn(); ImGui::Text("%.2f", static_cast<double>(r.outings) / static_cast<double>(weeks));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(r.stopovers));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(r.visits));
        ImGui::TableNextColumn(); ImGui::Text("%.0f", r.revenue);
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(r.stranded));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(r.aborted));
        ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(r.homeless));
    }
    ImGui::EndTable();
}

std::string SimStatsPanel::BuildCsv(const SimStats& stats, const std::vector<GameObject>& objects) const
{
    std::string out = "範囲,期間,区分,項目,件数,分母,率\n";
    const bool showNormal = m_scopeSelect == 0 || m_scopeSelect == 2;
    const bool showTest = m_scopeSelect == 1 || m_scopeSelect == 2;
    for (int pass = 0; pass < 2; ++pass)
    {
        if ((pass == 0 && !showNormal) || (pass == 1 && !showTest))
        {
            continue;
        }
        const SimStatScope scopeId = pass == 0 ? SimStatScope::Normal : SimStatScope::Test;
        const SimStatsScopeData& scope = stats.Scope(scopeId);
        const SimStatBucket& bucket = PickBucket(scope, m_periodSelect);
        const char* scopeName = ScopeJa(scopeId);
        const char* periodName = PeriodJa(m_periodSelect);
        AppendCsvRows(out, scopeName, periodName, "外出", BuildOutingRows(bucket));
        AppendCsvRows(out, scopeName, periodName, "立ち寄り", BuildStopoverRows(bucket));
        AppendCsvRows(out, scopeName, periodName, "利用", BuildUseRows(bucket));
        if (scopeId == SimStatScope::Normal)
        {
            out += Fmt("%s,%s,外出判定,需要の不足,%.0f,%llu,%.4f\n", scopeName, periodName, static_cast<double>(bucket.outingRollNeed.hits),
                       static_cast<unsigned long long>(bucket.outingRollNeed.trials), bucket.outingRollNeed.trials ? static_cast<double>(bucket.outingRollNeed.hits) / static_cast<double>(bucket.outingRollNeed.trials) : 0.0);
            out += Fmt("%s,%s,外出判定,自発,%.0f,%llu,%.4f\n", scopeName, periodName, static_cast<double>(bucket.outingRollSpontaneous.hits),
                       static_cast<unsigned long long>(bucket.outingRollSpontaneous.trials), bucket.outingRollSpontaneous.trials ? static_cast<double>(bucket.outingRollSpontaneous.hits) / static_cast<double>(bucket.outingRollSpontaneous.trials) : 0.0);
        }
        for (const SimWeekRow& week : scope.weekly)
        {
            out += Fmt("%s,週%d,週ごと,外出開始,%llu,,\n", scopeName, week.week, static_cast<unsigned long long>(week.bucket.outingStarted));
            out += Fmt("%s,週%d,週ごと,立ち寄り開始,%llu,,\n", scopeName, week.week, static_cast<unsigned long long>(week.bucket.stopStarted));
            out += Fmt("%s,週%d,週ごと,売上,%.0f,,\n", scopeName, week.week, week.bucket.revenue);
        }
        for (const auto& entry : scope.facilities)
        {
            const SimFacilityStat& f = entry.second;
            std::string name = FacilityName(objects, entry.first);
            std::replace(name.begin(), name.end(), ',', ' ');
            out += Fmt("%s,通算,施設別,%s 候補に入った,%llu,,\n", scopeName, name.c_str(), static_cast<unsigned long long>(f.candidateIn));
            out += Fmt("%s,通算,施設別,%s 選ばれた,%llu,%llu,%.4f\n", scopeName, name.c_str(), static_cast<unsigned long long>(f.chosen), static_cast<unsigned long long>(f.candidateIn),
                       f.candidateIn ? static_cast<double>(f.chosen) / static_cast<double>(f.candidateIn) : 0.0);
            out += Fmt("%s,通算,施設別,%s 利用,%llu,,\n", scopeName, name.c_str(), static_cast<unsigned long long>(f.visits));
            out += Fmt("%s,通算,施設別,%s 売上,%.0f,,\n", scopeName, name.c_str(), f.revenue);
        }
        for (const auto& entry : scope.residents)
        {
            const SimResidentStat& r = entry.second;
            out += Fmt("%s,通算,住民別,#%u 外出,%llu,,\n", scopeName, entry.first, static_cast<unsigned long long>(r.outings));
            out += Fmt("%s,通算,住民別,#%u 利用,%llu,,\n", scopeName, entry.first, static_cast<unsigned long long>(r.visits));
        }
    }
    return out;
}
