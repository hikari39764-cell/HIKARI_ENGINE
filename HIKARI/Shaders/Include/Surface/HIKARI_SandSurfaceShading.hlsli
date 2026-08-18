#ifndef HIKARI_SAND_SURFACE_SHADING_INCLUDED
#define HIKARI_SAND_SURFACE_SHADING_INCLUDED

#include "Include/Surface/HIKARI_SandSurfaceParams.hlsli"

float4 HikariShadeSandSurface(
    PSInput input,
    HikariMeshObjectData objectData,
    float3 sandSurfaceData)
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

    float3 blendedLight = p.lightTint;
    float3 blendedDark = p.darkTint;
    if (p.layerCount > 1u)
    {
        // 任意層数を層 tint で混合。層 tint はペイントプリセット由来で、
        // 組み込み sand/snow/dirt は従来ハードコード色と同一値を持つため
        // 既定の見た目は変わらない。
        blendedLight = 0.0f.xxx;
        blendedDark = 0.0f.xxx;
        const uint blendLayers = min(p.layerCount, 8u);
        for (uint layer = 0u; layer < blendLayers; ++layer)
        {
            const float w = HikariSandSampleLayerWeight(p, layer, input.uv1);
            blendedLight += HikariSandSampleLayerLightTint(p, layer) * w;
            blendedDark += HikariSandSampleLayerDarkTint(p, layer) * w;
        }
    }

    const float3 worldPos = input.worldPosWS;
    const float3 viewDir =
        normalize(gCameraPos.xyz - worldPos);
    const float3 lightDir = normalize(-gDirectionalDir.xyz);

    // アルベドは通常マテリアルの baseColor / baseColorTexture を再利用する。
    // テクスチャが無い場合は procedural な砂色へ落とす。
    float4 albedo = materialData.baseColor;
    if (materialData.hasBaseColorTexture != 0)
    {
        const float2 baseColorUv = HikariResolveMaterialUv(
            materialData,
            HIKARI_MATERIAL_UV_BASE_COLOR,
            input.uv,
            input.uv1);
        const float2 sandUv =
            baseColorUv +
            (HikariSandValueNoise(worldPos.xz * 0.35f) - 0.5f) *
                0.08f * float2(1.0f, 0.75f);
        const float3 grain = saturate(
            HikariSampleMaterialTexture(
            materialData.baseColorTextureDescriptorIndex,
            gLinearWrap,
            sandUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f)).rgb);
        // アニメ調：写実の細粒を残しつつ、暖かい夏の砂色へ寄せて
        // コントラストを圧縮し、彩度をわずかに上げて平塗りに近づける。
        const float3 flatWarm = float3(0.97f, 0.88f, 0.68f);
        float3 stylized = lerp(flatWarm, grain, 0.52f);
        const float luminance = dot(
            stylized,
            float3(0.299f, 0.587f, 0.114f));
        stylized = lerp(
            float3(luminance, luminance, luminance),
            stylized,
            1.14f);
        albedo.rgb *= stylized;
    }
    else
    {
        albedo.rgb *= blendedLight;
    }

    // sandSurfaceData.z は runtime response のみ。地形サイズではなく
    // authored heightScale を基準にして、大きな地形でも足跡の濃さを保つ。
    const float responseScale = max(p.heightScale, 0.05f);
    const float pitMaskRaw = saturate(
        -sandSurfaceData.z / responseScale);
    const float rimMaskRaw = saturate(
        sandSurfaceData.z / responseScale);
    const float pitMask = smoothstep(0.16f, 0.38f, pitMaskRaw);
    const float rimMask = smoothstep(0.04f, 0.24f, rimMaskRaw);
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
            saturate(pitMaskRaw) * p.wetness);
    }

    // 法線マップ（NormalDX）は共有の normal resolver でワールド空間へ
    // 変換する。VS が変形勾配から再構築した normalWS を基準にするため、
    // 凹みの傾斜とテクスチャの細粒が食い違わない。
    float3 shadingNormal = HikariResolveSurfaceShadingNormal(
        materialData,
        input.normalWS,
        input.tangentWS,
        input.uv,
        input.uv1,
        gLinearWrap);
    // 風向きに沿った砂紋。ランダムノイズではなく、方向性のある
    // 波を法線へ加えることで、砂漠らしい細かい明暗を作る。
    const float2 windDir = normalize(float2(0.93f, 0.37f));
    const float ripplePhase =
        dot(worldPos.xz, windDir) * 2.4f +
        HikariSandValueNoise(worldPos.xz * 0.32f + 7.0f) * 1.7f;
    const float rippleAmplitude =
        p.rippleAmount * 0.12f * saturate(shadingNormal.y);
    shadingNormal = normalize(
        shadingNormal +
        float3(
            -windDir.x * sin(ripplePhase),
            0.0f,
            -windDir.y * sin(ripplePhase)) * rippleAmplitude);

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
    float3 color = albedo.rgb * diffuse;

    // 海面ハイライトに似た広い光沢を加え、流れるような砂の質感を作る。
    const float3 halfDir = normalize(lightDir + viewDir);
    const float oceanSpecular =
        pow(saturate(dot(shadingNormal, halfDir)), 64.0f);
    color += oceanSpecular * 0.16f * blendedLight;

    // 砂粒のきらめき。高周波 hash で細かい輝点を作り、硬さを避ける。
    const float grain = HikariSandHash21(
        floor(worldPos.xz * 160.0f));
    const float sparkle = pow(grain, 12.0f) *
        p.sparkleStrength * ndotl;
    color += sparkle * 1.8f * float3(1.0f, 0.96f, 0.82f);

    // 簡易フレネルで明るい空の照り返しを足し、単調な土色を避ける。
    const float fresnel =
        pow(1.0f - saturate(dot(shadingNormal, viewDir)), 3.0f);
    color += fresnel * 0.06f * float3(0.72f, 0.82f, 1.0f);

    return float4(max(color, 0.0f.xxx), albedo.a);
}

#endif
