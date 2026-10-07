#pragma once

#include <string>

namespace Debug
{
    //エラーが発生したことを表示する。エラーの種類で使い分ける
    void Log(const std::string& message);
    void Info(const std::string& message);
    void Warning(const std::string& message);
    void Error(const std::string& message);
    void Assert(bool condition, const std::string& message);

    // Log/Info/Warning/Errorの直近の履歴をImGuiパネルに表示する。
    // ファイルへの一時的な書き出しを毎回行わなくても、実行中にリアルタイムで確認できるようにするためのもの。
    // Game::Draw()から他のデバッグパネル(Profiler等)と同じように毎フレーム呼ぶ想定。
    void DrawDebugUI();
}