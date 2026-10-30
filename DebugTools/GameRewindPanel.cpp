#include "GameRewindPanel.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include "imgui.h"
#include "SimReproController.h"
#include "GameRewind.h"

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

    // ステップ数を、「分:秒」(実時間。1ステップ=1/60秒)にする。
    std::string Clock(uint64_t steps)
    {
        const uint64_t seconds = steps / 60;
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%llu:%02llu", static_cast<unsigned long long>(seconds / 60), static_cast<unsigned long long>(seconds % 60));
        return buffer;
    }

    const char* StateJa(GameRewindState state)
    {
        switch (state)
        {
        case GameRewindState::Live:    return "記録中(ふだんどおり進んでいます)";
        case GameRewindState::Seeking: return "進め直し中...";
        case GameRewindState::Past:    return "過去で止まっています";
        default:                        return "OFF";
        }
    }
}

void GameRewindPanel::Draw(GameRewind& timeline, const SimReproController& repro)
{
    if (m_open && !*m_open)
    {
        return;
    }
    if (!ImGui::Begin("タイムライン", m_open))
    {
        ImGui::End();
        return;
    }

    Note("プレイ中のゲーム内時間を、シークバーで巻き戻し・進められます。ONの間、ゲームは固定の刻み(1/60秒)で進み、一定ごとに街の保存データ(キーフレーム)を、メモリに撮ります。"
         "シークは、目的の直前のキーフレームを読み込み、記録した指示・倍率を入れて、進め直します(画面は描かず、分けて進めます)。巻き戻した後は時間が止まり、「ここから再開」で、そこから新しい未来が始まります。");

    // ---- ON/OFF ----
    bool enabled = timeline.IsEnabled();
    const bool reproBusy = repro.GetState() != SimReproState::Idle;
    ImGui::BeginDisabled(reproBusy && !enabled);
    if (ImGui::Checkbox("タイムラインを記録する(ON)", &enabled))
    {
        timeline.SetEnabled(enabled);
    }
    ImGui::EndDisabled();
    if (reproBusy && !enabled)
    {
        ImGui::TextColored(kColorWarning, "「乱数・再現」の記録・再生が動いている間は、ONにできません(どちらも、時間の進め方を決めるため)。");
    }

    const GameRewindState state = timeline.GetState();
    if (!enabled)
    {
        ImGui::TextDisabled("OFFの間は、ふだんどおり(実時間のフレーム間隔で進みます)。ONにすると、その時点から記録を始めます。");
        ImGui::End();
        return;
    }

    ImGui::Text("状態: %s", StateJa(state));
    if (!timeline.GetMessage().empty())
    {
        ImGui::TextWrapped("%s", timeline.GetMessage().c_str());
    }

    // ---- シークバー ----
    Heading("1. シークバー");
    if (timeline.GetKeyframeCount() == 0)
    {
        ImGui::TextDisabled("(ゲームシーンのPlayingになると、記録が始まります)");
    }
    else
    {
        const uint64_t oldest = timeline.GetOldestStep();
        const uint64_t latest = timeline.GetLatestStep();
        const uint64_t shown = (state == GameRewindState::Seeking) ? timeline.GetSeekTarget() : timeline.GetCurrentStep();
        if (!m_dragging)
        {
            m_seekValue = static_cast<int>(shown);
        }
        m_seekValue = (std::max)(static_cast<int>(oldest), (std::min)(m_seekValue, static_cast<int>(latest)));
        ImGui::SetNextItemWidth(-1.0f);
        const bool changed = ImGui::SliderInt("##timelineseek", &m_seekValue, static_cast<int>(oldest), static_cast<int>(latest), "");
        const bool active = ImGui::IsItemActive();
        const double now = ImGui::GetTime();
        if (active)
        {
            m_dragging = true;
        }
        // ドラッグの間は、少し間を置いて、追いかけて進め直す。離したら、その位置へ。
        if (changed && (now - m_lastSeekRequestTime) > 0.25)
        {
            timeline.RequestSeek(static_cast<uint64_t>(m_seekValue));
            m_lastSeekRequestTime = now;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            timeline.RequestSeek(static_cast<uint64_t>(m_seekValue));
            m_lastSeekRequestTime = now;
        }
        if (!active)
        {
            m_dragging = false;
        }
        ImGui::Text("今: %s   範囲: %s 〜 %s(実時間。最新の %s 前まで遡れます)", Clock(shown).c_str(), Clock(oldest).c_str(), Clock(latest).c_str(), Clock(latest - oldest).c_str());
        if (state == GameRewindState::Seeking)
        {
            const uint64_t current = timeline.GetCurrentStep();
            ImGui::TextColored(kColorWarning, "進め直し中: ステップ %llu / %llu", static_cast<unsigned long long>(current), static_cast<unsigned long long>(timeline.GetSeekTarget()));
        }
        ImGui::BeginDisabled(state != GameRewindState::Past);
        if (ImGui::Button("ここから再開(それより後の記録は捨てる)"))
        {
            timeline.RequestResumeFromHere();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(state == GameRewindState::Live);
        if (ImGui::Button("最新へ戻る(進め直して、続きを始める)"))
        {
            timeline.RequestGoLatest();
        }
        ImGui::EndDisabled();
        if (timeline.LatestMismatched())
        {
            ImGui::TextColored(kColorBad, "最新へ戻った時、記録した時と、乱数の状態・時刻が一致しませんでした(巻き戻しの結果が、元と違う可能性があります)。");
        }
    }

    // ---- 整合性の検査 ----
    Heading("2. 整合性の検査");
    ImGui::BeginDisabled(timeline.GetKeyframeCount() < 2 || timeline.GetVerifyResult().running);
    if (ImGui::Button("各キーフレームから進め直して、一致するか調べる"))
    {
        timeline.RequestVerify();
    }
    ImGui::EndDisabled();
    const GameRewindVerifyResult& verify = timeline.GetVerifyResult();
    if (verify.running)
    {
        ImGui::TextColored(kColorWarning, "検査中: %d / %d 区間", verify.checked, verify.total);
    }
    else if (verify.finished)
    {
        if (verify.mismatched == 0)
        {
            ImGui::TextColored(kColorGood, "%d 区間すべて一致しました(保存と進め直しで、乱数の状態・時刻が同じ)。", verify.checked);
        }
        else
        {
            ImGui::TextColored(kColorBad, "%d 区間のうち %d 区間が一致しませんでした(最初は、ステップ %llu まで)。保存していない状態が、結果に効いている可能性があります。",
                verify.checked, verify.mismatched, static_cast<unsigned long long>(verify.firstMismatchStep));
        }
    }
    Note("各キーフレームを読み込み、次のキーフレームまで進め直して、乱数の状態・ゲーム内の時刻が、記録した時と同じになるかを調べます。同じなら、巻き戻しが、元の流れを再現できています。");

    // ---- 使用量 ----
    Heading("3. 使用量");
    const double usedMB = static_cast<double>(timeline.GetBytesUsed()) / (1024.0 * 1024.0);
    ImGui::Text("キーフレーム: %zu 個   メモリ: %.2f MB / 上限 %d MB(仮)", timeline.GetKeyframeCount(), usedMB, timeline.Settings().memoryCapMB);
    ImGui::ProgressBar(static_cast<float>((std::min)(1.0, usedMB / static_cast<double>((std::max)(1, timeline.Settings().memoryCapMB)))), ImVec2(-1.0f, 0.0f));
    ImGui::Text("指示 %zu 件   倍率の変更 %zu 件   これまでに撮ったキーフレーム %llu 個", timeline.GetCommandEntryCount(), timeline.GetScaleEntryCount(), static_cast<unsigned long long>(timeline.GetKeyframesTaken()));
    ImGui::Text("直近のキーフレームを撮るのにかかった時間: %.2f ms   直近の進め直し: %.1f ms", timeline.GetLastKeyframeMilliseconds(), timeline.GetLastSeekMilliseconds());

    // ---- 設定 ----
    Heading("4. 設定(仮の既定値。使いながら、調整します)");
    GameRewindSettings& settings = timeline.Settings();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("遡れる長さ(実時間の分)", &settings.windowMinutes, 1, 60);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("メモリの上限(MB)", &settings.memoryCapMB, 16, 4096);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("キーフレームの間隔(ステップ。60=1秒)", &settings.keyframeInterval, 60, 3600);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("進め直しの持ち時間(ミリ秒/フレーム)", &settings.stepBudgetMilliseconds, 1, 50);
    Note("上限を超えたら、古いキーフレームから捨てます(遡れる長さが短くなります)。間隔を短くすると、シークは速くなりますが、メモリを多く使います。街が大きくなると、キーフレームも大きくなるので、使用量を見ながら調整してください。");

    // ---- 限界 ----
    Heading("5. 注意");
    Note("・建物・道路・入居の変更は、その直後にキーフレームを撮ります(巻き戻しても、その変更は残ります)。Stop(Editing)の間の操作は記録せず、Playingに戻った時に、撮り直します。"
         "・巻き戻した時点の、住民デバッグの記録・行動統計は、その手前のキーフレームから数え直しになります。音・カメラ・画面の状態は、戻りません。"
         "・保存していない状態が結果に効くと、進め直した結果が元と少しずれます(「整合性の検査」で、確かめられます)。"
         "・「乱数・再現」の記録・再生とは、同時に使えません。");

    ImGui::End();
}
