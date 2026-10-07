#pragma once
#include <cstdint>

// 代表住民が「今の時刻にどこにいるはずか」。
enum class ScheduledActivity
{
    Home, // 自宅にいる時間帯(夜・早朝・夕方以降)
    Work  // 職場にいる時間帯(日中)
};

// 住民の1日の行動の予定表。
// 「何時に出勤し、何時に帰宅するか」というゲーム内容に依存する数値は、ゲーム内時刻を
// 管理するGameClockには埋め込まず、このデータに持たせる(DemandSystemも時刻の数字を持たない)。
// ゲームの中身が固まったら、ここの値だけを調整すればよい。将来、職業ごと・季節ごとに
// 予定表を変えたくなったら、住民ごとにこの構造体を持たせる形に広げられる。
struct ResidentSchedule
{
    float leaveHomeHour = 7.5f;   // 出勤を始める時刻(時間単位。7.5=7時30分)
    float leaveWorkHour = 17.0f;  // 退勤して帰宅を始める時刻
    float maxOffsetHours = 1.0f;  // 住民ごとのばらつきの最大幅(±この時間)。全員が同じ瞬間に動かないようにする

    // 住民(idで識別)ごとの、予定の時刻のずれ。idから決まる固定の値(同じ住民は毎日同じずれ)。
    // -maxOffsetHours〜+maxOffsetHoursの範囲。
    float OffsetForAgent(uint32_t agentId) const
    {
        // 簡単な整数ハッシュ(Knuthの乗算ハッシュ)で、連番のidでも値が散らばるようにする。
        uint32_t hashed = agentId * 2654435761u;
        float unit = static_cast<float>((hashed >> 8) & 0xFFFFu) / 65535.0f; // 0〜1
        return (unit * 2.0f - 1.0f) * maxOffsetHours;
    }

    // hours(0〜24の時刻)に、offsetHoursだけずらした予定を適用した時、住民がいるべき場所。
    ScheduledActivity ActivityAt(float hours, float offsetHours) const
    {
        bool atWorkTime = (hours >= leaveHomeHour + offsetHours) && (hours < leaveWorkHour + offsetHours);
        return atWorkTime ? ScheduledActivity::Work : ScheduledActivity::Home;
    }
};
