#pragma once
#include "ISaveable.h"

// ゲーム内の時刻(1日の中の時間帯)と、週・月・年を管理する。
// 実時間のdeltaTime/FPSを扱うTimeManagerとは別物で、こちらは「街の中の時間」。
// 見た目(昼夜の光)・住民の1日の行動など、時刻に関わるものは全てこの時計を唯一の
// 時刻の源泉として参照する(時計を用途ごとに複数持つと、停止・倍速・時刻ジャンプでズレるため)。
//
// 週: 1週間 = 朝(kWakeHour)から夜(kSleepHour)までの「活動時間」+ 就寝。
//   活動時間は実時間で約120秒(kActiveWeekSeconds)、就寝は短く(kSleepSeconds)進める。
//   就寝に入る瞬間が「週末」(週末の処理: 需要の自然減少・ペナルティの判定・週の集計)、
//   就寝が明ける瞬間が「週の始まり」。1か月 = 4週間、1年 = 12か月。
//   週末・週の始まりは、イベントとして数えておき、使う側がConsume〜で受け取る
//   (Editingなどで更新されない間に起きた分も、取りこぼさない)。
// 「その時刻に何をするか」(外出の締め切りなど)は時計には埋め込まず、使う側のデータに持たせる。
class GameClock : public ISaveable
{
public:
    // 1週間の区切り。朝の起床時刻と、就寝に入る時刻(ゲーム内の時刻)。
    static constexpr float kWakeHour = 6.0f;
    static constexpr float kSleepHour = 22.0f;

    // 活動時間(起床〜就寝)が実時間で何秒か。就寝(就寝〜起床)が実時間で何秒か。
    // ゲームの中身が固まったら、ここの2か所を変えれば、1週間の長さが変わる。
    static constexpr float kActiveWeekSeconds = 120.0f;
    static constexpr float kSleepSeconds = 10.0f;

    // ゲーム内の1時間が実時間で何秒か(活動時間・就寝それぞれ)。
    static constexpr float kDefaultSecondsPerGameHour = kActiveWeekSeconds / (kSleepHour - kWakeHour);          // 7.5秒
    static constexpr float kDefaultSleepSecondsPerGameHour = kSleepSeconds / (24.0f - (kSleepHour - kWakeHour)); // 1.25秒

    // 1か月の週の数、1年の月の数。
    static constexpr int kWeeksPerMonth = 4;
    static constexpr int kMonthsPerYear = 12;

    // 起動直後の時刻(朝8時から始める。第1週の途中)。
    static constexpr float kStartHour = 8.0f;

    // 実時間のdeltaTimeを渡して時刻を進める。倍率(0=停止)をかけた分だけ進み、
    // 就寝に入る・起床する・24時を超える、という区切りを、その瞬間に処理する(1フレームに複数またいでも、取りこぼさない)。
    void Update(float realDeltaSeconds);

    // 0.0以上24.0未満の、1日の中の時刻(時間単位)。
    float GetTimeOfDayHours() const { return m_timeOfDayHours; }
    int GetHour() const;
    int GetMinute() const;

    // 1から数える日数(暦の日。24時で増える)。
    int GetDayCount() const { return m_dayCount; }

    // ---- 週・月・年 ----
    // 1から数える、通算の週。起床するたびに1つ進む(起動直後が第1週)。
    int GetWeekCount() const { return m_weekCount; }
    int GetWeekOfMonth() const { return (m_weekCount - 1) % kWeeksPerMonth + 1; }                        // 1〜4
    int GetMonth() const { return ((m_weekCount - 1) / kWeeksPerMonth) % kMonthsPerYear + 1; }            // 1〜12
    int GetYear() const { return (m_weekCount - 1) / (kWeeksPerMonth * kMonthsPerYear) + 1; }             // 1〜
    // 今が、就寝中(週末〜週の始まりの間)か。
    bool IsSleeping() const;
    // 今の週が、その月の最後の週か(週末で、月の集計を区切るかの判定に使う)。
    bool IsLastWeekOfMonth() const { return GetWeekOfMonth() == kWeeksPerMonth; }

    // 週末(就寝に入った)・週の始まり(起床した)の回数のうち、まだ受け取っていない分を返し、0に戻す。
    // 週末の処理は、IsLastWeekOfMonth()が「週末が起きた時の週」を指しているうちに(週の始まりより先に)行うこと。
    int ConsumeWeekEnds();
    int ConsumeWeekStarts();

    float GetTimeScale() const { return m_timeScale; }
    // 今の時間帯での、ゲーム内の1時間の実秒数(活動時間と就寝で違う)。
    float GetSecondsPerGameHour() const { return IsSleeping() ? m_sleepSecondsPerGameHour : m_secondsPerGameHour; }

    // 今フレームで「倍率をかけた後に」進んだ秒数。停止中は0。
    // 住民の移動など、ゲーム内時間に従って進むシミュレーションはこれを使う
    // (カメラ・UIなど実時間で動くものは今まで通りTimeManagerのdeltaTimeを使う)。
    float GetScaledDeltaSeconds() const { return m_scaledDeltaSeconds; }

    // 今フレームで進んだ、ゲーム内の時間(時間単位)。就寝中に進んだ分は含めない(活動時間だけ)。
    // 施設の利用時間・需要の不足の継続時間など、「活動時間に応じて進むもの」に使う。
    float GetActiveDeltaHours() const { return m_activeDeltaHours; }

    // 時刻を直接設定する(0〜24の範囲に正規化される。日数・週は変えない)。デバッグUIのスクラブ用。
    void SetTimeOfDayHours(float hours);

    // 時間の倍率(0=停止、1=通常、2/4=倍速)。負の値は0として扱う。
    void SetTimeScale(float scale);

    // 活動時間の、ゲーム内の1時間の実秒数。
    void SetSecondsPerGameHour(float seconds);

    // ---- セーブ/ロード(ISaveable) ----
    // 保存するのは時刻・日数・週。時間の倍率(停止/倍速)はプレイヤーの操作設定なので保存しない。
    const char* SaveKey() const override { return "clock"; }
    void Save(Json& section, const SaveContext& context) const override;
    void Load(const Json& section, int version, const LoadContext& context) override;
    void ResetToDefault() override;

    // 時刻・倍率を操作するデバッグ用ImGuiパネル(「Time」ウィンドウ)。
    // openがnullptrでなければ、タイトルバーの×で閉じられ、*openがfalseの間は描かない。
    void DrawDebugUI(bool* open = nullptr);

private:
    // 就寝に入る・起床する、の区切りで起きる処理。
    void OnSleepStart();
    void OnWake();

    float m_timeOfDayHours = kStartHour;
    int m_dayCount = 1;
    int m_weekCount = 1;
    int m_pendingWeekEnds = 0;
    int m_pendingWeekStarts = 0;
    float m_secondsPerGameHour = kDefaultSecondsPerGameHour;
    float m_sleepSecondsPerGameHour = kDefaultSleepSecondsPerGameHour;
    float m_timeScale = 1.0f;
    float m_scaledDeltaSeconds = 0.0f;
    float m_activeDeltaHours = 0.0f;
};
