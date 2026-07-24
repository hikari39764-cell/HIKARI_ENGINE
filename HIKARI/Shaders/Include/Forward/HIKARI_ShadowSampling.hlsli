#ifndef HIKARI_FORWARD_SHADOW_SAMPLING_INCLUDED
#define HIKARI_FORWARD_SHADOW_SAMPLING_INCLUDED

float HikariSampleShadowCompare(float2 uv, float currentDepth)
{
    return gShadowMap.SampleCmpLevelZero(gShadowSampler, uv, currentDepth);
}

float HikariSampleShadowPcf(float2 uv, float currentDepth)
{
    const float center = HikariSampleShadowCompare(uv, currentDepth);
    if (gShadowPcfEnabled == 0 || gShadowPcfRadius <= 0.00001f)
    {
        return center;
    }

    const float2 texelSize =
        float2(gShadowTexelSizeX, gShadowTexelSizeY) * gShadowPcfRadius;

    float sum = 0.0f;
    sum += HikariSampleShadowCompare(uv + texelSize * float2(-1.0f, -1.0f), currentDepth);
    sum += HikariSampleShadowCompare(uv + texelSize * float2( 0.0f, -1.0f), currentDepth);
    sum += HikariSampleShadowCompare(uv + texelSize * float2( 1.0f, -1.0f), currentDepth);
    sum += HikariSampleShadowCompare(uv + texelSize * float2(-1.0f,  0.0f), currentDepth);
    sum += center;
    sum += HikariSampleShadowCompare(uv + texelSize * float2( 1.0f,  0.0f), currentDepth);
    sum += HikariSampleShadowCompare(uv + texelSize * float2(-1.0f,  1.0f), currentDepth);
    sum += HikariSampleShadowCompare(uv + texelSize * float2( 0.0f,  1.0f), currentDepth);
    sum += HikariSampleShadowCompare(uv + texelSize * float2( 1.0f,  1.0f), currentDepth);
    return sum / 9.0f;
}

float HikariShadowReceiverFade(float2 uv)
{
    if (gShadowEdgeFade <= 0.00001f)
    {
        return 1.0f;
    }

    const float edgeDistance =
        min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y));
    return saturate(edgeDistance / gShadowEdgeFade);
}

float SampleDirectionalShadow(float3 worldPosWS, float3 geometricNormalWS, uint receiveShadow)
{
    if (gShadowEnabled == 0 || receiveShadow == 0)
    {
        return 1.0f;
    }

    const float3 biasedWorldPos = worldPosWS + geometricNormalWS * gShadowNormalBias;
    const float4 lightClip = mul(gShadowLightViewProj, float4(biasedWorldPos, 1.0f));
    if (abs(lightClip.w) < 1e-5f)
    {
        return 1.0f;
    }

    const float3 proj = lightClip.xyz / lightClip.w;
    const float2 uv = float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);
    if (uv.x < 0.0f || uv.x > 1.0f ||
        uv.y < 0.0f || uv.y > 1.0f ||
        proj.z < 0.0f || proj.z > 1.0f)
    {
        return 1.0f;
    }

    const float currentDepth = proj.z - gShadowDepthBias;
    const float visibility = HikariSampleShadowPcf(uv, currentDepth);
    const float shadowFactor = lerp(1.0f - gShadowStrength, 1.0f, visibility);
    return lerp(1.0f, shadowFactor, HikariShadowReceiverFade(uv));
}

#endif
