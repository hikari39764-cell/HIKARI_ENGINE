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

#endif
