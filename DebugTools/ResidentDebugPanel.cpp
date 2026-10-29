#include "ResidentDebugPanel.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include "imgui.h"
#include "DemandSystem.h"
#include "GameObject.h"
#include "GameObjectFactory.h"
#include "FacilityData.h"
#include "BuildingType.h"
#include "WorldScale.h"

namespace
{
    // ---- 色(普通の不成立=灰、注意=黄、確認された異常=赤。意外な抽選結果は、赤にしない) ----
    const ImVec4 kColorNormal(0.90f, 0.90f, 0.90f, 1.0f);
    const ImVec4 kColorSuccess(0.55f, 0.90f, 0.55f, 1.0f);
    const ImVec4 kColorNotMet(0.62f, 0.62f, 0.62f, 1.0f);
    const ImVec4 kColorWarning(1.00f, 0.85f, 0.30f, 1.0f);
    const ImVec4 kColorViolation(1.00f, 0.30f, 0.30f, 1.0f);
    const ImVec4 kColorHeading(0.55f, 0.80f, 1.00f, 1.0f);

    ImVec4 ColorOf(SimEventResult result)
    {
        switch (result)
        {
        case SimEventResult::Success:   return kColorSuccess;
        case SimEventResult::NotMet:    return kColorNotMet;
        case SimEventResult::Warning:   return kColorWarning;
        case SimEventResult::Violation: return kColorViolation;
        default:                        return kColorNormal;
        }
    }

    // printf形式で文字列を作る。
    std::string Fmt(const char* format, ...)
    {
        char buffer[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        return buffer;
    }

    std::string HourText(float hour)
    {
        int h = static_cast<int>(hour);
        int m = static_cast<int>((hour - static_cast<float>(h)) * 60.0f);
        return Fmt("%02d:%02d", h, m);
    }

    const char* StateJa(ResidentAgentState state)
    {
        switch (state)
        {
        case ResidentAgentState::AtHome:        return "自宅にいる (AtHome)";
        case ResidentAgentState::GoingOut:      return "施設へ移動中 (GoingOut)";
        case ResidentAgentState::Visiting:      return "施設を利用中 (Visiting)";
        case ResidentAgentState::ReturningHome: return "帰宅中 (ReturningHome)";
        case ResidentAgentState::Homeless:      return "住居なし (Homeless)";
        case ResidentAgentState::Stranded:      return "立ち往生 (Stranded)";
        default:                                return "(停止中の状態)";
        }
    }

    const char* RejectJa(CandidateReject reject)
    {
        switch (reject)
        {
        case CandidateReject::None:              return "対象";
        case CandidateReject::Unreachable:       return "除外: 道路でつながっていない";
        case CandidateReject::NotEnoughStamina:  return "除外: 体力不足";
        case CandidateReject::WrongCategory:     return "除外: 目的のカテゴリではない";
        case CandidateReject::VisitedThisOuting: return "除外: 今回の外出で使用済み";
        case CandidateReject::OutOfRange:        return "除外: 立ち寄りの範囲外(遠い)";
        }
        return "?";
    }

    const char* AnomalyJa(SimAnomaly anomaly)
    {
        switch (anomaly)
        {
        case SimAnomaly::NonFiniteValue:          return "値がNaN/Infになった";
        case SimAnomaly::StaminaOutOfRange:       return "体力が範囲外";
        case SimAnomaly::NeedOutOfRange:          return "需要が0〜100の範囲外";
        case SimAnomaly::SatisfactionOutOfRange:  return "満足度が範囲外";
        case SimAnomaly::PenaltyOutOfRange:       return "ペナルティの段階が範囲外";
        case SimAnomaly::StopCountNegative:       return "立ち寄り回数が負";
        case SimAnomaly::WaypointIndexOutOfRange: return "経路の添字が経路の外";
        case SimAnomaly::MissingTargetFacility:   return "行き先の施設が無い";
        case SimAnomaly::MissingHome:             return "家の参照が壊れている";
        case SimAnomaly::HomelessWithHome:        return "住居なしなのに家のidがある";
        default:                                  return "(不明な異常)";
        }
    }

    const char* EventKindJa(SimEventKind kind)
    {
        switch (kind)
        {
        case SimEventKind::OutingRoll:        return "外出判定";
        case SimEventKind::PurposeChoice:     return "目的選択";
        case SimEventKind::FacilitySelect:    return "施設選択";
        case SimEventKind::OutingBlocked:     return "外出できず";
        case SimEventKind::MoveStart:         return "移動開始";
        case SimEventKind::Arrived:           return "到着";
        case SimEventKind::VisitFinished:     return "利用完了";
        case SimEventKind::StopoverCheck:     return "立ち寄り判定";
        case SimEventKind::ReturnStart:       return "帰宅開始";
        case SimEventKind::ReturnRerouted:    return "迂回";
        case SimEventKind::ReturnHome:        return "帰宅完了";
        case SimEventKind::OutingAborted:     return "外出中断";
        case SimEventKind::BecameStranded:    return "立ち往生";
        case SimEventKind::StrandedRecovered: return "復旧";
        case SimEventKind::BecameHomeless:    return "住居なし";
        case SimEventKind::HomeDemolished:    return "住宅解体";
        case SimEventKind::Anomaly:           return "異常";
        case SimEventKind::MovedIn:           return "入居";
        }
        return "?";
    }

    // 施設の表示名("コンビニ #12")。もう無い施設は、その旨を出す。
    std::string FacilityLabel(const std::vector<GameObject>& objects, uint32_t id)
    {
        if (id == 0)
        {
            return "-";
        }
        const GameObject* object = GameObjectFactory::FindById(objects, id);
        if (!object)
        {
            return Fmt("施設#%u(もう無い)", id);
        }
        const FacilityDefinition* definition = FindFacilityDefinition(object->buildingType);
        return Fmt("%s #%u", definition ? definition->name : GetBuildingDefinition(object->buildingType).name, id);
    }

    std::string TypeLabel(BuildingType type, uint32_t id)
    {
        const FacilityDefinition* definition = FindFacilityDefinition(type);
        return Fmt("%s #%u", definition ? definition->name : GetBuildingDefinition(type).name, id);
    }

    // 1件のイベントの、人間向けの説明(UI側で作る。イベント自体は数値と列挙型だけを持つ)。
    std::string DescribeEvent(const SimEvent& e, const std::vector<GameObject>& objects)
    {
        const std::string facility = FacilityLabel(objects, e.facilityId);
        const std::string stop = (e.stopIndex > 0) ? Fmt("立ち寄り%d回目: ", e.stopIndex) : std::string();
        switch (e.kind)
        {
        case SimEventKind::OutingRoll:
            return Fmt("外出判定に当選(確率 %.1f%% > 乱数 %.4f)。%s", e.a * 100.0f, e.b,
                       e.reason == SimReason::OutingNeedDriven ? "需要の不足による外出" : "自発的な外出");
        case SimEventKind::PurposeChoice:
            return stop + "外出の目的を決めた(内訳は[詳細])";
        case SimEventKind::FacilitySelect:
            if (e.result == SimEventResult::Success)
            {
                return stop + Fmt("施設を抽選で選んだ → %s(重み %.3f、確率 %.1f%%)", facility.c_str(), e.a, e.b * 100.0f);
            }
            return stop + "選べる施設が無かった(全候補が除外された)";
        case SimEventKind::OutingBlocked:
            return e.reason == SimReason::HomeNoRoad ? "自宅の近くに道路が無く、出られなかった" : "目的のカテゴリの施設が1軒も無く、出られなかった";
        case SimEventKind::MoveStart:
            return stop + Fmt("%sへ歩き始めた(道のり %.1f)", facility.c_str(), e.a);
        case SimEventKind::Arrived:
            if (e.result == SimEventResult::Warning)
            {
                return Fmt("%sに着いたが、施設が無くなっていた", facility.c_str());
            }
            return Fmt("%sに着いて、利用を始めた(滞在 %.1f時間)", facility.c_str(), e.a);
        case SimEventKind::VisitFinished:
            if (e.result == SimEventResult::Warning)
            {
                return "利用が終わったが、施設が無くなっていた(売上・回復なし)";
            }
            return Fmt("%sの利用が終わった(売上 %.0f円 / 生活需要 %+.1f / 体力 -%.1f)", facility.c_str(), e.a, e.b, e.c);
        case SimEventKind::StopoverCheck:
            switch (e.reason)
            {
            case SimReason::StopRollHit:      return Fmt("立ち寄り%d回目の抽選に当選(確率 %.1f%% > 乱数 %.4f)", e.stopIndex, e.a * 100.0f, e.b);
            case SimReason::StopRollMiss:     return Fmt("立ち寄り%d回目の抽選に外れた → 寄らずに帰る(確率 %.1f%% ≦ 乱数 %.4f)", e.stopIndex, e.a * 100.0f, e.b);
            case SimReason::StopMaxReached:   return "立ち寄りを考えない: 立ち寄りの回数が上限";
            case SimReason::StopTooLate:      return "立ち寄りを考えない: 時刻が遅い";
            case SimReason::StopLowStamina:   return "立ち寄りを考えない: 体力が足りない";
            case SimReason::StopNoHome:       return "立ち寄りを考えない: 帰る家が無い";
            case SimReason::StopHomeMissing:  return "立ち寄りをやめた: 帰る家のオブジェクトが見つからない";
            case SimReason::StopNoRoad:       return "立ち寄りをやめた: 家か今の施設の近くに道路が無い";
            case SimReason::StopNoCandidates: return "立ち寄りをやめた: 同じカテゴリの他の施設が無い";
            case SimReason::StopFoodChain:    return Fmt("立ち寄りをやめた: 食事系が続くので(%s が選ばれたが、受け入れ確率 %.0f%% ≦ 乱数 %.4f)", facility.c_str(), e.a * 100.0f, e.b);
            default:                          return "立ち寄りの判定";
            }
        case SimEventKind::ReturnStart:
            return Fmt("帰り始めた(道のり %.1f)", e.a);
        case SimEventKind::ReturnRerouted:
            return Fmt("道路が無くなり、迂回して帰る(体力 -%.1f)", e.b);
        case SimEventKind::ReturnHome:
            return "家に着いた(外出が終わった)";
        case SimEventKind::OutingAborted:
            return Fmt("%sが解体され、外出が中断された", facility.c_str());
        case SimEventKind::BecameStranded:
            return e.reason == SimReason::RoadCut ? "道路が切り取られ、帰れなくなった(立ち往生)" : "家への経路が無く、立ち往生した";
        case SimEventKind::StrandedRecovered:
            return Fmt("道がつながって、帰り始めた(道のり %.1f)", e.a);
        case SimEventKind::BecameHomeless:
            return "帰る家が無く、住居なしになった";
        case SimEventKind::HomeDemolished:
            return Fmt("住んでいた住宅(#%u)が解体された", e.facilityId);
        case SimEventKind::Anomaly:
            return Fmt("異常: %s(値 %.3f)", AnomalyJa(e.anomaly), e.a);
        case SimEventKind::MovedIn:
            return Fmt("新しい住宅(#%u)への入居が決まった(プレイヤーが選んだ)。今の位置から歩いて向かう", e.facilityId);
        }
        return "?";
    }

    // 住民の外出判定の集計があるか(記録がOFFで、まだ何も数えていない時はfalse)。
    bool HasRollSummary(const SimDebugRecorder& recorder, uint32_t residentId)
    {
        const SimResidentRecord* record = recorder.FindResident(residentId);
        return record && record->roll.checks > 0;
    }

    // 内訳の1行の見出し。
    void Heading(const char* text)
    {
        ImGui::Spacing();
        ImGui::TextColored(kColorHeading, "%s", text);
        ImGui::Separator();
    }

    // 「項目: 値」の1行。
    void Row(const char* label, const std::string& value)
    {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine(150.0f);
        ImGui::TextUnformatted(value.c_str());
    }
}

void ResidentDebugPanel::NotifySelection(uint32_t residentId)
{
    if (residentId != m_lastSelection)
    {
        m_lastSelection = residentId;
        if (residentId != 0 && !m_pinned)
        {
            m_target = residentId;
            m_targetRunId = 0; // 次のDrawで、今の世界に結びつける
            m_selectedDetailId = 0;
            m_selectedCandidate = -1;
        }
    }
}

void ResidentDebugPanel::ClearTarget()
{
    m_target = 0;
    m_targetRunId = 0;
    m_lastSelection = 0;
    m_selectedDetailId = 0;
    m_selectedCandidate = -1;
}

void ResidentDebugPanel::Draw(DemandSystem& demand, const std::vector<GameObject>& objects)
{
    if (m_open && !*m_open)
    {
        return;
    }
    // 対象を、今の世界に結びつける。ロード・リセットで世界が変わったら、古い対象は無効(同じidが別の住民になることがあるため)。
    // ウィンドウが畳まれている(タブが裏にある)間も、確かめる。
    const uint32_t runId = demand.GetDebugRecorder().GetRunId();
    if (m_target != 0 && m_targetRunId == 0)
    {
        m_targetRunId = runId;
    }
    if (m_target != 0 && m_targetRunId != runId)
    {
        m_target = 0;
        m_targetRunId = 0;
        m_selectedDetailId = 0;
        m_selectedCandidate = -1;
    }

    ImGui::SetNextWindowPos(ImVec2(330.0f, 220.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(820.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("住民デバッグ", m_open))
    {
        ImGui::End();
        return;
    }

    DrawHeader(demand);

    if (m_target == 0)
    {
        ImGui::Spacing();
        ImGui::TextWrapped("住民が選ばれていません。ゲームの画面で、住民か住宅をクリックするか、上の一覧から選んでください。");
        ImGui::TextDisabled("(クリックしても、ゲーム内の時間は止まりません。2回クリックで開く「住民の情報」は、これとは別です)");
        ImGui::End();
        return;
    }

    const ResidentDebugView view = demand.GetResidentDebugView(m_target, objects);
    if (!view.found)
    {
        ImGui::Spacing();
        ImGui::TextColored(kColorWarning, "選んでいた住民(#%u)は、もういません(ロードや再読み込みで入れ替わった可能性があります)。", m_target);
        if (ImGui::Button("選択を外す"))
        {
            m_target = 0;
        }
        ImGui::End();
        return;
    }

    // 異常が出ている時は、タブの外でも気づけるように、赤い帯を出す。
    const SimResidentRecord* record = demand.GetDebugRecorder().FindResident(m_target);
    if (record && record->activeAnomalies != 0)
    {
        ImGui::TextColored(kColorViolation, "!! この住民は、今、不変条件に違反しています(「異常」タブを見てください)");
    }

    if (ImGui::BeginTabBar("##residentdebugtabs"))
    {
        if (ImGui::BeginTabItem("現在"))
        {
            DrawCurrentTab(demand, objects);
            ImGui::EndTabItem();
        }
        const ImGuiTabItemFlags decisionFlags = m_jumpToDetailTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        m_jumpToDetailTab = false;
        if (ImGui::BeginTabItem("判断の詳細", nullptr, decisionFlags))
        {
            DrawDecisionTab(demand, objects);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("処理履歴"))
        {
            DrawHistoryTab(demand, objects);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("異常"))
        {
            DrawAnomalyTab(demand, objects);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void ResidentDebugPanel::DrawHeader(DemandSystem& demand)
{
    SimDebugRecorder& recorder = demand.GetDebugRecorder();

    bool enabled = recorder.IsEnabled();
    if (ImGui::Checkbox("記録する", &enabled))
    {
        recorder.SetEnabled(enabled);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("固定(Pin)", &m_pinned) && !m_pinned)
    {
        m_lastSelection = 0; // 固定を外したら、次の選択に連動する
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ONの間は、ゲームで別の住民・住宅をクリックしても、この住民を表示し続けます。");
    }

    // 住民を一覧から選ぶ。
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    const std::string previewText = (m_target != 0) ? Fmt("住民 #%u", m_target) : std::string("住民を選ぶ...");
    if (ImGui::BeginCombo("##residentpicker", previewText.c_str()))
    {
        for (const ResidentListItem& item : demand.ListResidents())
        {
            ImGui::PushID(static_cast<int>(item.residentId));
            const std::string label = Fmt("住民 #%u  (住宅 %s)  %s", item.residentId,
                                          item.homeObjectId != 0 ? Fmt("#%u", item.homeObjectId).c_str() : "なし", StateJa(item.state));
            if (ImGui::Selectable(label.c_str(), item.residentId == m_target))
            {
                m_target = item.residentId;
                m_targetRunId = recorder.GetRunId();
                m_selectedDetailId = 0;
                m_selectedCandidate = -1;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    // 記録の使用状況(上限で古い記録を捨てた事実も、ここに出す)。
    const SimRecorderStats stats = recorder.GetStats();
    if (!enabled)
    {
        ImGui::TextColored(kColorWarning, "記録がOFFです(今の値だけ見えます。判断や履歴は、ONの間のものだけ残ります)");
    }
    else
    {
        ImGui::TextDisabled("記録: イベント %zu/%zu、判断の内訳 %zu/%zu、約%.0f KB、異常 %llu 件(世界 #%u)",
            stats.eventCount, stats.eventCapacity, stats.detailCount, stats.detailCapacity,
            static_cast<double>(stats.estimatedBytes) / 1024.0, static_cast<unsigned long long>(stats.anomaliesRaised), stats.runId);
        if (stats.eventsDropped > 0 || stats.detailsDropped > 0)
        {
            // 捨てた物だけを出す(判断の内訳は、イベントより容量が小さいので、先に捨てられる)。
            std::string dropped;
            if (stats.eventsDropped > 0) { dropped += Fmt("イベント %llu 件(残っているのは #%llu 番以降)", static_cast<unsigned long long>(stats.eventsDropped), static_cast<unsigned long long>(stats.oldestEventId)); }
            if (stats.detailsDropped > 0) { dropped += std::string(dropped.empty() ? "" : "、") + Fmt("判断の内訳 %llu 件", static_cast<unsigned long long>(stats.detailsDropped)); }
            ImGui::TextColored(kColorWarning, "容量の上限で、古い記録を捨てました: %s", dropped.c_str());
        }
    }
}

void ResidentDebugPanel::DrawCurrentTab(DemandSystem& demand, const std::vector<GameObject>& objects)
{
    const ResidentDebugView view = demand.GetResidentDebugView(m_target, objects);
    const ResidentSimSettings& settings = demand.GetSettings();
    const SimDebugRecorder& recorder = demand.GetDebugRecorder();

    ImGui::BeginChild("##current", ImVec2(0.0f, 0.0f), false);

    Heading("住民");
    Row("住民ID", Fmt("#%u", view.residentId));
    if (view.homeObjectId != 0)
    {
        Row("住宅", Fmt("#%u%s", view.homeObjectId, view.homeExists ? "" : "  (オブジェクトが見つからない!)"));
    }
    else
    {
        Row("住宅", "なし(住居なし。元の住宅の位置に立つ)");
    }
    Row("状態", StateJa(view.state));
    Row("位置", Fmt("(%.2f, %.2f)%s", view.position.x, view.position.z, view.markerVisible ? "" : "  ※建物の中(見た目は非表示)"));
    if (!view.lastDecisionNote.empty())
    {
        Row("直近の判断", view.lastDecisionNote);
    }

    Heading("外出");
    if (view.targetFacilityId != 0)
    {
        Row("行き先", view.targetExists ? TypeLabel(view.targetType, view.targetFacilityId) : Fmt("施設#%u(もう無い!)", view.targetFacilityId));
        Row("利用目的", UsagePurposeName(view.purpose));
    }
    else
    {
        Row("行き先", "なし");
    }
    Row("立ち寄り", Fmt("%d / %d 回%s", view.outingStopCount, settings.maxStopovers, view.usedFoodLikeFacilityThisOuting ? "  (今回の外出で食事系を使った)" : ""));
    if (view.state == ResidentAgentState::Visiting)
    {
        Row("滞在の残り", Fmt("%.2f ゲーム時間", view.visitHoursLeft));
    }

    Heading("需要・体力");
    const float lifeFrac = std::clamp(view.lifeNeed / 100.0f, 0.0f, 1.0f);
    const float activityFrac = std::clamp(view.activityNeed / 100.0f, 0.0f, 1.0f);
    const float staminaFrac = view.staminaCap > 0.0f ? std::clamp(view.stamina / view.staminaCap, 0.0f, 1.0f) : 0.0f;
    ImGui::TextDisabled("生活の需要");
    ImGui::SameLine(150.0f);
    ImGui::ProgressBar(lifeFrac, ImVec2(220.0f, 0.0f), Fmt("%.1f%s", view.lifeNeed, view.lifeNeed < view.needThreshold ? "  不足" : "").c_str());
    ImGui::TextDisabled("アクティビティの需要");
    ImGui::SameLine(150.0f);
    ImGui::ProgressBar(activityFrac, ImVec2(220.0f, 0.0f), Fmt("%.1f%s", view.activityNeed, view.activityNeed < view.needThreshold ? "  不足" : "").c_str());
    ImGui::TextDisabled("体力");
    ImGui::SameLine(150.0f);
    ImGui::ProgressBar(staminaFrac, ImVec2(220.0f, 0.0f), Fmt("%.1f / %.1f", view.stamina, view.staminaCap).c_str());
    Row("ペナルティ", Fmt("%d / %d 段階(体力の上限が %.0f 下がっている)", view.penaltyLevel, view.maxPenaltyLevel,
        static_cast<float>(view.penaltyLevel) * settings.penaltyStaminaCapStep));
    Row("満足度", Fmt("%.1f / %.0f", view.satisfaction, settings.satisfactionMax));
    Row("不足が続いた時間", Fmt("生活 %.2f / アクティビティ %.2f ゲーム時間(猶予 %.1f)", view.unmetLifeHours, view.unmetActivityHours, settings.unmetGraceHours));
    Row("充足が続いた時間", Fmt("%.2f ゲーム時間(回復に必要 %.1f)", view.satisfiedHours, settings.recoverSatisfiedHours));

    Heading("この住民の特徴(1が標準)");
    Row("品質重視度", Fmt("%.2f", view.traits.qualityWeight));
    Row("価格感度", Fmt("%.2f", view.traits.priceSensitivity));
    Row("距離許容度", Fmt("%.2f", view.traits.distanceTolerance));
    Row("自発性", Fmt("%.2f", view.traits.spontaneity));
    Row("需要の減りやすさ", Fmt("%.2f", view.traits.needDecayScale));
    Row("体力の上限の基準", Fmt("%.1f", view.traits.staminaCapBase));

    Heading("移動の経路");
    if (view.waypointCount >= 2)
    {
        Row("向き", view.waypointsHomeToTarget ? "自宅 → 施設の向きに並んでいる" : "施設 → 自宅の向きに並んでいる");
        Row("進み具合", Fmt("waypoint %zu / %zu", view.currentWaypointIndex + 1, view.waypointCount));
        Row("道のり", Fmt("全体 %.2f、残り %.2f", view.routeTotalLength, view.routeRemainingLength));
        Row("通る道路区間", Fmt("%zu 区間", view.segmentCount));
    }
    else
    {
        Row("経路", "なし");
    }

    Heading("次の外出判定");
    if (view.state == ResidentAgentState::AtHome)
    {
        std::string how;
        switch (view.nextOutingBranch)
        {
        case OutingChanceBranch::NeedDriven:
            how = Fmt("需要が不足している → 不足時の外出確率 %.1f%%", settings.needDrivenChancePerTick * 100.0f);
            break;
        case OutingChanceBranch::Spontaneous:
            how = Fmt("需要は足りている → 自発の確率 %.1f%% × 自発性 %.2f = %.1f%%", settings.spontaneousChancePerTick * 100.0f, view.traits.spontaneity, view.nextOutingChance * 100.0f);
            break;
        case OutingChanceBranch::SpontaneousLowStamina:
            how = Fmt("需要は足りているが、体力が %.0f 未満 → 確率 0%%(乱数は引くが、必ず不発)", settings.spontaneousMinStamina);
            break;
        }
        Row("1回の判定の確率", Fmt("%.1f%%", view.nextOutingChance * 100.0f));
        Row("理由", how);

        const SimTickGate& gate = recorder.GetTickGate();
        if (gate.valid && !gate.canDepart)
        {
            const char* why = gate.sleeping ? "就寝中" : (gate.tooLate ? "遅い時刻(これ以降は新しく外出しない)" : (gate.noFacilities ? "施設が1軒も無い" : "?"));
            ImGui::TextColored(kColorWarning, "直近のティック(週%d %s)では、新しい外出を考えない時間でした: %s", gate.week, HourText(gate.hour).c_str(), why);
        }
        if (HasRollSummary(recorder, m_target))
        {
            const SimResidentRecord* record = recorder.FindResident(m_target);
            const SimOutingRollSummary& roll = record->roll;
            Row("判定の集計", Fmt("%u 回判定、%u 回当選、今の連続不発 %u 回", roll.checks, roll.hits, roll.consecutiveMisses));
            Row("直近の判定", Fmt("週%d %s  確率 %.1f%% / 乱数 %.4f → %s", roll.lastWeek, HourText(roll.lastHour).c_str(), roll.lastChance * 100.0f, roll.lastRoll, roll.lastHit ? "当選" : "不発"));
        }
        else if (!recorder.IsEnabled())
        {
            ImGui::TextDisabled("(記録がOFFなので、判定の集計は取っていません)");
        }
    }
    else
    {
        ImGui::TextDisabled("家にいる時だけ、外出を考えます(今は %s)", StateJa(view.state));
    }

    Heading("最近使った施設(新しい順)");
    if (view.history.empty())
    {
        ImGui::TextDisabled("まだ、施設を使っていません");
    }
    for (size_t i = 0; i < view.history.size(); ++i)
    {
        const VisitRecord& visit = view.history[i];
        ImGui::Text("%zu. %s  (週%d %s)%s", i + 1, FacilityLabel(objects, visit.facilityObjectId).c_str(), visit.weekIndex, HourText(visit.hour).c_str(),
                    static_cast<int>(i) < settings.repeatVisitWindow ? "  ← 再訪補正の対象" : "");
    }
    ImGui::EndChild();
}

void ResidentDebugPanel::DrawDecisionTab(DemandSystem& demand, const std::vector<GameObject>& objects)
{
    const SimDebugRecorder& recorder = demand.GetDebugRecorder();
    if (!recorder.IsEnabled() && recorder.Events().empty())
    {
        ImGui::TextDisabled("記録がOFFです。「記録する」をONにすると、これからの判断が残ります。");
        return;
    }

    // この住民の、判断(目的選択のイベント)を、新しい順に集める。
    std::vector<const SimEvent*> decisions;
    for (const SimEvent* event : recorder.RecentEventsOf(m_target, 600))
    {
        if (event->kind == SimEventKind::PurposeChoice && event->detailId != 0)
        {
            decisions.push_back(event);
            if (decisions.size() >= 40) { break; }
        }
    }
    if (decisions.empty())
    {
        ImGui::TextDisabled("この住民の判断の記録は、まだありません(外出を考えるまで待つ、または、記録の容量の上限で捨てられました)。");
        return;
    }

    // 選択中の判断を決める(0か、無くなっていれば、最新)。
    const SimEvent* selected = decisions.front();
    for (const SimEvent* event : decisions)
    {
        if (event->detailId == m_selectedDetailId) { selected = event; break; }
    }
    m_selectedDetailId = selected->detailId;

    auto label = [&](const SimEvent& e)
    {
        return Fmt("週%d %s  外出#%u  %s", e.week, HourText(e.hour).c_str(), e.outingId,
                   e.stopIndex > 0 ? Fmt("立ち寄り%d回目", e.stopIndex).c_str() : "主目的");
    };
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::BeginCombo("判断を選ぶ", label(*selected).c_str()))
    {
        for (const SimEvent* event : decisions)
        {
            ImGui::PushID(static_cast<int>(event->eventId));
            if (ImGui::Selectable(label(*event).c_str(), event->detailId == m_selectedDetailId))
            {
                m_selectedDetailId = event->detailId;
                m_selectedCandidate = -1;
                selected = event;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    const SimDecisionDetail* detail = recorder.FindDetail(selected->detailId);
    if (!detail)
    {
        ImGui::TextColored(kColorWarning, "この判断の内訳は、容量の上限(%zu件)で捨てられました。", recorder.GetStats().detailCapacity);
        return;
    }
    ImGui::BeginChild("##decisiondetail", ImVec2(0.0f, 0.0f), false);
    DrawDecisionDetail(*detail, objects);
    ImGui::EndChild();
}

void ResidentDebugPanel::DrawDecisionDetail(const SimDecisionDetail& detail, const std::vector<GameObject>& objects)
{
    (void)objects;
    const ResidentSimSettings& s = detail.settings;
    const PurposeTrace& trace = detail.purposeTrace;

    Heading("判断した時の状態");
    Row("いつ", Fmt("週%d %s  外出#%u  %s", detail.week, HourText(detail.hour).c_str(), detail.outingId,
                   detail.isStopover ? Fmt("立ち寄り%d回目の判断", detail.stopIndex).c_str() : "主目的の判断"));
    Row("体力・需要", Fmt("体力 %.1f / 生活 %.1f / アクティビティ %.1f", detail.stamina, detail.lifeNeed, detail.activityNeed));

    if (detail.isStopover)
    {
        Heading("立ち寄りの抽選");
        const bool hit = detail.stopRoll >= 0.0f && detail.stopRoll < detail.stopChance;
        Row("確率", Fmt("基本 %.0f%% × 減衰 %.2f ^ これまでの立ち寄り %d 回 = %.1f%%", s.stopoverBaseChance * 100.0f, s.stopoverChanceDecay, detail.stopIndex - 1, detail.stopChance * 100.0f));
        Row("乱数", Fmt("%.4f  → %s", detail.stopRoll, hit ? "当選(乱数 < 確率)" : "外れ"));
    }

    Heading("目的の決定");
    {
        std::string how;
        if (trace.categoryDrawn)
        {
            how = Fmt("需要は足りている → 自発的な外出。カテゴリは乱数 %.4f %s %.0f%% → %s", trace.categoryRoll,
                      trace.categoryRoll < trace.leisureChance ? "<" : ">=", trace.leisureChance * 100.0f, FacilityCategoryName(detail.purpose.category));
        }
        else if (trace.lifeUnmet && trace.activityUnmet)
        {
            how = trace.tieBreakDrawn
                ? Fmt("生活・アクティビティが同じだけ不足 → 乱数 %.4f で決定 → %s", trace.tieBreakRoll, FacilityCategoryName(detail.purpose.category))
                : Fmt("両方不足 → より不足している方を優先 → %s", FacilityCategoryName(detail.purpose.category));
        }
        else
        {
            how = Fmt("%sが不足 → %s", trace.lifeUnmet ? "生活の需要" : "アクティビティの需要", FacilityCategoryName(detail.purpose.category));
        }
        Row("カテゴリ", how);
        if (trace.purposeRoll >= 0.0f)
        {
            if (detail.purpose.category == FacilityCategory::Daily)
            {
                Row("利用目的", Fmt("まとめ買いの確率 %.1f%%(不足の割合 %.2f から) / 乱数 %.4f → %s", trace.purposeChance * 100.0f, trace.lifeDeficit, trace.purposeRoll, UsagePurposeName(detail.purpose.purpose)));
            }
            else
            {
                Row("利用目的", Fmt("映画の確率 %.0f%% / 乱数 %.4f → %s", trace.purposeChance * 100.0f, trace.purposeRoll, UsagePurposeName(detail.purpose.purpose)));
            }
        }
    }

    if (detail.evaluations.empty())
    {
        ImGui::Spacing();
        ImGui::TextColored(kColorNotMet, "候補になる施設が1軒もありませんでした(目的のカテゴリの施設が無い)。");
        return;
    }

    Heading("候補の評価");
    if (!detail.history.empty())
    {
        std::string historyText;
        for (size_t i = 0; i < detail.history.size() && i < static_cast<size_t>((std::max)(1, s.repeatVisitWindow)); ++i)
        {
            historyText += (i ? " , " : "") + Fmt("#%u", detail.history[i].facilityObjectId);
        }
        ImGui::TextDisabled("判断した時の利用履歴(再訪補正の対象): %s", historyText.c_str());
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit;
    // 高さは、行数に合わせる(横スクロールを付けると、指定しない限り、残りの高さいっぱいに広がってしまうため)。
    const float tableHeight = (static_cast<float>(detail.evaluations.size()) + 1.0f) * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ScrollbarSize + 8.0f;
    if (ImGui::BeginTable("##candidates", 9, flags, ImVec2(0.0f, tableHeight)))
    {
        ImGui::TableSetupColumn("候補");
        ImGui::TableSetupColumn("道のり(マス)");
        ImGui::TableSetupColumn("採否");
        ImGui::TableSetupColumn("基本の重み");
        ImGui::TableSetupColumn("再訪倍率");
        ImGui::TableSetupColumn("最終の重み");
        ImGui::TableSetupColumn("選択確率");
        ImGui::TableSetupColumn("抽選の範囲");
        ImGui::TableSetupColumn("結果");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < detail.evaluations.size(); ++i)
        {
            const CandidateEvaluation& ev = detail.evaluations[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool isFormulaTarget = (m_selectedCandidate >= 0) ? (static_cast<int>(i) == m_selectedCandidate) : (static_cast<int>(i) == detail.choice.pickedIndex);
            if (ImGui::Selectable(TypeLabel(ev.candidate.type, ev.candidate.objectId).c_str(), isFormulaTarget, ImGuiSelectableFlags_SpanAllColumns))
            {
                m_selectedCandidate = static_cast<int>(i);
            }
            ImGui::TableNextColumn();
            if (ev.hasBreakdown) { ImGui::Text("%.1f", ev.breakdown.legacyDistance); }
            else if (ev.candidate.reachable) { ImGui::Text("%.1f", ev.candidate.pathLength / WorldScale::FromLegacy(1.0f)); }
            else { ImGui::TextDisabled("-"); }
            ImGui::TableNextColumn();
            if (ev.reject == CandidateReject::None) { ImGui::TextUnformatted("対象"); }
            else if (ev.reject == CandidateReject::NotEnoughStamina) { ImGui::TextColored(kColorNotMet, "除外: 体力不足(必要 %.1f > 体力 %.1f)", ev.requiredStamina, detail.stamina); }
            else { ImGui::TextColored(kColorNotMet, "%s", RejectJa(ev.reject)); }
            ImGui::TableNextColumn();
            if (ev.hasBreakdown) { ImGui::Text("%.4f", ev.breakdown.weight); } else { ImGui::TextDisabled("-"); }
            ImGui::TableNextColumn();
            if (ev.hasBreakdown) { ImGui::Text("x %.3f", ev.repeatFactor); } else { ImGui::TextDisabled("-"); }
            ImGui::TableNextColumn();
            if (ev.hasBreakdown) { ImGui::Text("%.4f", ev.weight); } else { ImGui::TextDisabled("-"); }
            ImGui::TableNextColumn();
            if (ev.hasBreakdown) { ImGui::Text("%.1f%%", ev.chance * 100.0f); } else { ImGui::TextDisabled("-"); }
            ImGui::TableNextColumn();
            if (ev.hasBreakdown) { ImGui::Text("%.3f 〜 %.3f", ev.rangeBegin, ev.rangeEnd); } else { ImGui::TextDisabled("-"); }
            ImGui::TableNextColumn();
            if (static_cast<int>(i) == detail.choice.pickedIndex)
            {
                if (detail.chosenIndex == static_cast<int>(i)) { ImGui::TextColored(kColorSuccess, "選ばれた"); }
                else { ImGui::TextColored(kColorNotMet, "抽選では選ばれたが取りやめ"); }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // 乱数と、抽選の結果。
    Heading("施設の抽選");
    if (detail.choice.randomUnit >= 0.0f)
    {
        Row("乱数", Fmt("%.4f(0以上1未満)", detail.choice.randomUnit));
        Row("重みの合計", Fmt("%.4f", detail.choice.totalWeight));
        Row("乱数 × 合計", Fmt("%.4f", detail.choice.pickTarget));
        if (detail.choice.pickedIndex >= 0)
        {
            const CandidateEvaluation& picked = detail.evaluations[static_cast<size_t>(detail.choice.pickedIndex)];
            Row("該当する候補", Fmt("%s(範囲 %.3f 〜 %.3f に %.3f が入る)", TypeLabel(picked.candidate.type, picked.candidate.objectId).c_str(),
                                    picked.rangeBegin, picked.rangeEnd, detail.choice.pickTarget));
            if (detail.evaluations.size() == 1)
            {
                ImGui::TextDisabled("※候補が1軒だけなので、重みが低くても選択確率は100%%です");
            }
        }
        else
        {
            ImGui::TextColored(kColorNotMet, "選べる候補がありませんでした(全て除外)");
        }
    }

    if (detail.foodChainChecked)
    {
        Heading("食事系の連続(立ち寄り)");
        Row("受け入れ確率", Fmt("%.0f%%(同じ外出で食事系を使った後に、別の食事系へ寄る確率)", detail.foodChainAcceptChance * 100.0f));
        Row("乱数", Fmt("%.4f  → %s", detail.foodChainRoll, detail.foodChainPassed ? "通った" : "寄らないことにした"));
    }

    // 選んだ候補の計算式(日本語)。実際の計算で使った値をそのまま出す。
    int formulaIndex = (m_selectedCandidate >= 0 && m_selectedCandidate < static_cast<int>(detail.evaluations.size())) ? m_selectedCandidate : detail.choice.pickedIndex;
    if (formulaIndex >= 0 && formulaIndex < static_cast<int>(detail.evaluations.size()) && detail.evaluations[static_cast<size_t>(formulaIndex)].hasBreakdown)
    {
        const CandidateEvaluation& ev = detail.evaluations[static_cast<size_t>(formulaIndex)];
        const FacilityWeightBreakdown& b = ev.breakdown;
        Heading(Fmt("計算式: %s(表の行をクリックで切り替え)", TypeLabel(ev.candidate.type, ev.candidate.objectId).c_str()).c_str());

        ImGui::TextWrapped("基本の重み = 目的への適性 %.2f × 品質係数 %.4f × 値段係数 %.4f × 距離係数 %.4f = %.5f", b.suitability, b.qualityFactor, b.priceFactor, b.distanceFactor, b.product);
        ImGui::TextDisabled("  品質係数 = 施設の品質 %.2f ^ (品質の効き方 %.2f × 品質重視度 %.2f = %.3f) = %.4f", b.quality, s.qualityExponent, detail.traits.qualityWeight, b.qualityExponent, b.qualityFactor);
        ImGui::TextDisabled("  値段係数 = 1 ÷ (1 + (値段の効き方 %.2f × 価格感度 %.2f = %.3f) × (値段 %.0f円 ÷ 基準 %.0f円 = %.3f)) = %.4f",
                            s.priceStrength, detail.traits.priceSensitivity, b.priceStrength, b.priceYen, s.priceReferenceYen, b.priceRatio, b.priceFactor);
        ImGui::TextDisabled("  距離係数 = exp(-道のり %.2f マス ÷ (距離の目安 %.1f × 距離許容度 %.2f = %.2f)) = %.4f",
                            b.legacyDistance, s.distanceScaleLegacyCells, detail.traits.distanceTolerance, b.distanceScale, b.distanceFactor);
        ImGui::TextWrapped("下限の適用 = max(%.5f, 重みの下限 %.4f) = %.5f", b.product, b.floorWeight, b.weight);
        ImGui::TextWrapped("再訪補正後の重み = 基本の重み %.5f × 直近利用履歴による倍率 %.3f = %.5f", b.weight, ev.repeatFactor, ev.weightAfterRepeat);
        ImGui::TextWrapped("最終の重み = max(再訪補正後 %.5f, 重みの下限 %.4f) = %.5f", ev.weightAfterRepeat, s.minimumWeight, ev.weight);
        ImGui::TextWrapped("選択確率 = 最終の重み %.5f ÷ 抽選対象の重みの合計 %.5f = %.2f%%", ev.weight, detail.choice.totalWeight, ev.chance * 100.0f);
        if (ev.requiredStamina >= 0.0f)
        {
            ImGui::TextDisabled("体力の確認: 必要 %.1f ≦ 体力 %.1f(往復の移動と利用)", ev.requiredStamina, detail.stamina);
        }
        if (detail.needDrivenDaily)
        {
            ImGui::TextDisabled("※不足による日用品の外出なので、再訪補正の下げ幅は %.0f%% に弱めてあります", s.needDrivenDailyRepeatStrength * 100.0f);
        }
    }

    if (ImGui::CollapsingHeader("判断した時の設定(スライダーを動かしても、この判断の値は変わりません)"))
    {
        ImGui::TextDisabled("品質の効き方 %.2f / 値段の基準 %.0f円 / 値段の効き方 %.2f / 距離の目安 %.1f マス / 重みの下限 %.4f",
                            s.qualityExponent, s.priceReferenceYen, s.priceStrength, s.distanceScaleLegacyCells, s.minimumWeight);
        ImGui::TextDisabled("再訪: 直近 %d 回 / 直前の倍率 %.2f / 不足の日用品の割合 %.2f", s.repeatVisitWindow, s.repeatVisitWeightFactor, s.needDrivenDailyRepeatStrength);
        ImGui::TextDisabled("立ち寄り: 最大 %d 回 / 基本 %.0f%% / 減衰 %.2f / 体力 %.0f 以上 / %.1f 時まで / 範囲 %.1f マス / 食事系 %.0f%%",
                            s.maxStopovers, s.stopoverBaseChance * 100.0f, s.stopoverChanceDecay, s.stopoverMinStamina, s.stopoverLatestHour,
                            s.stopoverMaxDistanceLegacyCells, s.foodChainAcceptChance * 100.0f);
    }
}

void ResidentDebugPanel::DrawHistoryTab(DemandSystem& demand, const std::vector<GameObject>& objects)
{
    const SimDebugRecorder& recorder = demand.GetDebugRecorder();
    if (!recorder.IsEnabled() && recorder.Events().empty())
    {
        ImGui::TextDisabled("記録がOFFです。「記録する」をONにすると、これからの処理が残ります。");
        return;
    }

    ImGui::Checkbox("不成立(灰)も出す", &m_showNotMet);
    ImGui::SameLine();
    ImGui::Checkbox("注意(黄)と異常(赤)だけ", &m_onlyProblems);
    ImGui::TextDisabled("色: 灰=普通の不成立(確率・条件)、黄=注意(設計上ありえる)、赤=確認された異常。意外な抽選結果は赤になりません。");

    // この住民のイベント(新しい順)。同じ外出ごとにまとめる。
    const std::vector<const SimEvent*> events = recorder.RecentEventsOf(m_target, 500);
    if (events.empty())
    {
        ImGui::TextDisabled("この住民の記録は、まだありません。");
        return;
    }

    auto visible = [&](const SimEvent& e)
    {
        if (m_onlyProblems) { return e.result == SimEventResult::Warning || e.result == SimEventResult::Violation; }
        if (!m_showNotMet && e.result == SimEventResult::NotMet) { return false; }
        return true;
    };

    ImGui::BeginChild("##history", ImVec2(0.0f, 0.0f), false);
    size_t begin = 0;
    bool firstGroup = true;
    while (begin < events.size())
    {
        // 同じ外出の番号が続く範囲を、1つのグループにする(新しい順に並んでいるので、範囲の最後が一番古い)。
        size_t end = begin;
        while (end < events.size() && events[end]->outingId == events[begin]->outingId) { ++end; }

        bool anyVisible = false, anyWarning = false, anyViolation = false;
        for (size_t i = begin; i < end; ++i)
        {
            anyVisible = anyVisible || visible(*events[i]);
            anyWarning = anyWarning || events[i]->result == SimEventResult::Warning;
            anyViolation = anyViolation || events[i]->result == SimEventResult::Violation;
        }
        if (anyVisible)
        {
            const SimEvent& oldest = *events[end - 1];
            const SimEvent& newest = *events[begin];
            int visits = 0;
            for (size_t i = begin; i < end; ++i) { visits += (events[i]->kind == SimEventKind::VisitFinished && events[i]->result == SimEventResult::Success) ? 1 : 0; }
            std::string title = (oldest.outingId != 0)
                ? Fmt("外出 #%u  週%d %s〜  施設 %d 回利用  [%s]", oldest.outingId, oldest.week, HourText(oldest.hour).c_str(), visits, EventKindJa(newest.kind))
                : Fmt("外出に属さない記録  週%d %s〜", oldest.week, HourText(oldest.hour).c_str());
            title += Fmt("###outing%u_%llu", oldest.outingId, static_cast<unsigned long long>(oldest.eventId));

            ImGui::PushStyleColor(ImGuiCol_Text, anyViolation ? kColorViolation : (anyWarning ? kColorWarning : kColorNormal));
            const bool open = ImGui::TreeNodeEx(title.c_str(), firstGroup ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
            ImGui::PopStyleColor();
            if (open)
            {
                for (size_t i = end; i > begin; --i) // 古い順に
                {
                    const SimEvent& e = *events[i - 1];
                    if (!visible(e)) { continue; }
                    ImGui::PushID(static_cast<int>(e.eventId));
                    ImGui::PushStyleColor(ImGuiCol_Text, ColorOf(e.result));
                    ImGui::Text("%s  [%s] %s", HourText(e.hour).c_str(), EventKindJa(e.kind), DescribeEvent(e, objects).c_str());
                    ImGui::PopStyleColor();
                    if (e.detailId != 0 && (e.kind == SimEventKind::PurposeChoice || e.kind == SimEventKind::FacilitySelect))
                    {
                        ImGui::SameLine();
                        if (recorder.FindDetail(e.detailId))
                        {
                            if (ImGui::SmallButton("詳細"))
                            {
                                m_selectedDetailId = e.detailId;
                                m_selectedCandidate = -1;
                                m_jumpToDetailTab = true;
                            }
                        }
                        else
                        {
                            ImGui::TextDisabled("(内訳は捨てられた)");
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            firstGroup = false;
        }
        begin = end;
    }
    ImGui::EndChild();
}

void ResidentDebugPanel::DrawAnomalyTab(DemandSystem& demand, const std::vector<GameObject>& objects)
{
    const SimDebugRecorder& recorder = demand.GetDebugRecorder();
    const SimRecorderStats stats = recorder.GetStats();

    ImGui::TextWrapped("確認された不変条件の違反だけを赤で出します(体力・需要の範囲、NaN/Inf、経路の添字、行き先・家の参照など)。意外な抽選結果や、設計上ありえる状態は、異常にしません。");
    ImGui::TextDisabled("この世界で検出した異常: %llu 件", static_cast<unsigned long long>(stats.anomaliesRaised));
    if (!recorder.IsEnabled())
    {
        ImGui::TextColored(kColorWarning, "記録がOFFなので、異常の検出もしていません。");
        return;
    }

    Heading("この住民");
    const SimResidentRecord* record = recorder.FindResident(m_target);
    if (record && record->activeAnomalies != 0)
    {
        for (uint32_t code = 1; code < 32; ++code)
        {
            if (record->activeAnomalies & (1u << code))
            {
                ImGui::TextColored(kColorViolation, "今、違反中: %s", AnomalyJa(static_cast<SimAnomaly>(code)));
            }
        }
    }
    else
    {
        ImGui::TextColored(kColorSuccess, "今、違反はありません");
    }

    Heading("検出した異常(全住民、新しい順)");
    int shown = 0;
    const std::deque<SimEvent>& all = recorder.Events();
    for (auto it = all.rbegin(); it != all.rend() && shown < 30; ++it)
    {
        if (it->kind != SimEventKind::Anomaly) { continue; }
        ImGui::PushID(static_cast<int>(it->eventId));
        ImGui::TextColored(kColorViolation, "週%d %s  住民#%u  %s", it->week, HourText(it->hour).c_str(), it->residentId, DescribeEvent(*it, objects).c_str());
        if (it->residentId != m_target)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("この住民を見る"))
            {
                m_target = it->residentId;
                m_targetRunId = recorder.GetRunId();
                m_selectedDetailId = 0;
            }
        }
        ImGui::PopID();
        ++shown;
    }
    if (shown == 0)
    {
        ImGui::TextDisabled("異常は検出されていません");
    }
}
