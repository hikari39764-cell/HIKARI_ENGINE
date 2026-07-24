cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
};

struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(gViewProj, float4(input.position, 1.0f));
    output.color = input.color;
    return output;
}
