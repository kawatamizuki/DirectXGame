#include "SimEndurancePanel.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include "imgui.h"
#include "SimEndurance.h"

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

    const char* AnomalyJa(SimAnomaly kind)
    {
        switch (kind)
        {
        case SimAnomaly::NonFiniteValue:            return "値がNaN/Inf";
        case SimAnomaly::StaminaOutOfRange:         return "体力が範囲外";
        case SimAnomaly::NeedOutOfRange:            return "需要が範囲外";
        case SimAnomaly::SatisfactionOutOfRange:    return "満足度が範囲外";
        case SimAnomaly::PenaltyOutOfRange:         return "ペナルティが範囲外";
        case SimAnomaly::StopCountNegative:         return "立ち寄り回数が負";
        case SimAnomaly::WaypointIndexOutOfRange:   return "経路の添字が範囲外";
        case SimAnomaly::MissingTargetFacility:     return "行き先の施設が無い";
        case SimAnomaly::MissingHome:               return "家の参照が壊れている";
        case SimAnomaly::HomelessWithHome:          return "住居なしなのに家のid";
        case SimAnomaly::StopCountExceeded:         return "立ち寄りの回数が上限超え";
        case SimAnomaly::ArrivedWithoutMove:        return "歩き始めずに到着";
        case SimAnomaly::SettleWithoutArrival:      return "到着せずに(2回目の)売上";
        case SimAnomaly::DuplicateFacilityInOuting: return "同じ外出で同じ施設を2回";
        case SimAnomaly::MoveWhileVisiting:         return "利用の途中で歩き出した";
        case SimAnomaly::ReturnWhileVisiting:       return "利用の途中で帰宅";
        case SimAnomaly::NegativeRevenue:           return "売上が負・NaN";
        case SimAnomaly::SettledOnMissingFacility:  return "売上の施設が無い";
        default:                                    return "(不明)";
        }
    }
}

void SimEndurancePanel::Draw(SimEndurance& endurance, bool canStart, const char* whyNot)
{
    if (m_open && !*m_open)
    {
        return;
    }
    if (!ImGui::Begin("耐久試験", m_open))
    {
        ImGui::End();
        return;
    }

    Note("今の街を「テスト世界」にして、画面を描かずに、長い期間(週)を一気に進め、異常・負荷・決定論を調べます。始める時に街を保存して(基点)、終わったら元の街へ戻します。"
         "1ステップは、ふだんのプレイと同じ呼び方(時計 → 住民)の固定刻み(1/60秒)で、時計の倍率で速く進めます(大きな時間を1回で渡さないので、週末・需要の判定・移動・滞在の順番は、ふだんと同じです)。"
         "プレイヤーの操作は入りません(同じ状態から、時間を進めるだけ)。");

    const SimEnduranceState state = endurance.GetState();
    SimEnduranceSettings& settings = endurance.Settings();

    Heading("1. 設定");
    ImGui::BeginDisabled(state != SimEnduranceState::Idle);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("進める週数(ゲーム内)", &settings.weeks, 1, 260);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("時計の倍率", &settings.timeScale, 1.0f, 120.0f, "%.0f 倍");
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("1フレームの持ち時間(ミリ秒)", &settings.budgetMilliseconds, 1, 50);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("実時間の上限(秒)", &settings.wallLimitSeconds, 5, 3600);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("計測の間隔(ステップ)", &settings.sampleIntervalSteps, 100, 10000);
    ImGui::Checkbox("異常が出たら止めて、その状態を残す", &settings.stopOnAnomaly);
    ImGui::EndDisabled();

    Heading("2. 実行");
    if (state == SimEnduranceState::Idle)
    {
        ImGui::BeginDisabled(!canStart);
        if (ImGui::Button("耐久試験を始める"))
        {
            endurance.RequestStart(SimEnduranceMode::Endurance);
        }
        ImGui::SameLine();
        if (ImGui::Button("決定論テスト(同じ条件で2回進めて、指紋を比べる)"))
        {
            endurance.RequestStart(SimEnduranceMode::Determinism);
        }
        ImGui::EndDisabled();
        if (!canStart && whyNot && whyNot[0] != '\0')
        {
            ImGui::TextColored(kColorWarning, "%s", whyNot);
        }
    }
    else if (state == SimEnduranceState::Running)
    {
        const int weeks = endurance.GetWeeksDone();
        const float progress = endurance.IsSecondRun() && endurance.GetTargetSteps() > 0
            ? static_cast<float>(endurance.GetSteps()) / static_cast<float>(endurance.GetTargetSteps())
            : static_cast<float>(weeks) / static_cast<float>((std::max)(1, settings.weeks));
        ImGui::TextColored(kColorWarning, "%s", endurance.IsSecondRun() ? "実行中(決定論テストの2回目)" : "実行中");
        ImGui::ProgressBar((std::min)(1.0f, (std::max)(0.0f, progress)), ImVec2(-1.0f, 0.0f));
        ImGui::Text("ステップ %llu   週 %d / %d   経過 %.1f 秒   メモリ %.0f MB   異常 %llu 件", static_cast<unsigned long long>(endurance.GetSteps()), weeks, settings.weeks,
            endurance.GetWallSeconds(), endurance.GetWorkingSetMB(), static_cast<unsigned long long>(endurance.GetAnomalyCount()));
        if (ImGui::Button("中止"))
        {
            endurance.RequestCancel();
        }
    }
    else
    {
        ImGui::TextColored(kColorBad, "異常が出て、止まっています(時間は止まっています)。");
        if (ImGui::Button("元の世界へ戻す"))
        {
            endurance.RequestRestoreWorld();
        }
        ImGui::SameLine();
        if (ImGui::Button("この状態で続ける"))
        {
            endurance.RequestKeepWorld();
        }
        ImGui::TextDisabled("止まっている間は、住民デバッグ・行動統計で、その時点の状態を見られます。");
    }
    if (!endurance.GetMessage().empty())
    {
        ImGui::TextWrapped("%s", endurance.GetMessage().c_str());
    }

    const SimEnduranceResult& r = endurance.GetResult();
    Heading("3. 結果");
    if (!r.valid)
    {
        ImGui::TextDisabled("まだ、試験をしていません。");
    }
    else
    {
        ImGui::Text("種類: %s   終わった理由: %s", r.mode == SimEnduranceMode::Determinism ? "決定論テスト" : "耐久試験", r.stopReason.c_str());
        if (ImGui::BeginTable("##enduranceresult", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            auto row = [](const char* label, const std::string& value)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(value.c_str());
            };
            auto fmt = [](const char* format, double value) { char buffer[64]; std::snprintf(buffer, sizeof(buffer), format, value); return std::string(buffer); };
            row("進めたステップ / 週", std::to_string(r.steps) + " ステップ / " + std::to_string(r.weeksDone) + " 週(倍率 " + fmt("%.0f", settings.timeScale) + ")");
            row("実時間", fmt("%.2f 秒", r.wallSeconds));
            row("速さ", fmt("%.0f ステップ/秒", r.stepsPerSecond) + " / ゲーム内の時間が、実時間の " + fmt("%.0f 倍", r.gameSecondsPerRealSecond) + "で進んだ");
            row("1ステップの平均", fmt("%.4f ミリ秒", r.averageStepMilliseconds));
            row("最も重かった1フレームの、進めた時間", fmt("%.1f ミリ秒", r.maxFrameMilliseconds));
            row("メモリ(ワーキングセット)", fmt("%.0f", r.workingSetStartMB) + " → " + fmt("%.0f", r.workingSetEndMB) + " MB(最大 " + fmt("%.0f", r.workingSetPeakMB) + " MB。増え方 " + fmt("%+.1f", r.workingSetEndMB - r.workingSetStartMB) + " MB)");
            row("記録したイベント / 記録の使用量", std::to_string(r.events) + " 件 / " + fmt("%.1f", static_cast<double>(r.recorderBytes) / (1024.0 * 1024.0)) + " MB(上限で、古い物は捨てています)");
            row("乱数の種", std::to_string(r.seed));
            row("異常", std::to_string(r.anomalies) + " 件");
            ImGui::EndTable();
        }
        if (r.anomalies == 0)
        {
            ImGui::TextColored(kColorGood, "異常は、見つかりませんでした。");
        }
        else
        {
            ImGui::TextColored(kColorBad, "異常が %llu 件ありました:", static_cast<unsigned long long>(r.anomalies));
            for (size_t i = 1; i < r.anomalyByKind.size(); ++i)
            {
                if (r.anomalyByKind[i] > 0)
                {
                    ImGui::BulletText("%s: %llu 件", AnomalyJa(static_cast<SimAnomaly>(i)), static_cast<unsigned long long>(r.anomalyByKind[i]));
                }
            }
            if (r.firstAnomaly.valid)
            {
                ImGui::Text("最初の異常: %s  ステップ %llu  週%d %.2f時  住民 #%u  値 %.3f", AnomalyJa(r.firstAnomaly.kind), static_cast<unsigned long long>(r.firstAnomaly.step),
                    r.firstAnomaly.week, r.firstAnomaly.hour, r.firstAnomaly.residentId, r.firstAnomaly.value);
                ImGui::TextDisabled("再現: 同じ基点・乱数の種・倍率で、先頭からそのステップまで進めると、同じ異常が出ます(固定刻みで、決定論的なため)。");
            }
        }
        if (r.determinismChecked)
        {
            char a[32], b[32];
            std::snprintf(a, sizeof(a), "%016llX", static_cast<unsigned long long>(r.digest.hash));
            std::snprintf(b, sizeof(b), "%016llX", static_cast<unsigned long long>(r.digestSecond.hash));
            ImGui::Text("指紋: 1回目 %s(イベント %llu・抽選 %llu) / 2回目 %s(イベント %llu・抽選 %llu)", a, static_cast<unsigned long long>(r.digest.events), static_cast<unsigned long long>(r.digest.rolls),
                b, static_cast<unsigned long long>(r.digestSecond.events), static_cast<unsigned long long>(r.digestSecond.rolls));
            if (r.determinismMatched)
            {
                ImGui::TextColored(kColorGood, "一致: 同じ条件で、同じイベントの列になりました。");
            }
            else
            {
                ImGui::TextColored(kColorBad, "不一致: 同じ条件のはずなのに、結果が違います(非決定的な処理・保存していない状態が、結果に効いている可能性があります)。");
            }
        }
        if (!r.csvPath.empty())
        {
            ImGui::TextDisabled("結果を保存しました: %s", r.csvPath.c_str());
        }
    }

    Heading("4. 注意");
    Note("・試験の間は、ゲームは止まっています(画面は、描き続けますが、街は進みません)。終わると、基点(始めた時の街・時間の速度)へ戻します。異常で止めた時は、その状態を残します。"
         "・「タイムライン」がON・「乱数・再現」の記録/再生中は、始められません。試験の間にロード(F9)すると、中止します。"
         "・メモリの増え方は、このプロセス全体のワーキングセットです(他の処理の影響も受けます)。目安として見てください。"
         "・C++のアクセス違反など、致命的な落ち方を、事前に全て捕まえることは、約束できません。");

    ImGui::End();
}
