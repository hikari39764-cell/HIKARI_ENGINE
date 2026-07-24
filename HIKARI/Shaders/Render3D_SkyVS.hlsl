cbuffer SkyCB : register(b0)
{
    float4x4 gWorldViewProj;
    float4 gSkyZenithExposure;
    float4 gSkyHorizonPower;
    float4 gSkyGroundYaw;
    float4 gSkyTintMode;
    float4 gSkySunDirectionIntensity;
    float4 gSkySunSizeParams;
};

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 localDir : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 clipPosition = mul(gWorldViewProj, float4(input.position, 1.0f));
    clipPosition.z = clipPosition.w;
    output.position = clipPosition;
    output.localDir = input.position;
    return output;
}
