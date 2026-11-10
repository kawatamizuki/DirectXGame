#include "SimStatsLoggerPanel.h"
#include <cstdio>
#include <filesystem>
#include <string>
#include "imgui.h"
#include "SimStatsLogger.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

void SimStatsLoggerPanel::Draw(SimStatsLogger& logger, bool canRun)
{
    if (m_open && !*m_open)
    {
        return;
    }
    if (!ImGui::Begin("統計の出力", m_open))
    {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("ゲームを起動して放置するだけで、週が1つ終わるたびに、住民の行動の統計をファイルへ追記します。"
                       "後から、表計算ソフトや文章で読んで、「自然な動きか」「偏っていないか」を調べられます。"
                       "出力は、乱数を引かず、住民の状態を変えません(結果は変わりません)。");

    ImGui::Spacing();
    if (logger.IsActive())
    {
        ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.50f, 1.0f), "記録中");
        ImGui::Text("書いた週: %d 行(最後の週末の回数 %d) / 経過 %.1f 分 / 異常 %llu 件", logger.GetRowsWritten(), logger.GetLastWeek(),
                    logger.GetWallSeconds() / 60.0, static_cast<unsigned long long>(logger.GetAnomalies()));
        if (!canRun)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.30f, 1.0f), "今は、記録を止めています(タイトル画面、または耐久試験の実行中)。");
        }
        if (ImGui::Button("記録を終了する"))
        {
            logger.Stop();
        }
    }
    else
    {
        if (ImGui::Button("記録を始める"))
        {
            logger.Start();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("今の通算を基準にして、次の週の区切りから書き始めます");
    }
    ImGui::Checkbox("ゲームを始めたら、自動で記録を始める(次の起動から。Editorのスイッチと同じ)", &logger.autoStart);

    if (!logger.GetFolder().empty())
    {
        ImGui::Spacing();
        ImGui::Text("出力先: %s", logger.GetFolder().c_str());
        if (ImGui::Button("フォルダを開く"))
        {
            const std::filesystem::path path = std::filesystem::absolute(logger.GetFolder());
            ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
    if (!logger.GetStatusText().empty())
    {
        ImGui::TextDisabled("%s", logger.GetStatusText().c_str());
    }

    if (ImGui::CollapsingHeader("出力するファイル"))
    {
        ImGui::BulletText("weekly.csv … 週ごとに1行。住民の様子の平均・外出の理由の別・立ち寄り・利用・売上・エリア目的の外出・区画の中と外の利用・異常の数");
        ImGui::BulletText("facilities.csv … 週 × 施設。その週と通算の来訪・売上・所属する区画");
        ImGui::BulletText("areas.csv … 週 × 区画。目的にした人数・来訪者・目的の来訪者・向かったが使わなかった人・延べ利用・売上");
        ImGui::BulletText("summary.txt … 毎週、書き直す要約。通算の件数と率・不成立の理由・確率の判定の期待と実際・住民ごとのばらつき・異常");
        ImGui::BulletText("notes.txt … 世界の読み込み(通算がリセットされる)などの出来事");
        ImGui::TextDisabled("1週ぶんの値は、通算の集計の前回の区切りからの差と、施設・区画の台帳の「先週」から作ります。時計の倍率を上げると、速く溜まります。");
    }
    ImGui::End();
}
