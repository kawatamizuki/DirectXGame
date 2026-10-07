#include "Debug.h"
#include <Windows.h>
#include <deque>
#include <fstream>
#include "imgui.h"

namespace
{
    // 画面表示用の直近ログ(リングバッファ)。無限に溜まらないよう上限を設けて古いものから捨てる。
    constexpr size_t kMaxLogEntries = 300;
    std::deque<std::string> s_logEntries;
    bool s_autoScroll = true;

    // 直近に書き出したファイルの絶対パス。空なら「まだ書き出していない」。
    // 相対パスだと実行時のカレントディレクトリ次第で迷子になりやすいため、
    // パネルには絶対パスをそのまま表示し続ける(タイマーで自動的に消したりしない。
    // ボタンを押した瞬間を見逃しても、後からいつでもパスを確認できるようにするため)。
    std::string s_exportedFilePath;

    // 現在のログ全部(直近kMaxLogEntries件)をテキストファイルに書き出す。
    // 実行ファイルと同じフォルダ(カレントディレクトリ)に固定名で書き出すことで、
    // 毎回同じ場所を上書きし、ファイルが増え続けないようにする。
    void ExportLogToFile()
    {
        const char* relativePath = "debug_log_export.txt";

        std::ofstream file(relativePath, std::ios::out | std::ios::trunc);
        if (!file)
        {
            return;
        }
        for (const std::string& entry : s_logEntries)
        {
            file << entry << "\n";
        }
        file.close();

        char fullPath[MAX_PATH] = {};
        GetFullPathNameA(relativePath, MAX_PATH, fullPath, nullptr);
        s_exportedFilePath = fullPath;
    }

    void Output(const std::string& prefix, const std::string& message)
    {
#ifdef _DEBUG
        std::string text = prefix + message + "\n";
        OutputDebugStringA(text.c_str());

        s_logEntries.push_back(prefix + message);
        if (s_logEntries.size() > kMaxLogEntries)
        {
            s_logEntries.pop_front();
        }
#endif
    }
}

namespace Debug
{
    void Log(const std::string& message)
    {
        Output("[LOG] ", message);
    }

    void Info(const std::string& message)
    {
        Output("[INFO] ", message);
    }

    void Warning(const std::string& message)
    {
        Output("[WARN] ", message);
    }

    void Error(const std::string& message)
    {
#ifdef _DEBUG
        Output("[ERROR] ", message);
#endif
    }

    void Assert(bool condition, const std::string& message)
    {
#ifdef _DEBUG
        if (!condition)
        {
            Output("[ASSERT] ", message);
            __debugbreak();
        }
#endif
    }

    void DrawDebugUI()
    {
        // 他の常設パネル(Build/City Stats/Profiler)と重ならない右側に配置する
        ImGui::SetNextWindowPos(ImVec2(1230.0f, 8.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(290.0f, 400.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Log");

        ImGui::Checkbox("Auto-scroll", &s_autoScroll);
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            s_logEntries.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Export to file"))
        {
            ExportLogToFile();
        }

        if (!s_exportedFilePath.empty())
        {
            ImGui::TextWrapped("Saved: %s", s_exportedFilePath.c_str());
        }

        ImGui::Separator();

        ImGui::BeginChild("LogScrollRegion");
        for (const std::string& entry : s_logEntries)
        {
            ImGui::TextUnformatted(entry.c_str());
        }
        if (s_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::End();
    }
}