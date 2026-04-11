
struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR;
};


struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

cbuffer ConstantBuffer : register(b0)
{
    matrix WVP;
};


PSInput VSMain(VSInput input)
{
    PSInput output;

    output.position = mul(float4(input.position, 1.0f), WVP);

    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
