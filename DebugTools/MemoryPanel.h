#pragma once
#include <deque>
#include <functional>
#include "MemoryReport.h"

// Editorの「メモリ」パネル。このゲームのメモリ使用量と、その内訳(ゲーム本体 / デバッグ・検証の機能)を、1か所で見る。
//  ・プロセス全体(ワーキングセット・プライベート)と、増え方(直近のグラフ・1分あたり)を出す。
//  ・内訳は「要素数 × 1件の大きさ」の概算。上限がある項目は、使用率と、どこで調整するかを出す。上限に近いと、色が変わり、警告の一覧に出る。
//  ・データは、provider(Game が作る MemoryReport)から、約1秒ごとに取る(毎フレームは、集めない)。ENABLE_EDITOR のビルドだけが呼ぶ。
class MemoryPanel
{
public:
    void SetOpenFlag(bool* open) { m_open = open; }
    void Draw(const std::function<MemoryReport()>& provider);

private:
    struct Sample
    {
        float workingSetMB = 0.0f;
        float gameMB = 0.0f;
        float debugMB = 0.0f;
    };

    bool* m_open = nullptr;
    MemoryReport m_report;
    double m_lastSampleTime = -100.0;
    double m_workingSetMB = 0.0;
    double m_privateMB = 0.0;
    double m_peakMB = 0.0;
    std::deque<Sample> m_history;           // 約1秒ごと。最大 kHistory 件(約10分)
    int m_sortMode = 0;                     // 0=グループ順 1=大きい順
    static constexpr size_t kHistory = 600;
};
