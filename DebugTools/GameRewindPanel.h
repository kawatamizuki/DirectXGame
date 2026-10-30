#pragma once
#include <cstdint>

class GameRewind;
class SimReproController;

// Editorの「タイムライン」パネル(巻き戻し・進めるシークバー)。
// ON/OFF・シークバー・「ここから再開」「最新へ」・整合性の検査・使用量・設定を出す。データは GameRewind から読むだけ。ENABLE_EDITOR のビルドだけが呼ぶ。
class GameRewindPanel
{
public:
    void SetOpenFlag(bool* open) { m_open = open; }
    void Draw(GameRewind& timeline, const SimReproController& repro);

private:
    bool* m_open = nullptr;
    int m_seekValue = 0;            // シークバーの値(ドラッグ中の位置)
    bool m_dragging = false;
    double m_lastSeekRequestTime = 0.0;
};
