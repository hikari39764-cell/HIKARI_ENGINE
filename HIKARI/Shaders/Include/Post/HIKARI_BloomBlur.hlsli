#ifndef HIKARI_BLOOM_BLUR_INCLUDED
#define HIKARI_BLOOM_BLUR_INCLUDED

Texture2D gBloomTex : register(t0);
SamplerState gSamp : register(s0);

float3 HikariSampleFiniteBloom(float2 uv)
{
    float3 color = gBloomTex.SampleLevel(gSamp, uv, 0.0f).rgb;
    return all(isfinite(color)) ? max(color, 0.0f.xxx) : 0.0f.xxx;
}

float4 HikariBloomBlur(float2 uv, float2 direction)
{
    float radius = max(gUser[0].z, 0.0f);
    float2 texel = direction * radius;
    float3 sum = HikariSampleFiniteBloom(uv) * 0.40f;
    sum += HikariSampleFiniteBloom(uv + texel * 1.0f) * 0.24f;
    sum += HikariSampleFiniteBloom(uv - texel * 1.0f) * 0.24f;
    sum += HikariSampleFiniteBloom(uv + texel * 2.0f) * 0.06f;
    sum += HikariSampleFiniteBloom(uv - texel * 2.0f) * 0.06f;
    return float4(sum, 0.0f);
}

#endif
