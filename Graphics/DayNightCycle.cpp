#include "DayNightCycle.h"
#include <cmath>

using namespace DirectX;

namespace
{
    // ある時刻の光の色の設定(キーフレーム)。時刻の間は滑らかに補間される。
    // 見た目の調整はこの表の数値を変えるだけで済むよう、色の数値はここに集約している。
    struct LightKey
    {
        float hour;
        XMFLOAT3 sky;           // 空(背景)の色
        XMFLOAT3 light;         // 太陽/月の光の色(強さ込み)
        XMFLOAT3 ambientSky;    // 上向きの面に届く環境光
        XMFLOAT3 ambientGround; // 下向きの面に届く環境光
    };

    // 時刻の昇順。先頭(0時)と末尾(24時)は同じ値にして、日付の切り替わりで色が飛ばないようにする。
    // 夜でも建物の配置操作ができるよう、環境光は昼の3割程度を下限にしている。
    const LightKey kKeys[] =
    {
        {  0.0f, { 0.03f, 0.04f, 0.10f }, { 0.10f, 0.12f, 0.22f }, { 0.20f, 0.24f, 0.38f }, { 0.10f, 0.12f, 0.20f } }, // 深夜
        {  5.0f, { 0.06f, 0.07f, 0.16f }, { 0.12f, 0.13f, 0.25f }, { 0.22f, 0.25f, 0.40f }, { 0.11f, 0.12f, 0.20f } }, // 夜明け前
        {  6.5f, { 0.95f, 0.60f, 0.40f }, { 0.67f, 0.39f, 0.21f }, { 0.45f, 0.40f, 0.45f }, { 0.28f, 0.24f, 0.25f } }, // 朝焼け
        {  8.5f, { 0.60f, 0.78f, 0.92f }, { 0.78f, 0.68f, 0.54f }, { 0.45f, 0.50f, 0.58f }, { 0.30f, 0.30f, 0.30f } }, // 朝
        { 12.0f, { 0.53f, 0.75f, 0.90f }, { 0.62f, 0.60f, 0.54f }, { 0.48f, 0.54f, 0.62f }, { 0.32f, 0.32f, 0.30f } }, // 昼
        { 16.5f, { 0.58f, 0.76f, 0.90f }, { 0.74f, 0.66f, 0.52f }, { 0.46f, 0.50f, 0.56f }, { 0.30f, 0.29f, 0.28f } }, // 午後
        { 18.5f, { 0.95f, 0.50f, 0.30f }, { 0.63f, 0.29f, 0.14f }, { 0.45f, 0.35f, 0.42f }, { 0.27f, 0.22f, 0.24f } }, // 夕焼け
        { 20.0f, { 0.20f, 0.14f, 0.30f }, { 0.20f, 0.17f, 0.30f }, { 0.28f, 0.27f, 0.42f }, { 0.14f, 0.13f, 0.22f } }, // 薄暮
        { 22.0f, { 0.05f, 0.06f, 0.14f }, { 0.11f, 0.13f, 0.24f }, { 0.21f, 0.25f, 0.39f }, { 0.10f, 0.12f, 0.20f } }, // 夜
        { 24.0f, { 0.03f, 0.04f, 0.10f }, { 0.10f, 0.12f, 0.22f }, { 0.20f, 0.24f, 0.38f }, { 0.10f, 0.12f, 0.20f } }, // 深夜(0時と同じ)
    };
    constexpr int kKeyCount = static_cast<int>(sizeof(kKeys) / sizeof(kKeys[0]));

    // 日の出・日の入りの時刻。この間は太陽、それ以外は月が光源になる。
    constexpr float kSunriseHour = 6.0f;
    constexpr float kSunsetHour = 18.0f;

    // 光源の向きを南(-Z側)へ傾ける量。真東→頭上→真西の円弧だと、正午に真上から
    // 照らして壁の面がほとんど暗くなるため、少し傾けて壁にも光が当たるようにする。
    constexpr float kLightSouthTilt = 0.35f;

    // 光源が地平線に近い間は光を絞る範囲(仰角のsinがこの値以下で絞り始める)。
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

LightingState DayNightCycle::Evaluate(float timeOfDayHours) const
{
    // 0〜24に巻き戻す。
    float hours = std::fmod(timeOfDayHours, 24.0f);
    if (hours < 0.0f)
    {
        hours += 24.0f;
    }

    // hoursを挟む2つのキーフレームを探して補間する(滑らかに繋がるようSmoothStepで緩急をつける)。
    int upper = 1;
    while (upper < kKeyCount - 1 && kKeys[upper].hour <= hours)
    {
        upper++;
    }
    const LightKey& a = kKeys[upper - 1];
    const LightKey& b = kKeys[upper];

    float span = b.hour - a.hour;
    float t = (span > 0.0001f) ? SmoothStep01((hours - a.hour) / span) : 0.0f;

    LightingState state;
    state.skyColor = Lerp(a.sky, b.sky, t);
    state.ambientSky = Lerp(a.ambientSky, b.ambientSky, t);
    state.ambientGround = Lerp(a.ambientGround, b.ambientGround, t);
    XMFLOAT3 lightColor = Lerp(a.light, b.light, t);

    // 光源の向き。太陽は日の出(東=+X)の地平線→正午に頭上→日の入り(西=-X)へ円弧を描く。
    // 地平線の下に沈んでいる間は、太陽と反対側(月)に切り替える(月は18時に東から昇り、
    // 真夜中に頭上、6時に西へ沈む)。
    float sunAngle = (hours - kSunriseHour) / (kSunsetHour - kSunriseHour) * XM_PI; // 6時=0、18時=π
    float elevationSin = std::sin(sunAngle);
    float horizontal = std::cos(sunAngle);

    XMFLOAT3 toLight;
    if (elevationSin >= 0.0f)
    {
        toLight = XMFLOAT3(horizontal, elevationSin, -kLightSouthTilt);
    }
    else
    {
        toLight = XMFLOAT3(-horizontal, -elevationSin, -kLightSouthTilt);
    }
    XMVECTOR toLightVec = XMVector3Normalize(XMLoadFloat3(&toLight));
    XMStoreFloat3(&state.directionToLight, toLightVec);

    // 地平線付近は光を絞る(向きが切り替わる瞬間の強さを0にして、飛びを見せない)。
    float horizonFade = SmoothStep01(std::fabs(elevationSin) / kHorizonFadeRange);
    state.lightColor = XMFLOAT3(
        lightColor.x * horizonFade,
        lightColor.y * horizonFade,
        lightColor.z * horizonFade);

    return state;
}
