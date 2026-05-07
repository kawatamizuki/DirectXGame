
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
    //テクスチャ読み込みをする前
    //return input.color;
    
    // NG例:
    // OBJのUVとDirectXで読み込んだ画像のV方向が逆になる場合がある。
    // そのまま input.uv を使うと、テクスチャの上下が合わず
    // 黒く見えたり、意図しない表示になることがある。
    //
    // float4 texColor = diffuseTexture.Sample(textureSampler, input.uv);
    // return float4(texColor.rgb, 1.0f);

    //objloader側で修正予定
    
    // V方向を反転して、OBJのUVとDirectXのテクスチャ座標を合わせる
    float2 uv = float2(input.uv.x, 1.0f - input.uv.y);

    float4 texColor = diffuseTexture.Sample(textureSampler, uv);

    // alphaを1.0に固定して、不透明として表示する
    return float4(texColor.rgb, 1.0f);
}