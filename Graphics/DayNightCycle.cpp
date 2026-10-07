#include "DayNightCycle.h"
#include <cmath>

using namespace DirectX;

namespace
{
    // 光の色の設定(キーフレーム)。時刻の間は滑らかに補間される。
    // 見た目の調整はこの表の数値を変えるだけで済むよう、色の数値はここに集約している。
    //
    // 表の「hour」は実際の時計の時刻ではなく、昼の長さを6時〜18時として並べた「基準の時刻」。
    // 実際の時刻は、日の出〜日没の進み具合(昼)/日没〜日の出の進み具合(夜)を通して
    // この基準の時刻に換算してから表を引く。既定(6時〜18時)なら実際の時刻と一致する。
    struct LightKey
    {
        float hour;
        XMFLOAT3 sky;           // 空(背景)の色
        XMFLOAT3 light;         // 太陽/月の光の色(強さ込み)
        XMFLOAT3 ambientSky;    // 上向きの面に届く環境光
        XMFLOAT3 ambientGround; // 下向きの面に届く環境光
    };

    // 夜の色。日没後しばらくしてから日の出の少し前まで、この色で一定にする
    // (真夜中だけ暗くなる、夜明け前だけ明るくなる、といった変化を付けない)。
    // 夜でも建物の配置操作ができるよう、環境光は昼の3割程度を下限にしている。
    #define NIGHT_KEY_VALUES \
        { 0.05f, 0.06f, 0.14f }, { 0.11f, 0.13f, 0.24f }, { 0.21f, 0.25f, 0.39f }, { 0.10f, 0.12f, 0.20f }

    // 基準の時刻の昇順。先頭(0時)と末尾(24時)は同じ値にして、日付の切り替わりで色が飛ばないようにする。
    const LightKey kKeys[] =
    {
        {  0.0f, NIGHT_KEY_VALUES },                                                                                          // 夜(一定)
        {  4.5f, NIGHT_KEY_VALUES },                                                                                          // 夜(一定)。ここから夜明けの色へ変わり始める
        {  6.5f, { 0.95f, 0.60f, 0.40f }, { 0.67f, 0.39f, 0.21f }, { 0.45f, 0.40f, 0.45f }, { 0.28f, 0.24f, 0.25f } },        // 朝焼け
        {  8.5f, { 0.60f, 0.78f, 0.92f }, { 0.78f, 0.68f, 0.54f }, { 0.45f, 0.50f, 0.58f }, { 0.30f, 0.30f, 0.30f } },        // 朝
        { 12.0f, { 0.53f, 0.75f, 0.90f }, { 0.62f, 0.60f, 0.54f }, { 0.48f, 0.54f, 0.62f }, { 0.32f, 0.32f, 0.30f } },        // 昼
        { 16.5f, { 0.58f, 0.76f, 0.90f }, { 0.74f, 0.66f, 0.52f }, { 0.46f, 0.50f, 0.56f }, { 0.30f, 0.29f, 0.28f } },        // 午後
        { 18.5f, { 0.95f, 0.50f, 0.30f }, { 0.63f, 0.29f, 0.14f }, { 0.45f, 0.35f, 0.42f }, { 0.27f, 0.22f, 0.24f } },        // 夕焼け
        { 19.5f, { 0.20f, 0.14f, 0.30f }, { 0.20f, 0.17f, 0.30f }, { 0.28f, 0.27f, 0.42f }, { 0.14f, 0.13f, 0.22f } },        // 薄暮
        { 20.5f, NIGHT_KEY_VALUES },                                                                                          // 夜(一定)。ここから朝まで変化しない
        { 24.0f, NIGHT_KEY_VALUES },                                                                                          // 夜(0時と同じ)
    };
    constexpr int kKeyCount = static_cast<int>(sizeof(kKeys) / sizeof(kKeys[0]));

    // 色の表(kKeys)が前提にしている、基準の日の出・日没の時刻と、そこから決まる昼・夜の長さ。
    constexpr float kRefSunriseHour = 6.0f;
    constexpr float kRefSunsetHour = 18.0f;
    constexpr float kRefDayLength = kRefSunsetHour - kRefSunriseHour;   // 12時間
    constexpr float kRefNightLength = 24.0f - kRefDayLength;            // 12時間

    // 光源の向きを南(-Z側)へ傾ける量。真東→頭上→真西の円弧だと、正午に真上から
    // 照らして壁の面がほとんど暗くなるため、少し傾けて壁にも光が当たるようにする。
    constexpr float kLightSouthTilt = 0.35f;

    // 夜の月の高さ(仰角のsin)。一定にしておくことで、月の位置によって地面や屋根の明るさが
    // 夜の間に変わらないようにする(東から昇って西へ沈むので、向きだけが動く)。
    constexpr float kMoonElevationSin = 0.85f;

    // 光源が地平線に近い間は光を絞る範囲(太陽の仰角のsinがこの値以下で絞り始める)。
    // 太陽→月の切り替わりで光の向きが一瞬で反対側へ飛ぶが、その瞬間は強さが0なので目立たない。
    constexpr float kHorizonFadeRange = 0.15f;

    float SmoothStep01(float t)
    {
        if (t < 0.0f) { t = 0.0f; }
        if (t > 1.0f) { t = 1.0f; }
        return t * t * (3.0f - 2.0f * t);
    }

    XMFLOAT3 Lerp(const XMFLOAT3& a, const XMFLOAT3& b, float t)
    {
        return XMFLOAT3(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t);
    }
}

void DayNightCycle::SetDaylightSpan(float sunriseHour, float sunsetHour)
{
    // 日の出<日没で、どちらも0〜24の範囲に収まっていないと昼夜の割り算が成り立たない。
    // 不正な値は無視して、今の設定のままにする。
    constexpr float kMinSpan = 1.0f;
    if (sunriseHour < 0.0f || sunsetHour > 24.0f || sunsetHour - sunriseHour < kMinSpan)
    {
        return;
    }
    m_sunriseHour = sunriseHour;
    m_sunsetHour = sunsetHour;
}

bool DayNightCycle::IsDaytime(float timeOfDayHours) const
{
    float hours = std::fmod(timeOfDayHours, 24.0f);
    if (hours < 0.0f)
    {
        hours += 24.0f;
    }
    return hours >= m_sunriseHour && hours < m_sunsetHour;
}

LightingState DayNightCycle::Evaluate(float timeOfDayHours) const
{
    // 0〜24に巻き戻す。
    float hours = std::fmod(timeOfDayHours, 24.0f);
    if (hours < 0.0f)
    {
        hours += 24.0f;
    }

    float dayLength = m_sunsetHour - m_sunriseHour;
    float nightLength = 24.0f - dayLength;

    // 実際の時刻hoursを、「昼のどのあたりか/夜のどのあたりか」を通して、色の表の基準の時刻に換算する。
    // 同時に、光源(太陽/月)の向きと、太陽の仰角のsin(昼は正、夜は負)も求める。
    float tableHour;       // 色の表を引くための基準の時刻(0〜24)
    float sunElevationSin; // 太陽の仰角のsin(地平線の下は負)
    XMFLOAT3 toLight;

    bool isDay = (hours >= m_sunriseHour && hours < m_sunsetHour);
    if (isDay)
    {
        // 昼: 日の出(進み具合0)→日没(進み具合1)。太陽は東(+X)の地平線→頭上→西(-X)へ円弧を描く。
        float progress = (hours - m_sunriseHour) / dayLength;
        float angle = progress * XM_PI;
        tableHour = kRefSunriseHour + progress * kRefDayLength;
        sunElevationSin = std::sin(angle);
        toLight = XMFLOAT3(std::cos(angle), sunElevationSin, -kLightSouthTilt);
    }
    else
    {
        // 夜: 日没(進み具合0)→日の出(進み具合1)。月は東から昇って西へ沈むが、高さは一定。
        float sinceSunset = hours - m_sunsetHour;
        if (sinceSunset < 0.0f)
        {
            sinceSunset += 24.0f;
        }
        float progress = sinceSunset / nightLength;
        float angle = progress * XM_PI;

        tableHour = std::fmod(kRefSunsetHour + progress * kRefNightLength, 24.0f);
        sunElevationSin = -std::sin(angle);

        // 月は東(+X)から昇り、南(-Z)の空を通って、西(-X)へ沈む。水平方向の長さ
        // (sqrt(1 - 高さ^2))を一定に保って回すので、向きのベクトルは最初から長さ1になり、
        // 月の位置が変わっても地面や屋根に当たる光の量が変わらない(夜の間ずっと同じ明るさ)。
        float moonHorizontalScale = std::sqrt(1.0f - kMoonElevationSin * kMoonElevationSin);
        toLight = XMFLOAT3(
            std::cos(angle) * moonHorizontalScale,
            kMoonElevationSin,
            -std::sin(angle) * moonHorizontalScale);
    }

    // tableHourを挟む2つのキーフレームを探して補間する(滑らかに繋がるようSmoothStepで緩急をつける)。
    int upper = 1;
    while (upper < kKeyCount - 1 && kKeys[upper].hour <= tableHour)
    {
        upper++;
    }
    const LightKey& a = kKeys[upper - 1];
    const LightKey& b = kKeys[upper];

    float span = b.hour - a.hour;
    float t = (span > 0.0001f) ? SmoothStep01((tableHour - a.hour) / span) : 0.0f;

    LightingState state;
    state.skyColor = Lerp(a.sky, b.sky, t);
    state.ambientSky = Lerp(a.ambientSky, b.ambientSky, t);
    state.ambientGround = Lerp(a.ambientGround, b.ambientGround, t);
    XMFLOAT3 lightColor = Lerp(a.light, b.light, t);

    XMVECTOR toLightVec = XMVector3Normalize(XMLoadFloat3(&toLight));
    XMStoreFloat3(&state.directionToLight, toLightVec);

    // 地平線付近は光を絞る(向きが切り替わる瞬間の強さを0にして、飛びを見せない)。
    // 夜は太陽の仰角が負なので、日没直後と日の出直前(太陽が地平線の近く)だけ絞られ、
    // 夜の間(太陽が十分に深い所)は絞られず一定の明るさになる。
    float horizonFade = SmoothStep01(std::fabs(sunElevationSin) / kHorizonFadeRange);
    state.lightColor = XMFLOAT3(
        lightColor.x * horizonFade,
        lightColor.y * horizonFade,
        lightColor.z * horizonFade);

    return state;
}
