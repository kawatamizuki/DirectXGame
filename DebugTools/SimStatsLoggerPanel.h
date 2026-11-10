#pragma once

class SimStatsLogger;

// Editorの「統計の出力」パネル。放置して集める、行動の統計の出力(SimStatsLogger)の、開始・停止・状況の表示。ENABLE_EDITOR のビルドだけが呼ぶ。
class SimStatsLoggerPanel
{
public:
    void SetOpenFlag(bool* open) { m_open = open; }
    // canRun: 今、記録できる状況か(ゲームシーンで、耐久試験が動いていない)。
    void Draw(SimStatsLogger& logger, bool canRun);

private:
    bool* m_open = nullptr;
};
