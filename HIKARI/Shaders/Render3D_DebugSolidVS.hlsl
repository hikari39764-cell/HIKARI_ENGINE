cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4 gViewport;
    float4 gDebugExposure; // x: P/E
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
    output.color = float4(input.color.rgb * gDebugExposure.x, input.color.a);
    return output;
}
