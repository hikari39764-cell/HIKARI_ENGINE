#ifndef HIKARI_SHADOW_CASCADE_CONTRACT_INCLUDED
#define HIKARI_SHADOW_CASCADE_CONTRACT_INCLUDED

// Surface と Volume が同じ Cascade 選択・Atlas 境界・遷移域を使う。
float2 HikariShadowAtlasTileOrigin(uint cascadeIndex)
{
    return float2((float)(cascadeIndex & 1u), (float)(cascadeIndex >> 1u)) * 0.5f;
}

float2 HikariClampShadowAtlasTileUv(
    float2 uv, uint cascadeIndex, float2 tileScale, float2 atlasTexelSize)
{
    const float2 origin = HikariShadowAtlasTileOrigin(cascadeIndex);
    return clamp(uv, origin + atlasTexelSize * 0.5f,
        origin + tileScale - atlasTexelSize * 0.5f);
}

uint HikariSelectShadowCascade(float viewDepth, float4 splits, uint cascadeCount)
{
    uint cascadeIndex = 0u;
    [unroll]
    for (uint index = 0u; index < 3u; ++index)
    {
        if (index + 1u < cascadeCount && viewDepth > splits[index])
            cascadeIndex = index + 1u;
    }
    return cascadeIndex;
}

float HikariShadowCascadeBlendWeight(
    float viewDepth, float4 splits, uint cascadeIndex, float blendFraction)
{
    if (blendFraction <= 0.0f) return 0.0f;
    const float nearDepth = cascadeIndex == 0u ? 0.0f : splits[cascadeIndex - 1u];
    const float farDepth = splits[cascadeIndex];
    const float width = max((farDepth - nearDepth) * blendFraction, 0.0001f);
    return saturate((viewDepth - (farDepth - width)) / width);
}

// 体積（霧・流体）が使う級联 1 枚の比較標本（PCF なし）。図集の範囲外は 1 で valid = false。
float HikariSampleVolumeShadowCascade(Texture2D<float> shadowMap, SamplerComparisonState shadowSampler,
    float4x4 lightViewProj, float3 worldPosition, uint cascadeIndex, float2 tileScale, float2 atlasTexelSize,
    float depthBias, out bool valid)
{
    const float4 clip = mul(lightViewProj, float4(worldPosition, 1.0f));
    const float3 ndc = clip.xyz / max(abs(clip.w), 1e-6f);
    const float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    valid = all(uv >= 0.0f) && all(uv <= 1.0f) && ndc.z >= 0.0f && ndc.z <= 1.0f;
    if (!valid) return 1.0f;
    const float2 atlasUv = uv * tileScale + HikariShadowAtlasTileOrigin(cascadeIndex);
    return shadowMap.SampleCmpLevelZero(shadowSampler,
        HikariClampShadowAtlasTileUv(atlasUv, cascadeIndex, tileScale, atlasTexelSize), ndc.z - depthBias);
}

#endif
