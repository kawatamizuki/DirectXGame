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
            SetTarget(residentId);
        }
    }
}

void ResidentDebugPanel::SetTarget(uint32_t residentId)
{
    m_target = residentId;
    m_targetRunId = 0; // 次のDrawで、今の世界に結びつける
    m_selectedDetailId = 0;
    m_selectedCandidate = -1;
    m_decisionManual = false;
    m_live = true;     // 別の住民へ切り替えたら、ライブ(最新を追う)に戻す
    m_cursorEventId = 0;
    m_cursorIndex = -1;
    m_timeline.clear();
}

void ResidentDebugPanel::ClearTarget()
{
    SetTarget(0);
    m_lastSelection = 0;
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
        SetTarget(0);
    }
    if (m_bookmarksRunId != runId)
    {
        // 別の世界になった: ブックマークも、止まった印も、前の世界の物。
        m_bookmarks.clear();
        m_bookmarksRunId = runId;
        m_lastPausedSeen = 0;
    }

    // 一時停止の条件に当てはまって、ゲームが止まった(新しい停止)時は、そのイベントを、履歴として見せる(止まった理由を、すぐ読めるように)。
    // 固定(Pin)中は、別の住民へは切り替えない(「このイベントを見る」ボタンで、切り替えられる)。
    {
        SimDebugRecorder& recorder = demand.GetDebugRecorder();
        if (recorder.IsPauseActive() && recorder.PausedEventId() != m_lastPausedSeen)
        {
            m_lastPausedSeen = recorder.PausedEventId();
            if (const SimEvent* paused = recorder.FindEvent(m_lastPausedSeen))
            {
                if (!m_pinned || m_target == paused->residentId || m_target == 0)
                {
                    SeekToEvent(paused->residentId, paused->eventId);
                }
            }
        }
    }

    ImGui::SetNextWindowPos(ImVec2(330.0f, 220.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(900.0f, 740.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("住民デバッグ", m_open))
    {
        ImGui::End();
        return;
    }

    DrawHeader(demand, objects);

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

    // 履歴のナビゲーション(ライブ/履歴・シークバー・絞り込み・一時停止の条件・ブックマーク)。
    BuildNavigation(demand, objects);
    DrawNavigator(demand, objects);

    if (ImGui::BeginTabBar("##residentdebugtabs"))
    {
        if (ImGui::BeginTabItem("現在"))
        {
            // 履歴を見ている時は、カーソルのイベントに記録された、その時点の状態を出す(今の値で、上書きしない)。
            const SimEvent* cursorEvent = (!m_live && m_cursorIndex >= 0) ? m_timeline[static_cast<size_t>(m_cursorIndex)] : nullptr;
            if (cursorEvent)
            {
                DrawSnapshotView(*cursorEvent, demand, objects);
            }
            else
            {
                DrawCurrentTab(demand, objects);
            }
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

void ResidentDebugPanel::DrawHeader(DemandSystem& demand, const std::vector<GameObject>& objects)
{
    SimDebugRecorder& recorder = demand.GetDebugRecorder();

    // 一時停止の条件に当てはまって、ゲームが止まっている間は、一番上に出す(ゲームを止めたのは、このパネルの条件)。
    if (recorder.IsPauseActive())
    {
        const SimEvent* paused = recorder.FindEvent(recorder.PausedEventId());
        ImGui::PushStyleColor(ImGuiCol_Text, kColorWarning);
        if (paused)
        {
            ImGui::Text("一時停止中: 住民 #%u のイベント #%llu で、止まりました — [%s] %s", paused->residentId,
                static_cast<unsigned long long>(paused->eventId), EventKindJa(paused->kind), DescribeEvent(*paused, objects).c_str());
        }
        else
        {
            ImGui::TextUnformatted("一時停止中(止めたイベントの記録は、上限で捨てられました)");
        }
        ImGui::PopStyleColor();
        if (ImGui::Button("再開"))
        {
            recorder.ClearPause(); // 次のフレームで、時間が元の倍率に戻る(Game側)
        }
        if (paused)
        {
            ImGui::SameLine();
            if (ImGui::Button("このイベントを見る"))
            {
                SeekToEvent(paused->residentId, paused->eventId);
            }
        }
        ImGui::Separator();
    }

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
                SetTarget(item.residentId);
                m_targetRunId = recorder.GetRunId();
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

// ---------------------------------------------------------------------------
// 履歴のナビゲーション(段階2)
// ---------------------------------------------------------------------------
void ResidentDebugPanel::GoLive()
{
    m_live = true;
    m_scrollToCursor = false;
    m_decisionManual = false; // 判断の詳細も、また最新を追う
}

void ResidentDebugPanel::SeekToIndex(int index)
{
    if (m_timeline.empty())
    {
        return;
    }
    index = (std::max)(0, (std::min)(index, static_cast<int>(m_timeline.size()) - 1));
    m_cursorIndex = index;
    m_cursorEventId = m_timeline[static_cast<size_t>(index)]->eventId;
    m_live = false;
    m_scrollToCursor = true;
    m_syncDecisionPending = true;
}

void ResidentDebugPanel::SeekToEvent(uint32_t residentId, uint64_t eventId)
{
    if (residentId != m_target)
    {
        SetTarget(residentId);
    }
    m_live = false;
    m_cursorEventId = eventId;
    m_cursorIndex = -1;          // 次の BuildNavigation で、時間軸の中の位置を求める
    m_scrollToCursor = true;
    m_syncDecisionPending = true;
}

void ResidentDebugPanel::SyncDecisionToCursor(const SimDebugRecorder& recorder)
{
    // カーソルの時点(そのイベントを含む)より前で、一番新しい「目的選択」を探す。絞り込みで隠れていても、記録全体から探す。
    const std::deque<SimEvent>& events = recorder.Events();
    for (auto it = events.rbegin(); it != events.rend(); ++it)
    {
        if (it->residentId != m_target || it->eventId > m_cursorEventId)
        {
            continue;
        }
        if (it->kind == SimEventKind::PurposeChoice && it->detailId != 0)
        {
            m_selectedDetailId = it->detailId;
            m_selectedCandidate = -1;
            m_decisionManual = true; // カーソルの時点の判断を、見る(ライブで、最新へ自動で切り替わらない)
            return;
        }
    }
}

void ResidentDebugPanel::BuildNavigation(DemandSystem& demand, const std::vector<GameObject>& objects)
{
    const SimDebugRecorder& recorder = demand.GetDebugRecorder();

    // 捨てられたイベントのブックマークは、外す(記録に無いので、移れない)。
    if (!recorder.Events().empty())
    {
        const uint64_t oldest = recorder.Events().front().eventId;
        m_bookmarks.erase(m_bookmarks.begin(), m_bookmarks.lower_bound(oldest));
    }
    else
    {
        m_bookmarks.clear();
    }

    // 時間軸 = この住民の、絞り込みを通ったイベント。ブックマークしたイベントは、絞り込みで隠さない。
    const std::string search = m_search;
    const bool onlyProblems = m_onlyProblems;
    const bool showNotMet = m_showNotMet;
    auto pass = [&](const SimEvent& e)
    {
        if (m_bookmarks.count(e.eventId) != 0)
        {
            return true;
        }
        if (onlyProblems && e.result != SimEventResult::Warning && e.result != SimEventResult::Violation)
        {
            return false;
        }
        if (!showNotMet && e.result == SimEventResult::NotMet)
        {
            return false;
        }
        if (!search.empty())
        {
            const std::string text = std::string(EventKindJa(e.kind)) + " " + DescribeEvent(e, objects);
            if (text.find(search) == std::string::npos)
            {
                return false;
            }
        }
        return true;
    };
    m_timeline = BuildTimeline(recorder, m_target, pass);

    // カーソル。ライブなら、いつも最新。履歴なら、見ているイベント(絞り込みで外れた時は、それより前の一番近いイベント)。
    m_cursorClamped = false;
    if (m_timeline.empty())
    {
        m_cursorIndex = -1;
    }
    else if (m_live)
    {
        m_cursorIndex = static_cast<int>(m_timeline.size()) - 1;
        m_cursorEventId = m_timeline.back()->eventId;
    }
    else
    {
        m_cursorIndex = FindTimelineIndex(m_timeline, m_cursorEventId, &m_cursorClamped);
    }

    if (m_syncDecisionPending)
    {
        m_syncDecisionPending = false;
        SyncDecisionToCursor(recorder);
    }
}

void ResidentDebugPanel::DrawNavigator(DemandSystem& demand, const std::vector<GameObject>& objects)
{
    SimDebugRecorder& recorder = demand.GetDebugRecorder();
    const int count = static_cast<int>(m_timeline.size());
    const SimEvent* cursor = (m_cursorIndex >= 0 && m_cursorIndex < count) ? m_timeline[static_cast<size_t>(m_cursorIndex)] : nullptr;

    ImGui::Separator();

    // ---- ライブ / 履歴 ----
    if (m_live)
    {
        ImGui::TextColored(kColorSuccess, "ライブ: 最新のイベントを追っています");
    }
    else
    {
        ImGui::TextColored(kColorWarning, "履歴を表示中(記録の閲覧です)。現在の街は巻き戻っていません。ゲームは、そのまま進んでいます");
        ImGui::SameLine();
        if (ImGui::SmallButton("ライブに戻る"))
        {
            GoLive();
        }
        if (cursor)
        {
            ImGui::TextColored(kColorWarning, "見ているイベント: #%llu  週%d %s  [%s]", static_cast<unsigned long long>(cursor->eventId), cursor->week,
                HourText(cursor->hour).c_str(), EventKindJa(cursor->kind));
            if (m_cursorClamped)
            {
                ImGui::TextColored(kColorWarning, "(見ていたイベントは、上限で捨てられた・絞り込みで外れたので、一番古い物に寄せました)");
            }
        }
        else
        {
            ImGui::TextColored(kColorWarning, "(この住民の記録は、見つかりません)");
        }
    }

    // ---- 絞り込み・検索(時間軸と、処理履歴の両方に効く) ----
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##resdbgsearch", "検索(種類・説明の文字)", m_search, sizeof(m_search));
    ImGui::SameLine();
    ImGui::Checkbox("不成立(灰)も出す", &m_showNotMet);
    ImGui::SameLine();
    ImGui::Checkbox("注意(黄)と異常(赤)だけ", &m_onlyProblems);

    if (count == 0)
    {
        ImGui::TextDisabled("時間軸に出せるイベントが、ありません(まだ記録が無い・絞り込みで、全て外れた)");
    }
    else
    {
        // ---- シークバー(記録されたイベントの並びを、ドラッグで選ぶ。街の状態は、動かさない) ----
        int index = m_cursorIndex;
        ImGui::SetNextItemWidth((std::max)(120.0f, ImGui::GetContentRegionAvail().x - 200.0f));
        if (ImGui::SliderInt("##resdbgseek", &index, 0, (std::max)(0, count - 1), ""))
        {
            SeekToIndex(index);
        }
        // 目印: 注意(黄)・異常(赤)・ブックマーク(黄の星の位置)を、バーの上に重ねる。
        {
            const ImVec2 pmin = ImGui::GetItemRectMin();
            const ImVec2 pmax = ImGui::GetItemRectMax();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float grab = ImGui::GetStyle().GrabMinSize;
            const float span = (pmax.x - pmin.x) - grab;
            const ImU32 yellow = ImGui::GetColorU32(kColorWarning);
            const ImU32 red = ImGui::GetColorU32(kColorViolation);
            for (int i = 0; i < count; ++i)
            {
                const SimEvent& e = *m_timeline[static_cast<size_t>(i)];
                const bool mark = e.result == SimEventResult::Warning || e.result == SimEventResult::Violation;
                const bool booked = m_bookmarks.count(e.eventId) != 0;
                if (!mark && !booked)
                {
                    continue;
                }
                const float x = pmin.x + grab * 0.5f + (count > 1 ? span * static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f);
                if (mark)
                {
                    draw->AddLine(ImVec2(x, pmin.y), ImVec2(x, pmin.y + 5.0f), e.result == SimEventResult::Violation ? red : yellow, 2.0f);
                }
                if (booked)
                {
                    draw->AddTriangleFilled(ImVec2(x, pmax.y - 1.0f), ImVec2(x - 4.0f, pmax.y + 5.0f), ImVec2(x + 4.0f, pmax.y + 5.0f), yellow);
                }
            }
        }
        ImGui::SameLine();
        ImGui::Text("%d / %d", m_cursorIndex + 1, count);
        ImGui::SameLine();
        ImGui::TextDisabled("(目印: 上の黄=注意、上の赤=異常、下の黄の三角=ブックマーク)");

        // カーソルのイベントの説明。
        if (cursor)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ColorOf(cursor->result));
            ImGui::TextWrapped("週%d %s  [%s] %s", cursor->week, HourText(cursor->hour).c_str(), EventKindJa(cursor->kind), DescribeEvent(*cursor, objects).c_str());
            ImGui::PopStyleColor();
        }

        // ---- 移動ボタン ----
        auto isProblem = [](const SimEvent& e) { return e.result == SimEventResult::Warning || e.result == SimEventResult::Violation; };
        auto isBookmark = [&](const SimEvent& e) { return m_bookmarks.count(e.eventId) != 0; };
        const int at = m_cursorIndex;
        if (ImGui::SmallButton("先頭")) { SeekToIndex(0); }
        ImGui::SameLine();
        if (ImGui::SmallButton("前")) { SeekToIndex(at - 1); }
        ImGui::SameLine();
        if (ImGui::SmallButton("次")) { SeekToIndex(at + 1); }
        ImGui::SameLine();
        if (ImGui::SmallButton("最新(ライブ)")) { GoLive(); }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::SmallButton("前の外出")) { const int i = PreviousOutingIndex(m_timeline, at); if (i >= 0) { SeekToIndex(i); } }
        ImGui::SameLine();
        if (ImGui::SmallButton("次の外出")) { const int i = NextOutingIndex(m_timeline, at); if (i >= 0) { SeekToIndex(i); } }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::SmallButton("前の注意・異常")) { const int i = PreviousMatchIndex(m_timeline, at, isProblem); if (i >= 0) { SeekToIndex(i); } }
        ImGui::SameLine();
        if (ImGui::SmallButton("次の注意・異常")) { const int i = NextMatchIndex(m_timeline, at, isProblem); if (i >= 0) { SeekToIndex(i); } }

        // ---- ブックマーク ----
        if (cursor)
        {
            const bool booked = m_bookmarks.count(cursor->eventId) != 0;
            if (ImGui::SmallButton(booked ? "ブックマークを外す" : "ブックマークする"))
            {
                if (booked) { m_bookmarks.erase(cursor->eventId); } else { m_bookmarks.insert(cursor->eventId); }
            }
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("前のブックマーク")) { const int i = PreviousMatchIndex(m_timeline, at, isBookmark); if (i >= 0) { SeekToIndex(i); } }
        ImGui::SameLine();
        if (ImGui::SmallButton("次のブックマーク")) { const int i = NextMatchIndex(m_timeline, at, isBookmark); if (i >= 0) { SeekToIndex(i); } }
    }

    // ブックマークの一覧(全住民。クリックで、その住民・そのイベントへ)。
    if (ImGui::CollapsingHeader(Fmt("ブックマーク一覧 (%zu)###resdbgbookmarks", m_bookmarks.size()).c_str()))
    {
        if (m_bookmarks.empty())
        {
            ImGui::TextDisabled("まだありません。イベントを選んで「ブックマークする」を押すと、ここに並びます(捨てられた記録のブックマークは、消えます)");
        }
        for (uint64_t id : m_bookmarks)
        {
            const SimEvent* e = recorder.FindEvent(id);
            if (!e) { continue; }
            ImGui::PushID(static_cast<int>(id));
            const std::string label = Fmt("住民#%u  週%d %s  [%s] %s", e->residentId, e->week, HourText(e->hour).c_str(), EventKindJa(e->kind), DescribeEvent(*e, objects).c_str());
            if (ImGui::Selectable(label.c_str(), !m_live && m_cursorEventId == id && m_target == e->residentId))
            {
                SeekToEvent(e->residentId, e->eventId);
            }
            ImGui::PopID();
        }
    }

    // ---- 一時停止の条件(条件に合うイベントが起きたら、ゲームを自動で止める) ----
    if (ImGui::CollapsingHeader("一時停止の条件(イベントが起きたら、ゲームを自動で止める)"))
    {
        SimBreakpoints& bp = recorder.Breakpoints();
        ImGui::TextDisabled("止まるのは、シミュレーションの更新が終わった後です(途中では止まりません)。止まると、そのイベントを、履歴として開きます。");
        ImGui::Checkbox("確認された異常(赤)で止める", &bp.onViolation);
        ImGui::Checkbox("注意(黄)以上で止める", &bp.onWarning);

        // 対象の住民。
        int scope = (bp.residentId == 0) ? 0 : 1;
        ImGui::TextUnformatted("対象:");
        ImGui::SameLine();
        bool changed = ImGui::RadioButton("全住民", &scope, 0);
        ImGui::SameLine();
        const std::string thisLabel = (bp.residentId != 0) ? Fmt("住民 #%u だけ", bp.residentId) : (m_target != 0 ? Fmt("今の住民 #%u だけ", m_target) : std::string("今の住民だけ"));
        changed = ImGui::RadioButton(thisLabel.c_str(), &scope, 1) || changed;
        if (changed)
        {
            bp.residentId = (scope == 0) ? 0 : m_target;
        }

        // 種類で止める。
        ImGui::TextUnformatted("止めるイベントの種類:");
        constexpr int kKindCount = static_cast<int>(SimEventKind::MovedIn) + 1;
        for (int k = 0; k < kKindCount; ++k)
        {
            bool on = (bp.kindMask & (1u << k)) != 0;
            if (ImGui::Checkbox(EventKindJa(static_cast<SimEventKind>(k)), &on))
            {
                bp.kindMask = on ? (bp.kindMask | (1u << k)) : (bp.kindMask & ~(1u << k));
            }
            if ((k % 6) != 5 && k + 1 < kKindCount) { ImGui::SameLine(); }
        }
        static const char* const kResultLabels[] = { "結果を問わない", "成功のとき", "不成立(灰)のとき", "注意(黄)のとき", "異常(赤)のとき", "経過のとき" };
        static const uint32_t kResultMasks[] = { 0u, 1u << static_cast<uint32_t>(SimEventResult::Success), 1u << static_cast<uint32_t>(SimEventResult::NotMet),
            1u << static_cast<uint32_t>(SimEventResult::Warning), 1u << static_cast<uint32_t>(SimEventResult::Violation), 1u << static_cast<uint32_t>(SimEventResult::Info) };
        int resultIndex = 0;
        for (int r = 0; r < 6; ++r) { if (bp.resultMask == kResultMasks[r]) { resultIndex = r; } }
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::Combo("上の種類のうち", &resultIndex, kResultLabels, 6))
        {
            bp.resultMask = kResultMasks[resultIndex];
        }
        ImGui::TextDisabled("例: 種類=立ち寄り判定 + 成功のとき → 立ち寄りの抽選に当選した瞬間に止まる");
        if (!recorder.IsEnabled() && bp.AnyEnabled())
        {
            ImGui::TextColored(kColorWarning, "記録がOFFなので、条件は働きません");
        }
        else if (bp.AnyEnabled())
        {
            ImGui::TextColored(kColorSuccess, "条件が有効です。当てはまるイベントが記録されると、ゲームが止まります");
        }
    }
    ImGui::Separator();
}

void ResidentDebugPanel::DrawSnapshotView(const SimEvent& event, DemandSystem& demand, const std::vector<GameObject>& objects)
{
    const ResidentSimSettings& settings = demand.GetSettings();
    const SimResidentSnapshot& s = event.snapshot;
    ImGui::BeginChild("##snapshot", ImVec2(0.0f, 0.0f), false);

    Heading(Fmt("このイベント(#%llu)の時点で、記録されていた状態", static_cast<unsigned long long>(event.eventId)).c_str());
    ImGui::TextDisabled("今の値ではなく、イベントに残した写しです。写しに無い物(位置・経路・滞在の残り)は、記録していないので、出せません。");
    Row("いつ", Fmt("週%d %s  外出#%u  [%s]", event.week, HourText(event.hour).c_str(), event.outingId, EventKindJa(event.kind)));
    Row("状態", StateJa(s.state));
    Row("住宅", s.homeObjectId != 0 ? Fmt("#%u", s.homeObjectId) : std::string("なし(住居なし)"));
    if (s.targetFacilityId != 0)
    {
        Row("行き先", FacilityLabel(objects, s.targetFacilityId));
    }
    Row("立ち寄り", Fmt("%d / %d 回", s.outingStopCount, settings.maxStopovers));

    Heading("需要・体力(その時点)");
    Row("生活の需要", Fmt("%.1f%s", s.lifeNeed, s.lifeNeed < settings.needSatisfiedThreshold ? "  不足" : ""));
    Row("アクティビティの需要", Fmt("%.1f%s", s.activityNeed, s.activityNeed < settings.needSatisfiedThreshold ? "  不足" : ""));
    Row("体力", Fmt("%.1f / %.1f", s.stamina, s.staminaCap));
    Row("ペナルティ", Fmt("%d 段階", s.penaltyLevel));
    Row("満足度", Fmt("%.1f / %.0f", s.satisfaction, settings.satisfactionMax));

    Heading("このイベントの内容");
    ImGui::PushStyleColor(ImGuiCol_Text, ColorOf(event.result));
    ImGui::TextWrapped("%s", DescribeEvent(event, objects).c_str());
    ImGui::PopStyleColor();
    if (event.detailId != 0)
    {
        ImGui::TextDisabled("判断の内訳は、「判断の詳細」タブで見られます(カーソルの時点の判断に、合わせてあります)。");
    }

    const ResidentDebugView live = demand.GetResidentDebugView(m_target, objects);
    if (live.found)
    {
        Heading("この住民の特徴(生まれた時から、変わりません)");
        Row("品質重視度", Fmt("%.2f", live.traits.qualityWeight));
        Row("価格感度", Fmt("%.2f", live.traits.priceSensitivity));
        Row("距離許容度", Fmt("%.2f", live.traits.distanceTolerance));
        Row("自発性", Fmt("%.2f", live.traits.spontaneity));
    }
    ImGui::EndChild();
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

    // 選択中の判断を決める。ライブで、自分で選んでいない間は、いつも最新。履歴のカーソル・コンボ・[詳細]で選んだ時は、その判断
    // (コンボの一覧に無い、古い判断も、記録全体から探して見られる)。見つからなければ、最新。
    const SimEvent* selected = nullptr;
    if (!(m_live && !m_decisionManual) && m_selectedDetailId != 0)
    {
        for (const SimEvent* event : recorder.RecentEventsOf(m_target, 100000))
        {
            if (event->kind == SimEventKind::PurposeChoice && event->detailId == m_selectedDetailId) { selected = event; break; }
        }
    }
    if (!selected)
    {
        selected = decisions.front();
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
                m_decisionManual = true;
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

    ImGui::TextDisabled("色: 灰=普通の不成立(確率・条件)、黄=注意(設計上ありえる)、赤=確認された異常。意外な抽選結果は赤になりません。行をクリックすると、そのイベントを履歴として見られます。");

    // 時間軸(この住民の、絞り込み後のイベント。古い順)を、同じ外出ごとにまとめて出す。絞り込み・検索は、上のナビゲーションで行う。
    const SimTimelineEvents& events = m_timeline;
    if (events.empty())
    {
        ImGui::TextDisabled("この住民の記録は、まだありません(または、絞り込みで、全て外れています)。");
        return;
    }

    ImGui::BeginChild("##history", ImVec2(0.0f, 0.0f), false);
    // ライブで、一番下まで読んでいる時だけ、増えたイベントを追って、下へスクロールする。履歴を見ている間・上を読んでいる間は、動かさない。
    const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8.0f;
    const size_t total = events.size();
    size_t begin = 0;
    while (begin < total)
    {
        // 同じ外出の番号が続く範囲を、1つのグループにする。
        size_t end = begin;
        while (end < total && events[end]->outingId == events[begin]->outingId) { ++end; }

        bool anyWarning = false, anyViolation = false;
        int visits = 0;
        for (size_t i = begin; i < end; ++i)
        {
            anyWarning = anyWarning || events[i]->result == SimEventResult::Warning;
            anyViolation = anyViolation || events[i]->result == SimEventResult::Violation;
            visits += (events[i]->kind == SimEventKind::VisitFinished && events[i]->result == SimEventResult::Success) ? 1 : 0;
        }
        const SimEvent& oldest = *events[begin];
        const SimEvent& newest = *events[end - 1];
        const bool hasCursor = m_cursorIndex >= static_cast<int>(begin) && m_cursorIndex < static_cast<int>(end);
        std::string title = (oldest.outingId != 0)
            ? Fmt("外出 #%u  週%d %s〜  施設 %d 回利用  [%s]", oldest.outingId, oldest.week, HourText(oldest.hour).c_str(), visits, EventKindJa(newest.kind))
            : Fmt("外出に属さない記録  週%d %s〜", oldest.week, HourText(oldest.hour).c_str());
        title += Fmt("###outing%u_%llu", oldest.outingId, static_cast<unsigned long long>(oldest.eventId));

        // カーソルへ移った時は、そのグループを開く。ライブでは、最新のグループを、既定で開く。
        if (hasCursor && m_scrollToCursor)
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }
        else if (end == total)
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, anyViolation ? kColorViolation : (anyWarning ? kColorWarning : kColorNormal));
        const bool open = ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_None);
        ImGui::PopStyleColor();
        if (open)
        {
            for (size_t i = begin; i < end; ++i)
            {
                const SimEvent& e = *events[i];
                const bool isCursor = (static_cast<int>(i) == m_cursorIndex);
                ImGui::PushID(static_cast<int>(e.eventId));
                ImGui::PushStyleColor(ImGuiCol_Text, ColorOf(e.result));
                const std::string text = Fmt("%s  [%s] %s", HourText(e.hour).c_str(), EventKindJa(e.kind), DescribeEvent(e, objects).c_str());
                const float textWidth = ImGui::CalcTextSize(text.c_str()).x + 8.0f;
                // 履歴モードの時だけ、見ているイベントの行を強調する(ライブでは、最新の行が、いつも「カーソル」なので、強調しない)。
                if (ImGui::Selectable(text.c_str(), isCursor && !m_live, 0, ImVec2(textWidth, 0.0f)))
                {
                    SeekToIndex(static_cast<int>(i));
                }
                ImGui::PopStyleColor();
                if (isCursor && m_scrollToCursor)
                {
                    ImGui::SetScrollHereY(0.5f);
                }
                if (e.detailId != 0 && (e.kind == SimEventKind::PurposeChoice || e.kind == SimEventKind::FacilitySelect))
                {
                    ImGui::SameLine();
                    if (recorder.FindDetail(e.detailId))
                    {
                        if (ImGui::SmallButton("詳細"))
                        {
                            m_selectedDetailId = e.detailId;
                            m_selectedCandidate = -1;
                            m_decisionManual = true;
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
        begin = end;
    }
    if (m_live && wasAtBottom)
    {
        ImGui::SetScrollHereY(1.0f);
    }
    m_scrollToCursor = false;
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
            if (ImGui::SmallButton("このイベントを見る"))
            {
                SeekToEvent(it->residentId, it->eventId); // その住民へ切り替えて、履歴として開く
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
