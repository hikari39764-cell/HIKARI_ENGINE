cbuffer CommonParams : register(b0)
{
    float gTime;
    float gDeltaTime;
    float gCombo;
    float gIntensity;
    float gResolutionX;
    float gResolutionY;
    float2 gCommonPadding;
    float4 gUser[16];
};

Texture2D gSceneTex : register(t0);
SamplerState gSamp : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 color = gSceneTex.Sample(gSamp, input.uv);
    float threshold = max(gUser[0].x, 0.0f);
    float intensity = max(gUser[0].y, 0.0f);
    float luminance = dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    float bloomAmount = saturate(luminance - threshold);
    return float4(color.rgb * bloomAmount, intensity);
}
