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

Texture2D gBloomTex : register(t0);
SamplerState gSamp : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float radius = max(gUser[0].z, 0.0f);
    float2 texel = float2(gUser[1].x, 0.0f) * radius;
    float4 sum = gBloomTex.Sample(gSamp, input.uv) * 0.40f;
    sum += gBloomTex.Sample(gSamp, input.uv + texel * 1.0f) * 0.24f;
    sum += gBloomTex.Sample(gSamp, input.uv - texel * 1.0f) * 0.24f;
    sum += gBloomTex.Sample(gSamp, input.uv + texel * 2.0f) * 0.06f;
    sum += gBloomTex.Sample(gSamp, input.uv - texel * 2.0f) * 0.06f;
    return sum;
}
