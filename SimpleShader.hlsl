
// 光の計算(定数バッファb2とApplyLighting)。画風を変える/影を足す時はここだけ触る。
#include "Lighting.hlsli"

Texture2D diffuseTexture : register(t0);
SamplerState textureSampler : register(s0);


struct VSInput
{
    float3 position : POSITION; // 頂点位置
    float3 normal : NORMAL; // 法線
    float2 uv : TEXCOORD; // UV
    float4 color : COLOR; // 色
};

struct PSInput
{
    float4 position : SV_POSITION; // 画面座標
    float3 normal : NORMAL; // 法線（そのまま渡す）
    float2 uv : TEXCOORD; // UV（そのまま渡す）
    float4 color : COLOR; // 色
};
cbuffer ConstantBuffer : register(b0)
{
    matrix WVP;
    matrix World; // ワールド行列(法線をワールド空間へ変換して、光との向きを比べるために使う)
};

// Material用の定数バッファ
// C++側の MaterialBuffer と対応している。
// register(b1) なので、C++側では PSSetConstantBuffers(1, 1, &m_materialBuffer) で渡す。
cbuffer MaterialBuffer : register(b1)
{
    int hasTexture; // 1ならテクスチャあり、0ならテクスチャなし
    float alpha;    // 不透明度(1.0=不透明。配置プレビューのゴースト表示などで使う)
    float2 padding; // 16バイト境界に合わせるための詰め物
};


PSInput VSMain(VSInput input)
{
    PSInput output;

    // 頂点座標をWVPで変換
    output.position = mul(float4(input.position, 1.0f), WVP);

    // 法線をワールド空間へ変換して渡す(ピクセルシェーダーで光との向きを比べる)。
    // 拡大が一様でない地面でも、平面の法線(上向き)は変わらないので逆転置行列は使わない。
    // 法線が零ベクトル(法線を持たないモデル)の場合は上向きとして扱い、真っ黒にならないようにする。
    float3 worldNormal = mul(float4(input.normal, 0.0f), World).xyz;
    float normalLength = length(worldNormal);
    output.normal = (normalLength > 0.0001f) ? (worldNormal / normalLength) : float3(0.0f, 1.0f, 0.0f);
    output.uv = input.uv;
    output.color = input.color;

    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
       // テクスチャなしの場合
    // diffuseTexture.Sample() は行わず、頂点カラーをそのまま使う。
    if (hasTexture == 0)
    {
        return float4(ApplyLighting(input.color.rgb, input.normal), alpha);
    }

     // UV反転はここでは行わない。
    // ObjLoader側で DirectX 用に変換しておく。
    float2 uv = input.uv;

    float4 texColor = diffuseTexture.Sample(textureSampler, uv);

    return float4(ApplyLighting(texColor.rgb, input.normal), alpha);
}
