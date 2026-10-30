#include "SimReproPanel.h"
#include <cstdio>
#include <vector>
#include "imgui.h"
#include "DemandSystem.h"
#include "SimReproController.h"

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

    // イベントの種類の名前(SimEventKind の並び。255は、外出判定の抽選)。
    const char* KindName(uint8_t kind)
    {
        static const char* const kNames[] = { "外出判定に当選", "目的選択", "施設選択", "外出できず", "歩き始め", "到着", "利用完了", "立ち寄り判定", "帰宅開始", "迂回", "帰宅完了",
                                              "中断", "立ち往生", "立ち往生から復帰", "住居なし", "住宅の解体", "異常", "入居", "テスト指示" };
        if (kind == 255) { return "外出判定の抽選"; }
        return kind < sizeof(kNames) / sizeof(kNames[0]) ? kNames[kind] : "?";
    }

    void DrawSummaryColumn(const char* label, const SimReproRunSummary& s)
    {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(label);
        if (!s.valid)
        {
            ImGui::TextDisabled("(まだ)");
            return;
        }
        ImGui::Text("フレーム %llu", static_cast<unsigned long long>(s.frames));
        ImGui::Text("イベント %llu / 抽選 %llu", static_cast<unsigned long long>(s.digest.events), static_cast<unsigned long long>(s.digest.rolls));
        ImGui::Text("指紋 %016llX", static_cast<unsigned long long>(s.digest.hash));
        ImGui::Text("週 %d  %02d:%02d", s.week, static_cast<int>(s.hour), static_cast<int>((s.hour - static_cast<int>(s.hour)) * 60.0f));
        ImGui::Text("乱数の種 %08X", s.seed);
        ImGui::Text("乱数の状態 %08X", s.randomState);
    }
}

void SimReproPanel::Draw(DemandSystem& demand, SimReproController& repro, bool timelineEnabled)
{
    if (m_open && !*m_open)
    {
        return;
    }
    if (!ImGui::Begin("乱数・再現", m_open))
    {
        ImGui::End();
        return;
    }

    // ---- 乱数の取り方 ----
    Heading("1. 乱数の取り方(新しい世界を始める時: ゲームの開始・ロード・リセット)");
    int mode = static_cast<int>(demand.GetRandomMode());
    ImGui::RadioButton("通常: ロードのたびに、新しい種(同じセーブでも、その後の抽選結果が変わる)", &mode, static_cast<int>(SimRandomMode::Reseed));
    ImGui::RadioButton("固定シード: 設定の種で始める(同じ種・同じ配置なら、同じ選択になる)", &mode, static_cast<int>(SimRandomMode::FixedSeed));
    ImGui::RadioButton("状態を復元: セーブに残した乱数の状態から、同じ列を続ける(再現テスト用)", &mode, static_cast<int>(SimRandomMode::RestoreState));
    demand.SetRandomMode(static_cast<SimRandomMode>(mode));
    int seed = static_cast<int>(demand.GetSettings().seed);
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputInt("固定シードの値", &seed))
    {
        demand.GetSettings().seed = static_cast<uint32_t>(seed);
    }
    ImGui::Text("今の世界の乱数の種: %08X   今の内部状態: %08X", demand.GetActiveSeed(), demand.GetRandomState());
    if (ImGui::SmallButton("今の種を、固定シードにする"))
    {
        demand.GetSettings().seed = demand.GetActiveSeed();
    }
    Note("通常のプレイでは「通常」のまま(既定)。ロードのたびに抽選結果が変わり、「毎回同じ失敗」になりません。擬似乱数なので、種が同じなら同じ列になります(真の乱数ではありません)。"
         "モードは、次に世界を始める時から効きます。セーブには、種と内部状態を残します(通常のプレイでは、読み込み時に使いません)。");

    // ---- 再現セッション ----
    Heading("2. 再現セッション(記録 → 再生 → 結果の比較)");
    Note("「同じスナップショット・同じ乱数の状態・同じ指示・同じフレーム数」で、同じイベントの列(指紋)になるかを確かめます。"
         "記録中・再生中は、実時間ではなく、固定の刻み(1/60秒)で進みます(PCの速さに、結果が依存しないため)。ステップ数(フレーム数)が違うと、結果はずれます。"
         "記録中も、メニュー・住民の情報パネルを開く、時間の倍率を変える、カメラを動かす、行動テストの指示を出す、は、ふだんどおり使えます(時間が止まった・動いた様子も記録され、再生で再現されます)。"
         "建物・道路の変更、ロード、Stop(Editing)への切り替えをすると、中止します(記録していない操作が入ると、再現できないため)。再生中も、メニューを開くと、ふだんと同じように時間が止まります(再生も、その間は止まり、閉じると続きから進みます)。再生が終わると、最終フレームで止まり、「続きを実行」を押すと、再生を始める前の速度で、続きを遊べます。");

    const SimReproState state = repro.GetState();
    if (state == SimReproState::Idle)
    {
        ImGui::BeginDisabled(timelineEnabled);
        if (ImGui::Button("記録を始める(スナップショットを保存して、読み込み直す)"))
        {
            repro.RequestBeginRecording();
        }
        ImGui::EndDisabled();
        if (timelineEnabled)
        {
            ImGui::TextColored(kColorWarning, "タイムラインがONの間は、記録を始められません(「タイムライン」のパネルで、OFFにしてください)。");
        }
        ImGui::BeginDisabled(!repro.GetRecorded().valid);
        ImGui::RadioButton("乱数の状態を復元して再生", &m_replayRandom, 0);
        ImGui::SameLine();
        ImGui::RadioButton("再抽選して再生(結果が変わるはず)", &m_replayRandom, 1);
        if (ImGui::Button("スナップショットへ戻して、同じ条件で再生"))
        {
            repro.RequestReplay(m_replayRandom == 0 ? SimReplayRandom::SameState : SimReplayRandom::Reseed);
        }
        ImGui::EndDisabled();
    }
    else if (state == SimReproState::Recording)
    {
        ImGui::TextColored(kColorWarning, "記録中: %llu フレーム   指示 %zu 件   倍率の変更 %zu 件",
            static_cast<unsigned long long>(repro.GetFrame()), repro.GetCommands().size(), repro.GetScaleChanges().size());
        ImGui::Text("行動テストの指示と、時間の倍率の変更が、フレーム番号つきで記録されます。");
        if (ImGui::Button("記録を止める"))
        {
            repro.RequestStopRecording();
        }
        ImGui::SameLine();
        if (ImGui::Button("中止"))
        {
            repro.RequestCancel();
        }
    }
    else if (state == SimReproState::Held)
    {
        ImGui::TextColored(kColorGood, "再生が終わりました。最終フレーム(%llu フレーム目)で止まっています。", static_cast<unsigned long long>(repro.GetFrame()));
        ImGui::TextDisabled("街の状態・住民デバッグ・行動統計を、この時点のまま見られます。時間は、進みません。");
        if (ImGui::Button("続きを実行する(再生を始める前の速度で、ふだんのプレイに戻る)"))
        {
            repro.RequestResume();
        }
    }
    else
    {
        const float progress = repro.GetEndFrame() > 0 ? static_cast<float>(repro.GetFrame()) / static_cast<float>(repro.GetEndFrame()) : 1.0f;
        ImGui::TextColored(kColorWarning, "再生中: %llu / %llu フレーム", static_cast<unsigned long long>(repro.GetFrame()), static_cast<unsigned long long>(repro.GetEndFrame()));
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
        if (ImGui::Button("中止"))
        {
            repro.RequestCancel();
        }
    }
    if (!repro.GetMessage().empty())
    {
        ImGui::TextWrapped("%s", repro.GetMessage().c_str());
    }

    // ---- 記録した区間の保存・読み込み ----
    if (state == SimReproState::Idle || state == SimReproState::Held)
    {
        ImGui::Spacing();
        ImGui::TextColored(kColorHeading, "記録した区間の保存・読み込み(Saves/repro/)");
        ImGui::BeginDisabled(!repro.GetRecorded().valid);
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("名前", m_segmentName, sizeof(m_segmentName));
        ImGui::SameLine();
        if (ImGui::Button("今の記録を、区間として保存"))
        {
            repro.SaveSegment(m_segmentName, m_segmentMessage);
        }
        ImGui::EndDisabled();
        const std::vector<std::string> names = repro.ListSegments();
        if (m_segmentSelect >= static_cast<int>(names.size()))
        {
            m_segmentSelect = -1;
        }
        ImGui::SetNextItemWidth(260.0f);
        const char* preview = m_segmentSelect >= 0 ? names[static_cast<size_t>(m_segmentSelect)].c_str() : "保存した区間を選ぶ...";
        if (ImGui::BeginCombo("保存した区間", preview))
        {
            for (size_t i = 0; i < names.size(); ++i)
            {
                if (ImGui::Selectable(names[i].c_str(), static_cast<int>(i) == m_segmentSelect))
                {
                    m_segmentSelect = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_segmentSelect < 0);
        if (ImGui::Button("読み込む") && m_segmentSelect >= 0)
        {
            repro.LoadSegment(names[static_cast<size_t>(m_segmentSelect)], m_segmentMessage);
        }
        ImGui::EndDisabled();
        if (!m_segmentMessage.empty())
        {
            ImGui::TextWrapped("%s", m_segmentMessage.c_str());
        }
        Note("区間 = スナップショット(街の保存) + 行動テストの指示・時間の倍率の記録 + 記録した結果(指紋)。読み込むと、「同じ条件で再生」で、いつでも再生・比較できます(別の日・別の起動でも)。"
             "ファイルから読み込んだ記録は、最初にずれたイベントの位置は出せません(途中経過は、保存しないため)。");
    }

    // ---- 結果 ----
    Heading("3. 結果の比較");
    if (ImGui::BeginTable("##reprosummary", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextRow();
        DrawSummaryColumn("記録した実行", repro.GetRecorded());
        DrawSummaryColumn(repro.GetLastReplayRandom() == SimReplayRandom::SameState ? "再生した実行(乱数の状態を復元)" : "再生した実行(再抽選)", repro.GetReplayed());
        ImGui::EndTable();
    }
    if (repro.GetRecorded().valid && repro.GetReplayed().valid)
    {
        if (repro.Matches())
        {
            ImGui::TextColored(kColorGood, "一致: 同じ条件で、同じイベントの列になりました(再現できました)。");
        }
        else if (repro.GetLastReplayRandom() == SimReplayRandom::Reseed)
        {
            ImGui::TextColored(kColorWarning, "不一致(再抽選なので、期待どおり): 乱数の種が違うと、結果が変わります。");
        }
        else
        {
            ImGui::TextColored(kColorBad, "不一致: 同じ条件のはずなのに、結果が違います。保存していない状態が、結果に効いている可能性があります。");
        }
    }
    // 食い違った時は、最初にずれたイベントを出す(原因を探す手がかり)。
    if (repro.GetRecorded().valid && repro.GetReplayed().valid && !repro.Matches())
    {
        const int index = repro.RecordedHasTrace() ? repro.FirstDivergence() : -1;
        const auto& recorded = repro.GetRecorded().trace;
        const auto& replayed = repro.GetReplayed().trace;
        if (index >= 0)
        {
            ImGui::Text("最初にずれたのは、%d 番目(0から数える)のイベント・抽選:", index);
            if (static_cast<size_t>(index) < recorded.size())
            {
                const SimDigestTraceEntry& e = recorded[static_cast<size_t>(index)];
                ImGui::Text("  記録: %s  住民 #%u  週%d %.2f時  a=%.4f", KindName(e.kind), e.residentId, e.week, e.hour, e.a);
            }
            else { ImGui::TextDisabled("  記録: (ここで終わっている)"); }
            if (static_cast<size_t>(index) < replayed.size())
            {
                const SimDigestTraceEntry& e = replayed[static_cast<size_t>(index)];
                ImGui::Text("  再生: %s  住民 #%u  週%d %.2f時  a=%.4f", KindName(e.kind), e.residentId, e.week, e.hour, e.a);
            }
            else { ImGui::TextDisabled("  再生: (ここで終わっている)"); }
        }
    }
    Note("指紋は、記録されたイベントと外出判定の抽選を、順番どおりに混ぜた値です(記録の容量で古いイベントを捨てても、含まれます)。"
         "一致すれば、イベントの列が同じ(場所・時刻・数値まで)と言えます。ゲームの全ての状態の一致までは、保証しません。");

    ImGui::End();
}
