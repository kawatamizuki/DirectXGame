#pragma once
#include "LightingState.h"

// ゲーム内の時刻(0〜24時)から、その時の光の状態(LightingState)を求める。
// 状態を持たない純粋な計算なので、別の昼夜サイクル(季節ごとの違いなど)に
// 差し替えたくなったら、同じ形のクラスを作って置き換えるだけで済む。
class DayNightCycle
{
public:
    // timeOfDayHours: 0.0以上24.0未満の時刻。範囲外の値は0〜24に巻き戻して扱う。
    LightingState Evaluate(float timeOfDayHours) const;
};
