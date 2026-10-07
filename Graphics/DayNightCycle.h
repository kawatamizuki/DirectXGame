#pragma once
#include "LightingState.h"

// ゲーム内の時刻(0〜24時)から、その時の光の状態(LightingState)を求める。
// 状態を持たない純粋な計算なので、別の昼夜サイクル(季節ごとの違いなど)に
// 差し替えたくなったら、同じ形のクラスを作って置き換えるだけで済む。
//
// 明るさは「時計の時刻」ではなく「昼のどのあたりか/夜のどのあたりか」で決める。
// 夜は日没から日の出までずっと同じ明るさ(真夜中だけ特別に暗くなったりしない)で、
// 昼は日の出から日没までの進み具合(朝焼け→昼→夕焼け)で明るさが決まる。
// こうしておけば、季節で日の出・日没の時刻が変わっても(冬は昼が短い等)、
// 「この時間帯はこの明るさ」という見た目の規則が崩れない。
class DayNightCycle
{
public:
    // その日の昼の長さ(日の出〜日没の時刻)。季節の仕組みを足す時は、季節に応じた値を
    // SetDaylightSpanで渡す(既定は6時〜18時)。夜の長さは残りの時間になる。
    void SetDaylightSpan(float sunriseHour, float sunsetHour);

    // timeOfDayHours: 0.0以上24.0未満の時刻。範囲外の値は0〜24に巻き戻して扱う。
    LightingState Evaluate(float timeOfDayHours) const;

    // timeOfDayHoursが日の出から日没までの間(昼)か。UIの太陽/月のアイコンの切り替えなどに使う。
    bool IsDaytime(float timeOfDayHours) const;

private:
    float m_sunriseHour = 6.0f;
    float m_sunsetHour = 18.0f;
};
