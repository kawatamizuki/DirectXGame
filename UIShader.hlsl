// ゲームプレイ用UI(UIRenderer)のシェーダー。
// 頂点はピクセル座標(左上原点)で渡され、VSで画面座標(-1〜1)へ変換する。
// 図形(単色)も文字も同じアトラス(R8の1チャンネル)を使う: 図形は白い領域のUVを指すので
// アトラスの値が1になり、文字は文字の形(0〜1)がそのままアルファになる。

Texture2D atlasTexture : register(t0);
SamplerState atlasSampler : register(s0);

// C++側の UIConstants(UIRenderer.cpp)と対応している。register(b0)。
cbuffer UIConstants : register(b0)
{
    float twoOverWidth;  // 2 / 画面の幅(ピクセル)
    float twoOverHeight; // 2 / 画面の高さ(ピクセル)
    float2 padding;      // 16バイト境界に合わせるための詰め物
};

struct VSInput
{
    float2 position : POSITION; // ピクセル座標
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

PSInput VSMain(VSInput input)
{
    PSInput output;

    // 左上原点のピクセル座標 → 画面座標(x:-1〜1、y:1〜-1)。
    output.position = float4(
        input.position.x * twoOverWidth - 1.0f,
        1.0f - input.position.y * twoOverHeight,
        0.0f,
        1.0f);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float coverage = atlasTexture.Sample(atlasSampler, input.uv).r;
    return float4(input.color.rgb, input.color.a * coverage);
}
