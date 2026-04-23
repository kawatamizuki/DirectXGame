
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
    return input.color;
}