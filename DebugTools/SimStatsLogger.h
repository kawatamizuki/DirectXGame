#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include "SimDebugRecorder.h"
#include "SimStats.h"

class CommercialAreaManager;
class DemandSystem;
class GameClock;

// 放置して集める、行動の統計の出力(Editorの「統計の出力」)。
// ゲームを起動して、しばらく放っておくだけで、週が1つ終わるたびに、住民の行動の統計をファイルへ追記する。
// 後から、表計算ソフトや文章として読んで、「自然な動きか」「偏っていないか」を調べるための物。
//
//  ・出力先: Saves/stats/session_<日時>/(1回の記録=1つのフォルダ。Git管理外)
//      weekly.csv      週ごとに1行(住民の様子の平均・外出の理由の別・立ち寄り・利用・売上・エリア目的の外出・異常の数)
//      facilities.csv  週 × 施設(その週と通算の来訪・売上・所属する区画)
//      areas.csv       週 × 区画(目的にした人数・来訪者・目的の来訪者・延べ利用・売上・所属する施設の数)
//      summary.txt     毎週、書き直す全体の要約(通算の件数と率・不成立の理由・確率の判定の期待値と実際・住民ごとのばらつき・異常)
//      notes.txt       世界の読み込み・区切りなどの出来事
//  ・1週ぶんの値は、「通算の集計の、前回の区切りからの差」と、施設・区画の台帳の「先週」から作る(週の区切りは、DemandSystem の週末の処理)。
//  ・記録のON/OFF・出力は、乱数を引かない・住民の状態を変えない(シミュレーションの結果は変わらない)。
//  ・ENABLE_EDITOR のビルドだけが使う(統計・異常の記録が、Editorのビルドだけにあるため)。
class SimStatsLogger : public ISimRecordObserver
{
public:
    static constexpr const char* kOutputDirectory = "Saves/stats";

    void Initialize(DemandSystem* demand, CommercialAreaManager* areas, GameClock* clock);

    // 開始・終了。開始すると、今の通算を基準にして、次の週の区切りから書き始める。
    bool Start();
    void Stop();
    bool IsActive() const { return m_active; }

    // 毎フレーム。canRun: 今、書いてよいか(ゲームシーンで、耐久試験などが世界を差し替えていない)。
    // 自動開始がONで、まだ始めていなければ、ここで始める。
    void Update(bool canRun);

    // 設定(Editorの設定ファイルに保存する物は、Game 側がスイッチとして登録する)。
    bool autoStart = false;             // ゲームシーンに入ったら、自動で記録を始める


    // ---- 読み取り(パネル用) ----
    const std::string& GetFolder() const { return m_folder; }
    int GetRowsWritten() const { return m_rows; }
    int GetLastWeek() const { return m_lastWeekLogged; }
    double GetWallSeconds() const;
    uint64_t GetAnomalies() const { return m_anomalyTotal; }
    const std::string& GetStatusText() const { return m_message; }

    // ---- 記録の観察者 ----
    void OnEvent(const SimEvent& event) override;
    void OnDetail(const SimDecisionDetail& /*detail*/) override {}
    void OnOutingRoll(int /*week*/, OutingChanceBranch /*branch*/, float /*chance*/, bool /*naturalHit*/) override {}
    void OnWorldReset() override;

private:
    void WriteWeekRows();
    void WriteSummary();
    void Note(const std::string& text);
    bool OpenFolder();

    DemandSystem* m_demand = nullptr;
    CommercialAreaManager* m_areas = nullptr;
    GameClock* m_clock = nullptr;

    bool m_active = false;
    bool m_worldReset = false;          // 世界が差し替わった(通算の基準を取り直す)
    std::string m_folder;
    std::string m_message;
    int m_rows = 0;
    int m_lastWeekLogged = 0;
    int m_weeksEndedSeen = 0;           // 最後に見た、週末の回数
    bool m_autoTried = false;           // 自動開始を試した(手動で止めた後に、勝手に再開しないため)
    std::chrono::steady_clock::time_point m_startTime{};
    SimStatBucket m_prev;               // 前回の行の時点の、通算(今の週の差を出す基準)
    uint64_t m_anomalyTotal = 0;
    std::array<uint64_t, 32> m_anomalyByKind{};
};
