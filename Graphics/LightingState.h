#pragma once
#include <DirectXMath.h>

// 1フレームぶんの「光の状態」。描画側(Renderer)はこの構造体だけを受け取って描く。
// 光の状態を誰がどう決めるか(昼夜サイクル・固定の屋内照明・イベント演出など)は
// Rendererは知らないので、時間の仕組みを差し替えても描画側は触らずに済む。
// 将来、影(シャドウマップ)や画風(トゥーン/ドット化)を足す時も、光の向き(directionToLight)
// などはここから取るので、光の情報の出どころは常にこの1つになる。
struct LightingState
{
    // 光源(昼は太陽、夜は月)へ向かう向き(正規化済み)。法線との内積で明るさを決める。
    DirectX::XMFLOAT3 directionToLight{ 0.0f, 1.0f, 0.0f };

    // 光の色。強さ(明るさ)も込みの値(0〜1を超えてもよい)。
    DirectX::XMFLOAT3 lightColor{ 0.6f, 0.6f, 0.55f };

    // 環境光(光が直接当たらない面にも届く、全方位からの弱い光)。
    // 上向きの面ほどambientSky、下向きの面ほどambientGroundに近づける(半球環境光)。
    DirectX::XMFLOAT3 ambientSky{ 0.48f, 0.54f, 0.62f };
    DirectX::XMFLOAT3 ambientGround{ 0.32f, 0.32f, 0.30f };

    // 背景(空)の色。画面クリア色として使う。
    DirectX::XMFLOAT3 skyColor{ 0.53f, 0.75f, 0.9f };
};
