
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
};

// Material用の定数バッファ
// C++側の MaterialBuffer と対応している。
// register(b1) なので、C++側では PSSetConstantBuffers(1, 1, &m_materialBuffer) で渡す。
cbuffer MaterialBuffer : register(b1)
{
    int hasTexture; // 1ならテクスチャあり、0ならテクスチャなし
    float3 padding; // 16バイト境界に合わせるための詰め物
    float4 tint;
};

// The renderer updates this every frame. Future day/night shaders can use these
// values without changing the simulation clock or game code.
cbuffer WorldTimeBuffer : register(b2)
{
    float timeOfDay01;
    float daylight01;
    float2 worldTimePadding;
};


PSInput VSMain(VSInput input)
{
    PSInput output;

    // 頂点座標をWVPで変換
    output.position = mul(float4(input.position, 1.0f), WVP);

    // 今はそのまま渡す（まだ計算しない）
    output.normal = input.normal;
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
        return input.color * tint;
    }
    
     // UV反転はここでは行わない。
    // ObjLoader側で DirectX 用に変換しておく。
    float2 uv = input.uv;

    float4 texColor = diffuseTexture.Sample(textureSampler, uv);

    return float4(texColor.rgb, 1.0f) * tint;
}

