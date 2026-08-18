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

float3 HikariSampleFiniteBloom(float2 uv)
{
    float3 color = gBloomTex.SampleLevel(gSamp, uv, 0.0f).rgb;
    return all(isfinite(color)) ? max(color, 0.0f.xxx) : 0.0f.xxx;
}

float4 main(PSInput input) : SV_TARGET
{
    float radius = max(gUser[0].z, 0.0f);
    float2 texel = float2(0.0f, gUser[1].y) * radius;
    float3 sum = HikariSampleFiniteBloom(input.uv) * 0.40f;
    sum += HikariSampleFiniteBloom(input.uv + texel * 1.0f) * 0.24f;
    sum += HikariSampleFiniteBloom(input.uv - texel * 1.0f) * 0.24f;
    sum += HikariSampleFiniteBloom(input.uv + texel * 2.0f) * 0.06f;
    sum += HikariSampleFiniteBloom(input.uv - texel * 2.0f) * 0.06f;
    return float4(sum, 0.0f);
}
