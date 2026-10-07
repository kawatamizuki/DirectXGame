#pragma once
#include "ISaveable.h"

// ゲーム内の時刻(1日の中の時間帯と日数)を管理する。
// 実時間のdeltaTime/FPSを扱うTimeManagerとは別物で、こちらは「街の中の時間」。
// 見た目(昼夜の光)・住民の1日の行動など、時刻に関わるものは全てこの時計を唯一の
// 時刻の源泉として参照する(時計を用途ごとに複数持つと、停止・倍速・時刻ジャンプでズレるため)。
// 「その時刻に何をするか」(出勤・帰宅の時刻など)は時計には埋め込まず、使う側のデータ
// (ResidentScheduleなど)に持たせる。
class GameClock : public ISaveable
{
public:
    // ゲーム内の1時間が実時間で何秒か。20秒なので、1日(24時間)は実時間で480秒。
    // ゲームの中身が固まったら、ここの1か所を変えれば全体の進む速さが変わる。
    static constexpr float kDefaultSecondsPerGameHour = 20.0f;

    // 起動直後の時刻(朝8時から始める)。
    static constexpr float kStartHour = 8.0f;

    // 実時間のdeltaTimeを渡して時刻を進める。倍率(0=停止)をかけた分だけ進み、
    // 24時を超えたら日数を1つ進めて0時に戻る。
    void Update(float realDeltaSeconds);

    // 0.0以上24.0未満の、1日の中の時刻(時間単位)。
    float GetTimeOfDayHours() const { return m_timeOfDayHours; }
    int GetHour() const;
    int GetMinute() const;

    // 1から数える日数。
    int GetDayCount() const { return m_dayCount; }

    float GetTimeScale() const { return m_timeScale; }
    float GetSecondsPerGameHour() const { return m_secondsPerGameHour; }

    // 今フレームで「倍率をかけた後に」進んだ秒数。停止中は0。
    // 住民の移動や需要更新など、ゲーム内時間に従って進むシミュレーションはこれを使う
    // (カメラ・UIなど実時間で動くものは今まで通りTimeManagerのdeltaTimeを使う)。
    float GetScaledDeltaSeconds() const { return m_scaledDeltaSeconds; }

    // 時刻を直接設定する(0〜24の範囲に正規化される。日数は変えない)。デバッグUIのスクラブ用。
    void SetTimeOfDayHours(float hours);

    // 時間の倍率(0=停止、1=通常、2/4=倍速)。負の値は0として扱う。
    void SetTimeScale(float scale);

    void SetSecondsPerGameHour(float seconds);

    // ---- セーブ/ロード(ISaveable) ----
    // 保存するのは時刻と日数。時間の倍率(停止/倍速)はプレイヤーの操作設定なので保存しない。
    const char* SaveKey() const override { return "clock"; }
    void Save(Json& section, const SaveContext& context) const override;
    void Load(const Json& section, int version, const LoadContext& context) override;
    void ResetToDefault() override;

    // 時刻・倍率を操作するデバッグ用ImGuiパネル(「Time」ウィンドウ)。
    void DrawDebugUI();

private:
    float m_timeOfDayHours = kStartHour;
    int m_dayCount = 1;
    float m_secondsPerGameHour = kDefaultSecondsPerGameHour;
    float m_timeScale = 1.0f;
    float m_scaledDeltaSeconds = 0.0f;
};
