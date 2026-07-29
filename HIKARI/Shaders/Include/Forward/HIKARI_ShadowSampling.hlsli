#ifndef HIKARI_FORWARD_SHADOW_SAMPLING_INCLUDED
#define HIKARI_FORWARD_SHADOW_SAMPLING_INCLUDED

float2 HikariShadowAtlasTileOrigin(uint cascadeIndex)
{
    return
        float2(
            (float)(cascadeIndex & 1u),
            (float)(cascadeIndex >> 1u)) *
        0.5f;
}

float2 HikariClampShadowAtlasUv(float2 uv, uint cascadeIndex)
{
    const float2 tileOrigin =
        HikariShadowAtlasTileOrigin(cascadeIndex);
    const float2 halfTexel =
        float2(gShadowTexelSizeX, gShadowTexelSizeY) * 0.5f;
    return clamp(
        uv,
        tileOrigin + halfTexel,
        tileOrigin + gShadowCascadeParams.zw - halfTexel);
}

float HikariSampleShadowCompare(
    float2 uv,
    float currentDepth,
    uint cascadeIndex)
{
    return gShadowMap.SampleCmpLevelZero(
        gShadowSampler,
        HikariClampShadowAtlasUv(uv, cascadeIndex),
        currentDepth);
}

float HikariSampleShadowPcf(
    float2 uv,
    float currentDepth,
    uint cascadeIndex)
{
    const float center =
        HikariSampleShadowCompare(uv, currentDepth, cascadeIndex);
    if (gShadowPcfEnabled == 0 || gShadowPcfRadius <= 0.00001f)
    {
        return center;
    }

    const float2 texelSize =
        float2(gShadowTexelSizeX, gShadowTexelSizeY) * gShadowPcfRadius;

    float sum = 0.0f;
    sum += HikariSampleShadowCompare(uv + texelSize * float2(-1.0f, -1.0f), currentDepth, cascadeIndex);
    sum += HikariSampleShadowCompare(uv + texelSize * float2( 0.0f, -1.0f), currentDepth, cascadeIndex);
    sum += HikariSampleShadowCompare(uv + texelSize * float2( 1.0f, -1.0f), currentDepth, cascadeIndex);
    sum += HikariSampleShadowCompare(uv + texelSize * float2(-1.0f,  0.0f), currentDepth, cascadeIndex);
    sum += center;
    sum += HikariSampleShadowCompare(uv + texelSize * float2( 1.0f,  0.0f), currentDepth, cascadeIndex);
    sum += HikariSampleShadowCompare(uv + texelSize * float2(-1.0f,  1.0f), currentDepth, cascadeIndex);
    sum += HikariSampleShadowCompare(uv + texelSize * float2( 0.0f,  1.0f), currentDepth, cascadeIndex);
    sum += HikariSampleShadowCompare(uv + texelSize * float2( 1.0f,  1.0f), currentDepth, cascadeIndex);
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

uint HikariSelectShadowCascade(float viewDistance)
{
    const uint cascadeCount =
        clamp((uint)round(gShadowCascadeParams.x), 1u, 4u);
    uint cascadeIndex = 0u;
    [unroll]
    for (uint index = 0u; index < 3u; ++index)
    {
        if (index + 1u < cascadeCount &&
            viewDistance > gShadowCascadeSplits[index])
        {
            cascadeIndex = index + 1u;
        }
    }
    return cascadeIndex;
}

float2 HikariShadowAtlasUv(float2 cascadeUv, uint cascadeIndex)
{
    return
        cascadeUv * gShadowCascadeParams.zw +
        HikariShadowAtlasTileOrigin(cascadeIndex);
}

float3 HikariApplyBoundedShadowNormalBias(
    float3 worldPosWS,
    float3 geometricNormalWS,
    uint cascadeIndex)
{
    const float3 requestedOffset =
        geometricNormalWS * max(gShadowNormalBias, 0.0f);
    const float3 projectedOffset =
        mul(
            gShadowLightViewProj[cascadeIndex],
            float4(requestedOffset, 0.0f)).xyz;

    // Authoring remains in world units, but the effective offset is bounded in
    // the selected cascade. This prevents a large acne fix from detaching a
    // near-cascade shadow by many texels.
    const float maximumProjectedOffset =
        max(gShadowTexelSizeX, gShadowTexelSizeY) * 8.0f;
    const float projectedXY =
        max(abs(projectedOffset.x), abs(projectedOffset.y));
    const float xyScale =
        projectedXY > maximumProjectedOffset
            ? maximumProjectedOffset / projectedXY
            : 1.0f;
    const float maximumDepthOffset =
        max(gShadowDepthBias * 4.0f, 0.002f);
    const float projectedZ = abs(projectedOffset.z);
    const float depthScale =
        projectedZ > maximumDepthOffset
            ? maximumDepthOffset / projectedZ
            : 1.0f;
    return worldPosWS + requestedOffset * min(xyScale, depthScale);
}

float HikariSampleDirectionalShadowCascade(
    float3 worldPosWS,
    float3 geometricNormalWS,
    uint cascadeIndex,
    out bool cascadeValid)
{
    const float3 biasedWorldPos =
        HikariApplyBoundedShadowNormalBias(
            worldPosWS,
            geometricNormalWS,
            cascadeIndex);
    const float4 lightClip =
        mul(gShadowLightViewProj[cascadeIndex], float4(biasedWorldPos, 1.0f));
    if (abs(lightClip.w) < 1e-5f)
    {
        cascadeValid = false;
        return 1.0f;
    }

    const float3 proj = lightClip.xyz / lightClip.w;
    const float2 cascadeUv =
        float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);
    if (cascadeUv.x < 0.0f || cascadeUv.x > 1.0f ||
        cascadeUv.y < 0.0f || cascadeUv.y > 1.0f ||
        proj.z < 0.0f || proj.z > 1.0f)
    {
        cascadeValid = false;
        return 1.0f;
    }

    cascadeValid = true;
    const float2 atlasUv =
        HikariShadowAtlasUv(cascadeUv, cascadeIndex);
    const float currentDepth = proj.z - gShadowDepthBias;
    const float sampledVisibility =
        HikariSampleShadowPcf(
            atlasUv,
            currentDepth,
            cascadeIndex);
    const float shadowFactor =
        lerp(1.0f - gShadowStrength, 1.0f, sampledVisibility);
    return lerp(
        1.0f,
        shadowFactor,
        HikariShadowReceiverFade(cascadeUv));
}

float SampleDirectionalShadow(float3 worldPosWS, float3 geometricNormalWS, uint receiveShadow)
{
    if (gShadowEnabled == 0 || receiveShadow == 0)
    {
        return 1.0f;
    }

    // CPU cascade fitting uses axial camera depth. Clip W preserves that same
    // contract even for receivers near the edges of a wide view.
    const float viewDistance =
        max(
            mul(gViewProj, float4(worldPosWS, 1.0f)).w,
            0.0f);
    const uint cascadeIndex = HikariSelectShadowCascade(viewDistance);
    const uint cascadeCount =
        clamp((uint)round(gShadowCascadeParams.x), 1u, 4u);
    bool cascadeValid = false;
    float visibility =
        HikariSampleDirectionalShadowCascade(
            worldPosWS,
            geometricNormalWS,
            cascadeIndex,
            cascadeValid);
    if (!cascadeValid && cascadeIndex + 1u < cascadeCount)
    {
        visibility =
            HikariSampleDirectionalShadowCascade(
                worldPosWS,
                geometricNormalWS,
                cascadeIndex + 1u,
                cascadeValid);
    }

    const float cascadeNear =
        cascadeIndex == 0u
            ? 0.0f
            : gShadowCascadeSplits[cascadeIndex - 1u];
    const float cascadeFar = gShadowCascadeSplits[cascadeIndex];
    const float blendWidth =
        max((cascadeFar - cascadeNear) * gShadowCascadeParams.y, 0.0001f);
    if (cascadeIndex + 1u < cascadeCount &&
        viewDistance > cascadeFar - blendWidth)
    {
        bool nextCascadeValid = false;
        const float nextVisibility =
            HikariSampleDirectionalShadowCascade(
                worldPosWS,
                geometricNormalWS,
                cascadeIndex + 1u,
                nextCascadeValid);
        if (nextCascadeValid)
        {
            const float blend =
                saturate(
                    (viewDistance - (cascadeFar - blendWidth)) /
                    blendWidth);
            visibility = lerp(visibility, nextVisibility, blend);
        }
    }
    return visibility;
}

#endif
