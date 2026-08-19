#ifndef HIKARI_SAND_SURFACE_SHADING_INCLUDED
#define HIKARI_SAND_SURFACE_SHADING_INCLUDED

#include "Include/Surface/HIKARI_SandSurfaceParams.hlsli"
#include "Include/Surface/HIKARI_SandSurfaceNormal.hlsli"

float3 HikariResolveTerrainLayerFlatBaseTint(
    HikariSandLayerStyle style)
{
    // Preserve the original pure-Sand anime base while letting every other
    // built-in layer use its authored light tint as the flat base.
    const float3 flatWarm =
        float3(0.97f, 0.88f, 0.68f);
    return style.materialKind == HIKARI_SAND_LAYER_MATERIAL_KIND_SAND
        ? flatWarm
        : saturate(style.lightTint);
}

// All built-in terrain layers use the same anime color treatment. Their flat
// base tint, texture density, and saturation stay authored per layer in
// HikariSandLayerStyle (with the legacy Sand base resolved above). Keeping the
// formula in one helper prevents color branches from drifting.
float3 HikariApplyTerrainLayerAnimeColor(
    float3 flatBaseTint,
    float3 textureSample,
    float textureStrength,
    float saturation)
{
    const float3 flatBase = saturate(flatBaseTint);
    const float3 source = saturate(textureSample);
    const float strength = max(textureStrength, 0.0f);
    float3 flatToTexture = lerp(
        flatBase,
        source,
        min(strength, 1.0f));
    if (strength > 1.0f)
    {
        // Preserve the existing >1 texture-strength contract as an anime
        // contrast control rather than allowing an unbounded lerp result.
        flatToTexture = saturate(
            (flatToTexture - flatBase) * strength + flatBase);
    }
    const float luminance = dot(
        flatToTexture,
        float3(0.299f, 0.587f, 0.114f));
    return saturate(lerp(
        luminance.xxx,
        flatToTexture,
        clamp(saturation, 0.0f, 2.0f)));
}

float4 HikariShadeSandSurface(
    PSInput input,
    HikariMeshObjectData objectData,
    float3 sandSurfaceData,
    bool costNoShadow)
{
    (void)objectData;
    HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(input.materialDataIndex);
    const HikariSurfaceGpuSceneInstance instance =
        HikariGetSurfaceGpuSceneInstanceAt(input.surfaceGpuSceneIndex);
    const HikariSandSurfaceParams p =
        HikariLoadSandSurfaceParamsFromDataIndex(
            instance.surfaceFeatureDataIndex);

    HikariClipSandCoverage(p, input.uv1);

    const float3 worldPos = input.worldPosWS;
    const float3 viewDir =
        normalize(gCameraPos.xyz - worldPos);
    const float3 lightDir = normalize(-gDirectionalDir.xyz);

    // coverage uv1 と texture density を分離する。各 layer は world XZ から
    // 自分の atlas rect を SampleGrad し、frac 境界の過大 derivative と
    // 隣接 tile bleed を避ける。
    float weightSum = 0.0f;
    float3 blendedLight = 0.0f.xxx;
    float3 blendedDark = 0.0f.xxx;
    float3 blendedTexture = 0.0f.xxx;
    float2 blendedNormalXY = 0.0f.xx;
    float roughness = 0.0f;
    float shadowStrength = 0.0f;
    float granularStrength = 0.0f;
    const uint blendLayers = min(p.layerCount, 8u);
    bool pureSandBaseline = blendLayers == 1u;
    for (uint layer = 0u; layer < blendLayers; ++layer)
    {
        const HikariSandLayerStyle style =
            HikariSandLoadLayerStyle(p, layer);
        if (layer == 0u &&
            style.materialKind != HIKARI_SAND_LAYER_MATERIAL_KIND_SAND)
        {
            pureSandBaseline = false;
        }
        const float w = saturate(
            HikariSandSampleLayerWeight(p, layer, input.uv1));
        if (w <= 1.0e-5f)
        {
            continue;
        }
        const float2 tiledUv = worldPos.xz *
            max(style.tilesPerMeter, 1.0e-4f);
        const float2 atlasUv =
            frac(tiledUv) * style.atlasScaleBias.xy +
            style.atlasScaleBias.zw;
        const float2 atlasDx = ddx(tiledUv) * style.atlasScaleBias.xy;
        const float2 atlasDy = ddy(tiledUv) * style.atlasScaleBias.xy;

        float3 layerTexture = 1.0f.xxx;
        if (materialData.hasBaseColorTexture != 0u)
        {
            const float3 source = saturate(
                HikariSampleMaterialTextureGrad(
                    materialData.baseColorTextureDescriptorIndex,
                    gLinearClamp,
                    atlasUv,
                    atlasDx,
                    atlasDy,
                    1.0f.xxxx).rgb);
            layerTexture = HikariApplyTerrainLayerAnimeColor(
                HikariResolveTerrainLayerFlatBaseTint(style),
                source,
                style.textureStrength,
                style.saturation);
        }
        if (materialData.hasNormalTexture != 0u)
        {
            blendedNormalXY += HikariSampleSandLayerNormalXY(
                materialData,
                style,
                worldPos,
                gLinearClamp) * w;
        }

        blendedLight += saturate(style.lightTint) * w;
        blendedDark += saturate(style.darkTint) * w;
        blendedTexture += layerTexture * w;
        roughness += saturate(style.roughness) * w;
        shadowStrength += saturate(style.shadowStrength) * w;
        granularStrength += saturate(style.granularStrength) * w;
        weightSum += w;
    }
    if (weightSum <= 1.0e-5f)
    {
        blendedLight = p.lightTint;
        blendedDark = p.darkTint;
        blendedTexture = 1.0f.xxx;
        roughness = 0.92f;
        shadowStrength = 1.0f;
        granularStrength = 1.0f;
        weightSum = 1.0f;
    }
    const float inverseWeight = rcp(weightSum);
    blendedLight *= inverseWeight;
    blendedDark *= inverseWeight;
    blendedTexture *= inverseWeight;
    roughness *= inverseWeight;
    shadowStrength *= inverseWeight;
    granularStrength *= inverseWeight;

    float4 albedo = materialData.baseColor;
    albedo.rgb *= blendedTexture;

    // sandSurfaceData.z は runtime response のみ。地形サイズではなく
    // authored heightScale を基準にして、大きな地形でも足跡の濃さを保つ。
    const float responseScale = max(p.heightScale, 0.05f);
    const float pitMaskRaw = saturate(
        -sandSurfaceData.z / responseScale);
    const float rimMaskRaw = saturate(
        sandSurfaceData.z / responseScale);
    const float pitMask = smoothstep(0.16f, 0.38f, pitMaskRaw) *
        granularStrength;
    const float rimMask = smoothstep(0.04f, 0.24f, rimMaskRaw) *
        granularStrength;
    albedo.rgb = lerp(
        albedo.rgb,
        albedo.rgb * blendedDark * 0.82f,
        pitMask * p.pitDarkening);
    // 押し出された正の rim は pit より弱く、乾いた砂の細い明部として出す。
    const float3 rimColor = saturate(
        albedo.rgb * 1.06f + blendedLight * 0.025f);
    albedo.rgb = lerp(
        albedo.rgb,
        rimColor,
        rimMask * 0.35f);
    // 水際の湿った砂。沈んだ領域を wetness に応じて暗く湿らせる。
    if (p.wetness > 0.0f)
    {
        albedo.rgb = lerp(
            albedo.rgb,
            albedo.rgb * float3(0.48f, 0.44f, 0.40f),
            saturate(pitMaskRaw) * p.wetness * granularStrength);
    }

    // 法線マップ（NormalDX）は共有の normal resolver でワールド空間へ
    // 変換する。VS が変形勾配から再構築した normalWS を基準にするため、
    // 凹みの傾斜とテクスチャの細粒が食い違わない。
    float3 shadingNormal = normalize(input.normalWS);
    if (materialData.hasNormalTexture != 0u)
    {
        const float2 normalXY = blendedNormalXY * inverseWeight;
        const float normalZ = sqrt(saturate(
            1.0f - dot(normalXY, normalXY)));
        shadingNormal = HikariTransformTangentNormalToWorld(
            normalize(float3(normalXY, normalZ)),
            input.normalWS,
            input.tangentWS);
    }
    // 風向きに沿った砂紋。ランダムノイズではなく、方向性のある
    // 波を法線へ加えることで、砂漠らしい細かい明暗を作る。
    const float2 windDir = normalize(float2(0.93f, 0.37f));
    const float ripplePhase =
        dot(worldPos.xz, windDir) * 2.4f +
        HikariSandValueNoise(worldPos.xz * 0.32f + 7.0f) * 1.7f;
    const float rippleAmplitude =
        p.rippleAmount * granularStrength * 0.12f *
        saturate(shadingNormal.y);
    shadingNormal = normalize(
        shadingNormal +
        float3(
            -windDir.x * sin(ripplePhase),
            0.0f,
            -windDir.y * sin(ripplePhase)) * rippleAmplitude);

    // Sand is a regular directional-shadow receiver. Reuse the shared CSM
    // sampler and the same per-surface receive flag as opaque PBR surfaces.
    // The cost-analysis NO_SHADOW variant must remain a true zero-sample path.
    const bool doubleSidedBackFace =
        (materialData.instanceFlags & HIKARI_PROGRAM_FLAG_DOUBLE_SIDED) != 0u &&
        !input.isFrontFace;
    const float3 surfaceGeometricNormal = normalize(input.normalWS);
    const float3 geometricNormal =
        doubleSidedBackFace
            ? -surfaceGeometricNormal
            : surfaceGeometricNormal;
    const float sampledShadowFactor = costNoShadow
        ? 1.0f
        : SampleDirectionalShadow(
            input.worldPosWS,
            geometricNormal,
            HikariForwardVertexMetaReceiveShadow(input.forwardMeta));
    const float shadowFactor = lerp(
        1.0f,
        sampledShadowFactor,
        saturate(shadowStrength));
    if (gDebugView == 8)
    {
        return float4(shadowFactor.xxx, albedo.a);
    }

    const float ndotl = saturate(dot(shadingNormal, lightDir));
    // アニメ調：ndotl を 2..4 段のセル分帯へ量子化し、帯の境界を
    // toonBandHardness で柔らかくする。平坦な帯色で塗り分ける。
    const float bandCount = max(p.toonBandCount, 2.0f);
    const float scaled = ndotl * bandCount;
    const float bandBase = floor(scaled);
    const float bandEdge = smoothstep(
        0.5f - p.toonBandHardness,
        0.5f + p.toonBandHardness,
        scaled - bandBase);
    const float ramp = saturate(
        (bandBase + bandEdge) / max(bandCount - 1.0f, 1.0f));
    const float3 diffuse = lerp(blendedDark, blendedLight, ramp);
    float3 color = albedo.rgb * diffuse * shadowFactor;

    // 海面ハイライトに似た広い光沢を加え、流れるような砂の質感を作る。
    const float3 halfDir = normalize(lightDir + viewDir);
    const float specularPower = pureSandBaseline
        ? 64.0f
        : lerp(64.0f, 20.0f, roughness);
    const float stylizedSpecular = pow(
        saturate(dot(shadingNormal, halfDir)), specularPower);
    const float specularStrength = pureSandBaseline
        ? 0.16f
        : lerp(0.16f, 0.025f, roughness);
    color += stylizedSpecular * specularStrength *
        blendedLight * shadowFactor;

    // 砂粒のきらめき。高周波 hash で細かい輝点を作り、硬さを避ける。
    const float grain = HikariSandHash21(
        floor(worldPos.xz * 160.0f));
    const float sparkle = pow(grain, 12.0f) *
        p.sparkleStrength * granularStrength * ndotl;
    color += sparkle * 1.8f * float3(1.0f, 0.96f, 0.82f) * shadowFactor;

    // 簡易フレネルで明るい空の照り返しを足し、単調な土色を避ける。
    const float fresnel =
        pow(1.0f - saturate(dot(shadingNormal, viewDir)), 3.0f);
    color += fresnel * 0.06f * float3(0.72f, 0.82f, 1.0f);

    return float4(max(color, 0.0f.xxx), albedo.a);
}

#endif
