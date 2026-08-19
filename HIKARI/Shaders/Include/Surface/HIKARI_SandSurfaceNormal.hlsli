#ifndef HIKARI_SAND_SURFACE_NORMAL_INCLUDED
#define HIKARI_SAND_SURFACE_NORMAL_INCLUDED

// Sand forward and SceneSurface/GeometryAux must sample the same atlas tile.
// Keep coverage uv1 separate from world-space texture density and keep the
// atlas gutter protected by the clamp sampler supplied by the caller.
float2 HikariSampleSandLayerNormalXY(
    HikariMeshMaterialData materialData,
    HikariSandLayerStyle style,
    float3 worldPosWS,
    SamplerState samplerState)
{
    if (materialData.hasNormalTexture == 0u)
    {
        return 0.0f.xx;
    }

    const float2 tiledUv = worldPosWS.xz *
        max(style.tilesPerMeter, 1.0e-4f);
    const float2 atlasUv =
        frac(tiledUv) * style.atlasScaleBias.xy +
        style.atlasScaleBias.zw;
    const float2 atlasDx = ddx(tiledUv) * style.atlasScaleBias.xy;
    const float2 atlasDy = ddy(tiledUv) * style.atlasScaleBias.xy;
    const float2 normalXY =
        HikariSampleMaterialTextureGrad(
            materialData.normalTextureDescriptorIndex,
            samplerState,
            atlasUv,
            atlasDx,
            atlasDy,
            float4(0.5f, 0.5f, 1.0f, 1.0f)).xy *
            2.0f - 1.0f;
    // Sand layer strength is the authoritative normal multiplier. Do not
    // apply the generic material UV transform or normalScale a second time.
    return normalXY * max(style.normalStrength, 0.0f);
}

float3 HikariResolveSandSurfaceAuxNormal(
    HikariMeshMaterialData materialData,
    HikariSandSurfaceParams p,
    float3 worldPosWS,
    float2 coverageUv,
    float3 normalWS,
    float4 tangentWS,
    SamplerState samplerState)
{
    const float3 fallbackNormal = normalize(normalWS);
    if (materialData.hasNormalTexture == 0u)
    {
        return fallbackNormal;
    }

    float2 blendedNormalXY = 0.0f.xx;
    float weightSum = 0.0f;
    const uint blendLayers = min(p.layerCount, 8u);
    for (uint layer = 0u; layer < blendLayers; ++layer)
    {
        const float weight = saturate(
            HikariSandSampleLayerWeight(p, layer, coverageUv));
        if (weight <= 1.0e-5f)
        {
            continue;
        }

        const HikariSandLayerStyle style =
            HikariSandLoadLayerStyle(p, layer);
        blendedNormalXY += HikariSampleSandLayerNormalXY(
            materialData,
            style,
            worldPosWS,
            samplerState) * weight;
        weightSum += weight;
    }

    if (weightSum <= 1.0e-5f)
    {
        // Match forward's flat-normal result when no authored layer weight is
        // available but a normal texture is still enabled.
        blendedNormalXY = 0.0f.xx;
        weightSum = 1.0f;
    }

    const float2 normalXY = blendedNormalXY / weightSum;
    const float normalZ = sqrt(saturate(
        1.0f - dot(normalXY, normalXY)));
    return HikariTransformTangentNormalToWorld(
        normalize(float3(normalXY, normalZ)),
        normalWS,
        tangentWS);
}

#endif
