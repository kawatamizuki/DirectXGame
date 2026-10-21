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

    // trueの間、LogとInfoを記録しない(Warning・Error・Assertは今まで通り記録する)。
    // サムネイルの一括生成のように、大量のモデルを読み込む処理で、ログパネルが「読み込んだ」の行で埋まるのを防ぐため。
    void SetInfoSuppressed(bool suppressed);

    // スコープの間だけLog/Infoを止め、スコープを抜けたら元の状態に戻すヘルパー。
    class ScopedInfoSuppress
    {
    public:
        ScopedInfoSuppress();
        ~ScopedInfoSuppress();
        ScopedInfoSuppress(const ScopedInfoSuppress&) = delete;
        ScopedInfoSuppress& operator=(const ScopedInfoSuppress&) = delete;
    private:
        bool m_previous;
    };

    // Log/Info/Warning/Errorの直近の履歴をImGuiパネルに表示する。
    // ファイルへの一時的な書き出しを毎回行わなくても、実行中にリアルタイムで確認できるようにするためのもの。
    // Game::Draw()から他のデバッグパネル(Profiler等)と同じように毎フレーム呼ぶ想定。
    // openがnullptrでなければ、タイトルバーの×で閉じられ、*openがfalseの間は描かない。
    void DrawDebugUI(bool* open = nullptr);
}