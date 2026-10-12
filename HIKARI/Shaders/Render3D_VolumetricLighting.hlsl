#include "Include/Contracts/HIKARI_ShaderResourceBindings.hlsli"
#include "Include/Lighting/HIKARI_LocalLightContract.hlsli"
#include "Include/Lighting/HIKARI_ShadowCascadeContract.hlsli"
#include "Include/Lighting/HIKARI_VolumetricCamera.hlsli"
#include "Include/Lighting/HIKARI_VolumetricScattering.hlsli"
#include "Include/Lighting/HIKARI_LocalFogDensity.hlsli"
#include "Include/Lighting/HIKARI_IrradianceSh9.hlsli"

// 体積霧は五段で作る。GatherAmbientCS が粗い格子で媒質へ届く環境光（動的 GI の probe）を集め、InjectCS が froxel ごとに
// 標本位置を毎フレーム動かして散乱と消散を求め、TemporalCS が前フレームの体積を再投影して時間方向に
// 混ぜ、IntegrateCS が視線に沿って積分し、CompositeCS が場面へ合成する。注入の k 番目の層は奥行き
// [d(k-1), d(k)] を受け持ち、積分の k 番目は near..d(k) を持つ。
cbuffer VolumetricCB : register(b0)
{
    float4x4 gInvViewProj;
    float4x4 gPrevViewProj;
    float4x4 gLightViewProj[4];
    float4 gShadowCascadeSplits;
    float4 gShadowCascadeParams;
    float4 gShadowSampling; // depth bias, atlas texel size xy, reserved
    float4 gShadowCascadeDepthBiasScales;
    float4 gCameraPos;
    float4 gPrevCameraPos;
    float4 gFogColorDensity;
    float4 gFogDistances;
    float4 gDirectionalDirIntensity;
    float4 gDirectionalColorShadow;
    float4 gAmbientColorIntensity;
    float4 gVolumetricOptics; // base height, scatter gain, ambient gain, solar shaft gain
    float4 gVolumeSize;
    float4 gFrameParams; // x history weight, y P/P_prev, z shadowed, w pre-exposure
    float gVolumetricDebugMode;
    float gLocalLightDepthScale;
    float gLocalLightDepthBias;
    float gLocalFogVolumeCount;
    float4 gAerialPerspective; // x max distance, y aerial perspective enabled, z fog volume enabled
    float4 gFogJitter; // xy froxel 内の横の標本位置 [-0.5, 0.5]、z 層内の奥行きの標本位置 [0, 1)、w 1 なら環境光は GI の体積
    float4 gAmbientVolumeSize; // xyz 環境光の体積の大きさ
    float4 gFogNoise; // x 高さ霧の密度の揺らぎの強さ、y 1/大きさ [1/m]
    float4 gFogNoiseOffset; // xy 粗いノイズ、zw 細かいノイズの風の移動（ノイズ単位）
    HikariIrradianceVolumeData gFogGiVolumes[HIKARI_DYNAMIC_GI_VOLUME_COUNT];
    uint gLocalLightCount;
    uint gLocalLightClusterCountX;
    uint gLocalLightClusterCountY;
    uint gLocalLightClusterCountZ;
    uint gLocalLightTileSizePixels;
    uint gLocalLightClusterIndexCount;
    float gLocalLightNearDistance;
    float gLocalLightFarDistance;
    HikariLocalShadowData gLocalShadow;
    HikariLocalFogVolume gLocalFogVolumes[32];
};

#include "Include/Lighting/HIKARI_LocalLightList.hlsli"

Texture2D<float> gSceneDepth : register(t0);
Texture2D<float> gShadowMap : register(t1);
// 前フレームの時間混合済みの注入（Pre-Exposure 済みの散乱係数 x 入射光、消散係数）。TemporalCS が読む。
Texture3D<float4> gHistoryVolume : register(t2);
// 媒質へ届く環境光の全方向平均 radiance（GatherAmbientCS）。
Texture3D<float4> gAmbientVolume : register(t3);
Texture2D gLocalShadowAtlas : register(t7);
// 今フレームの注入（TemporalCS は生の標本、IntegrateCS は時間混合済みを読む）。
Texture3D<float4> gInjectedVolume : register(t10);
static const uint HIKARI_MATERIAL_TEXTURE_POOL_COUNT = HIKARI_SHADER_USER_SRV_COUNT;
Texture2D gMaterialTexturePool[HIKARI_MATERIAL_TEXTURE_POOL_COUNT] : register(t20);
RWTexture3D<float4> gOutputVolume : register(u0);
SamplerState gLinearClamp : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

#include "Include/Lighting/HIKARI_DynamicGiSampling.hlsli"
#include "Include/Lighting/HIKARI_LocalLightTextures.hlsli"

float DistanceToSlice(float distanceToCamera)
{
    return HikariVolumeDistanceToSlice(distanceToCamera, gFogDistances.x, gFogDistances.y, gVolumeSize.z);
}

// 高さ霧の密度の倍率。二つの周期ノイズ（細かい方は速く流れる）を重ね、平均がほぼ 1 になるよう 0..2 へ広げる。
static const int kFogNoisePeriod = 256;
static const float kFogNoiseDetailFrequency = 2.37f;
float FogNoiseDensityScale(float3 worldPosition)
{
    if (gFogNoise.x <= 0.0f)
        return 1.0f;
    const float3 position = worldPosition * gFogNoise.y;
    const int3 period = int3(kFogNoisePeriod, kFogNoisePeriod, kFogNoisePeriod);
    const float coarse = HikariNoisePeriodicValue3D(
        position + float3(gFogNoiseOffset.x, 0.0f, gFogNoiseOffset.y), period, 0x6a09e667u);
    const float detail = HikariNoisePeriodicValue3D(
        position * kFogNoiseDetailFrequency + float3(gFogNoiseOffset.z, 0.0f, gFogNoiseOffset.w), period, 0xbb67ae85u);
    const float noise = coarse * 0.65f + detail * 0.35f;
    // 二つの重なりは 0.5 の周りに集まるので、狭い幅で 0..1 へ広げて霧の塊と切れ間を出す（視線の積分で平均されて薄まる）。
    return lerp(1.0f, 2.0f * smoothstep(0.3f, 0.7f, noise), gFogNoise.x);
}

// 積分の k 番目の格納点の距離（fraction = k / (層数 - 1)）。
float FractionDistance(float fraction)
{
    return HikariVolumeFractionDistance(fraction, gFogDistances.x, gFogDistances.y);
}

float SampleVolumeShadowCascade(float3 worldPosition, uint cascadeIndex, out bool valid)
{
    const float4 clip = mul(gLightViewProj[cascadeIndex], float4(worldPosition, 1.0f));
    const float3 ndc = clip.xyz / max(abs(clip.w), 1e-6f);
    const float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    valid = all(uv >= 0.0f) && all(uv <= 1.0f) && ndc.z >= 0.0f && ndc.z <= 1.0f;
    if (!valid) return 1.0f;
    const float2 atlasUv = uv * gShadowCascadeParams.zw + HikariShadowAtlasTileOrigin(cascadeIndex);
    return gShadowMap.SampleCmpLevelZero(gShadowSampler,
        HikariClampShadowAtlasTileUv(atlasUv, cascadeIndex, gShadowCascadeParams.zw, gShadowSampling.yz),
        ndc.z - gShadowSampling.x * gShadowCascadeDepthBiasScales[cascadeIndex]);
}

float ShadowVisibility(float3 worldPosition, float3 cameraForward)
{
    if (gFrameParams.z < 0.5f) return 1.0f;
    const float viewDepth = HikariVolumeViewDepth(worldPosition, gCameraPos.xyz, cameraForward);
    const uint cascadeCount = clamp((uint)round(gShadowCascadeParams.x), 1u, 4u);
    const uint cascade = HikariSelectShadowCascade(viewDepth, gShadowCascadeSplits, cascadeCount);
    bool valid;
    float visibility = SampleVolumeShadowCascade(worldPosition, cascade, valid);
    const float blend = HikariShadowCascadeBlendWeight(
        viewDepth, gShadowCascadeSplits, cascade, gShadowCascadeParams.y);
    if (cascade + 1u < cascadeCount && (!valid || blend > 0.0f))
    {
        bool nextValid;
        const float nextVisibility = SampleVolumeShadowCascade(worldPosition, cascade + 1u, nextValid);
        if (nextValid) visibility = valid ? lerp(visibility, nextVisibility, blend) : nextVisibility;
    }
    return visibility;
}

// 媒質へ届く環境光。動的 GI があれば GatherAmbientCS の体積（覆わない所は空の光）、無ければ一様な空の光。
float3 AmbientRadiance(float2 uv, float distanceToCamera)
{
    if (gFogJitter.w < 0.5f)
        return gAmbientColorIntensity.rgb * gAmbientColorIntensity.a;
    const float slice =
        HikariVolumeDistanceToSlice(distanceToCamera, gFogDistances.x, gFogDistances.y, gAmbientVolumeSize.z);
    return gAmbientVolume.SampleLevel(gLinearClamp, float3(uv, slice), 0.0f).rgb;
}

// 標本の照明。日光と環境光は lightingPosition で、局所光は視線上の区間 segment（相機からの距離）の中で評価する。
// 局所光の奥行きの位置は灯りからの距離の二乗の逆数に比例する密度で選んで密度で割る（equiangular sampling、
// Kulla & Fajardo 2012）。区間の中で一様に選ぶと、灯りのすぐ近くを通る区間（聚光灯の根元）で標本ごとの明るさが
// 桁で揺れ、時間方向に積んでも粒が残った。錐・影・位相・投影模様は選んだ点で評価する。
float3 EvaluateLighting(
    float3 lightingPosition,
    float3 rayOrigin,
    float3 ray,
    float2 segment,
    float segmentFraction,
    float3 cameraForward,
    float2 pixelPosition,
    uint localLightDepthSlice,
    float3 directionalRadiance,
    float3 ambientRadiance)
{
    float3 lighting = ambientRadiance * max(gVolumetricOptics.z, 0.0f);
    lighting += directionalRadiance *
        max(gVolumetricOptics.w, 0.0f) *
        ShadowVisibility(lightingPosition, cameraForward);

    const uint2 localLightCluster =
        HikariResolveLocalLightClusterFromDepthSlice(
        pixelPosition,
        localLightDepthSlice);
    const float segmentLength = segment.y - segment.x;
    [loop]
    for (uint clusterSlot = 0u;
         clusterSlot < localLightCluster.y;
         ++clusterSlot)
    {
        const uint compactIndex =
            localLightCluster.x + clusterSlot;
        if (compactIndex >= gLocalLightClusterIndexCount)
        {
            break;
        }
        const uint lightIndex =
            gHikariLocalLightClusterIndices[compactIndex];
        if (lightIndex >= gLocalLightCount)
        {
            continue;
        }
        const HikariLocalLightGpuData light = gHikariLocalLights[lightIndex];
        // 灯りへの最接近点 closest と視線からの距離 spread（発光体の半径より近づけない）。区間の両端の角度の間を
        // 一様に選ぶと、位置の密度は 1/((t-closest)^2 + spread^2) に比例する。
        const float3 toLight = light.positionRange.xyz - rayOrigin;
        const float closest = dot(toLight, ray);
        const float spread = max(sqrt(max(dot(toLight, toLight) - closest * closest, 0.0f)),
            max(light.sourceRadius, 1.0e-3f));
        const float angleStart = atan((segment.x - closest) / spread);
        const float angleEnd = atan((segment.y - closest) / spread);
        float distance = segment.x + segmentLength * segmentFraction;
        float weight = 1.0f;
        if (segmentLength > 1.0e-4f && angleEnd - angleStart > 1.0e-6f)
        {
            distance = clamp(closest + spread * tan(lerp(angleStart, angleEnd, segmentFraction)), segment.x, segment.y);
            const float offset = distance - closest;
            weight = (offset * offset + spread * spread) * (angleEnd - angleStart) / (spread * segmentLength);
        }
        const float3 position = rayOrigin + ray * distance;
        HikariLocalLightSample lightSample;
        if (!HikariResolveLocalLightSample(light, position, lightSample))
        {
            continue;
        }
        HikariApplyLocalLightTextures(light, lightSample);
        const float pointPhase = HikariHenyeyGreenstein(
            dot(ray, lightSample.directionToLight),
            gFogDistances.w);
        // 単一 compare は標本ごとに 0/1 で、聚光灯の影の縁の霧が粒になる。描画と同じ 3x3 の bilinear compare で読む。
        const float visibility = HikariSampleLocalLightShadow(gLocalShadowAtlas, gShadowSampler,
            gLocalShadow, light, position, 0.0f.xxx, true);
        lighting += lightSample.radiance * pointPhase * visibility * weight;
    }
    return lighting;
}

// 環境光の粗い格子（froxel の横 8x8、奥行き 2 層ごと）。格納点は積分と同じ奥行きの分布で near..far に並ぶ。
[numthreads(8, 8, 1)]
void GatherAmbientCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint3 volumeSize = uint3(gAmbientVolumeSize.xyz + 0.5f);
    if (any(dispatchThreadId >= volumeSize))
    {
        return;
    }
    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(volumeSize.xy);
    float3 rayOrigin;
    float3 ray;
    bool orthographic;
    HikariReconstructVolumeRay(uv, gInvViewProj, gCameraPos.xyz, rayOrigin, ray, orthographic);
    // 格子点は格子の中心の光線で最初に当たる不透明面の手前へ引き寄せる。格子は粗く（奥行きで 1m 前後、横に 60 画素
    // ほど）、面の奥の点を使うと壁や屋根の向こう（外の空の光、灯りのついた部屋の照り返し）の環境光が補間で霧へ混ざる。
    // 輪郭の格子で最も遠い面を使うと、中心の光線が屋根に当たっていても部屋の中の点を拾い、屋根の上の霧が赤く光った。
    uint sceneWidth = 0u;
    uint sceneHeight = 0u;
    gSceneDepth.GetDimensions(sceneWidth, sceneHeight);
    const float3 cameraForward = HikariVolumeCameraForward(gInvViewProj, gCameraPos.xyz, orthographic);
    const int2 pixel = int2(uv * float2(sceneWidth - 1u, sceneHeight - 1u));
    const float depth = gSceneDepth.Load(int3(pixel, 0));
    const float surfaceDistance = depth < 0.999999f
        ? HikariVolumeCameraDistance(HikariUnprojectVolumePosition(uv, depth, gInvViewProj), gCameraPos.xyz,
              cameraForward, orthographic)
        : gFogDistances.y;
    const float cellDistance = FractionDistance((float)dispatchThreadId.z / max((float)volumeSize.z - 1.0f, 1.0f));
    const float sampleDistance = max(min(cellDistance, surfaceDistance - 0.3f), gFogDistances.x);
    const float3 worldPosition = rayOrigin + ray * sampleDistance;
    HikariDynamicGiQuery query = (HikariDynamicGiQuery)0;
    query.position = worldPosition;
    query.medium = true;
    const HikariDynamicGiSample sample = HikariQueryDynamicGi(gFogGiVolumes, query);
    const float3 radiance = sample.value + gAmbientColorIntensity.rgb * gAmbientColorIntensity.a * (1.0f - sample.coverage);
    gOutputVolume[dispatchThreadId] = float4(min(radiance, 60000.0f.xxx), 1.0f);
}

// 注入標本の密度と照明を同じ位置で評価する。
float4 SampleFogInjection(uint3 cell, float3 fraction, uint2 sceneSize)
{
    const float2 uv = (float2(cell.xy) + fraction.xy) / gVolumeSize.xy;
    float3 rayOrigin;
    float3 ray;
    bool orthographic;
    HikariReconstructVolumeRay(uv, gInvViewProj, gCameraPos.xyz, rayOrigin, ray, orthographic);
    const float3 cameraForward = HikariVolumeCameraForward(gInvViewProj, gCameraPos.xyz, orthographic);
    const float sliceScale = 1.0f / max(gVolumeSize.z - 1.0f, 1.0f);
    const float sampleDistance = FractionDistance(((float)cell.z - 1.0f + fraction.z) * sliceScale);
    const float3 worldPosition = rayOrigin + ray * sampleDistance;
    float lightingEnd = FractionDistance((float)cell.z * sliceScale);
    // 照明は標本の画素で見えている範囲（不透明面の手前）で評価し、面より奥の点は面のすぐ手前で照らす。面の奥（日なたの
    // 外や灯りのついた部屋の中）の標本は二つの経路で壁のこちら側へ漏れた。合成は面の深さで層の間を補間して面をまたぐ層の
    // 散乱を一部取り込むので、層の境が壁を横切る所に相機を中心とした輪の光が出た。相機が動くと履歴の再投影（奥行きと横の
    // 線形補間）が奥の froxel を壁の外や屋根の上の froxel へ混ぜ、履歴の重みで残って縞の光が出た。密度は元の位置のまま
    // （媒質の量は変えない）。
    float3 lightingPosition = worldPosition;
    const float surfaceDepth = gSceneDepth.Load(
        int3(min(uint2(uv * float2(sceneSize)), sceneSize - 1u), 0));
    if (surfaceDepth < 0.999999f)
    {
        const float surfaceDistance = HikariVolumeCameraDistance(
            HikariUnprojectVolumePosition(uv, surfaceDepth, gInvViewProj), gCameraPos.xyz, cameraForward, orthographic);
        const float lightingLimit = max(surfaceDistance - 0.05f, 0.0f);
        lightingPosition = rayOrigin + ray * min(sampleDistance, lightingLimit);
        lightingEnd = min(lightingEnd, lightingLimit);
    }
    const float2 lightingSegment =
        float2(min(FractionDistance(((float)cell.z - 1.0f) * sliceScale), lightingEnd), lightingEnd);
    const float heightAboveBase = max(worldPosition.y - gVolumetricOptics.x, 0.0f);
    float extinction = max(0.0f, gFogColorDensity.a) * exp(-heightAboveBase * gFogDistances.z) *
        FogNoiseDensityScale(worldPosition);
    float3 scatteringCoefficient = extinction * gFogColorDensity.rgb * max(gVolumetricOptics.y, 0.0f);
    [loop]
    for (uint volumeIndex = 0; volumeIndex < (uint)gLocalFogVolumeCount; ++volumeIndex)
    {
        const HikariLocalFogVolume volume = gLocalFogVolumes[volumeIndex];
        const float localExtinction = HikariLocalFogExtinction(volume, worldPosition);
        extinction += localExtinction;
        scatteringCoefficient += localExtinction * volume.colorDensity.rgb * volume.noiseEdgeScattering.w;
    }
    float3 inScattering = 0.0f.xxx;
    [branch] if (any(scatteringCoefficient > 0.0f))
    {
        const float3 directionalRadiance = gDirectionalColorShadow.rgb * gDirectionalDirIntensity.w *
            HikariHenyeyGreenstein(dot(ray, normalize(-gDirectionalDirIntensity.xyz)), gFogDistances.w);
        const uint localLightDepthSlice = HikariResolveLocalLightDepthSlice(
            HikariVolumeViewDepth(lightingPosition, gCameraPos.xyz, cameraForward));
        inScattering = scatteringCoefficient * EvaluateLighting(lightingPosition, rayOrigin, ray, lightingSegment,
            fraction.z, cameraForward, uv * float2(sceneSize), localLightDepthSlice, directionalRadiance,
            AmbientRadiance(uv, sampleDistance));
    }
    // 散乱はこのビューの HDR 空間で保持する（FP16 の体積に収め、合成で場面色と同じ空間になる）。
    return float4(inScattering * gFrameParams.w, extinction);
}

[numthreads(8, 8, 1)]
void InjectCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint3 volumeSize = uint3(gVolumeSize.xyz + 0.5f);
    if (any(dispatchThreadId >= volumeSize))
    {
        return;
    }
    const uint slice = dispatchThreadId.z;
    if (slice == 0u)
    {
        // 最初の格納点は near そのもの。受け持つ区間が無い。
        gOutputVolume[dispatchThreadId] = 0.0f.xxxx;
        return;
    }
    const bool useHistory = gVolumeSize.w > 0.5f && gFrameParams.x > 0.0f;
    uint sceneWidth = 0u;
    uint sceneHeight = 0u;
    gSceneDepth.GetDimensions(sceneWidth, sceneHeight);
    // 履歴がある時は体素ごとに毎フレーム 1 標本（UE5 の体积雾と同じ）。位置はフレームの R3 列に体素ごとの乱数を足して
    // 体素全域を偏りなく回り、TemporalCS が時間方向に積む。履歴の無いフレームは横 2x2・奥行き 4 区間の中心で
    // 4 標本を取り、最初の一枚から層を区切らない。
    if (useHistory)
    {
        const uint hash = HikariNoiseHashInt3(int3(dispatchThreadId), 0x5f3759dfu);
        const float3 phase = frac(float3(gFogJitter.xy + 0.5f, gFogJitter.z) + float3(
            HikariNoiseHashUnit(hash), HikariNoiseHashUnit(HikariNoiseMixUint(hash ^ 0x3c6ef372u)),
            HikariNoiseHashUnit(HikariNoiseMixUint(hash ^ 0xa54ff53au))));
        gOutputVolume[dispatchThreadId] = SampleFogInjection(dispatchThreadId, phase, uint2(sceneWidth, sceneHeight));
        return;
    }
    float4 injected = 0.0f.xxxx;
    [unroll] for (uint sampleIndex = 0u; sampleIndex < 4u; ++sampleIndex)
    {
        const float3 offset = float3(float(sampleIndex & 1u) * 0.5f, float(sampleIndex >> 1u) * 0.5f,
            float(sampleIndex) * 0.25f);
        injected += SampleFogInjection(dispatchThreadId, float3(0.25f, 0.25f, 0.125f) + offset,
            uint2(sceneWidth, sceneHeight));
    }
    gOutputVolume[dispatchThreadId] = injected * 0.25f;
}

// 前フレームの体積を froxel の中心（区間の奥行きの中点）で再投影して指数移動平均で混ぜる（UE5・Frostbite と同じ）。
// 今フレームの近傍へ挟んだり切り詰めたりはしない。標本が体素ごとに 1 個なので、光锥の根元や縁のように一部の標本しか
// 光に入らない体素では近傍 9 個がたまたま全部暗いことがあり（命中率 1/3 で 2.6%、1/10 で 39%）、履歴が一度で消えて
// 暗い斑点と縁の粒が残った。灯りの変化には設定の重みの速さで追いつく。
[numthreads(8, 8, 1)]
void TemporalCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint3 volumeSize = uint3(gVolumeSize.xyz + 0.5f);
    if (any(dispatchThreadId >= volumeSize))
    {
        return;
    }
    const uint slice = dispatchThreadId.z;
    const float4 injected = gInjectedVolume.Load(int4(dispatchThreadId, 0));
    gOutputVolume[dispatchThreadId] = injected;
    const bool useHistory = gVolumeSize.w > 0.5f && gFrameParams.x > 0.0f;
    if (slice == 0u || !useHistory)
    {
        return;
    }
    const float sliceScale = 1.0f / max((float)volumeSize.z - 1.0f, 1.0f);
    const float2 centerUv = (float2(dispatchThreadId.xy) + 0.5f) / float2(volumeSize.xy);
    float3 centerOrigin;
    float3 centerRay;
    bool centerOrthographic;
    HikariReconstructVolumeRay(centerUv, gInvViewProj, gCameraPos.xyz, centerOrigin, centerRay, centerOrthographic);
    const float3 center = centerOrigin + centerRay * FractionDistance(((float)slice - 0.5f) * sliceScale);
    const float4 previousClip = mul(gPrevViewProj, float4(center, 1.0f));
    const float3 previousNdc = previousClip.xyz / max(abs(previousClip.w), 1e-6f);
    const float2 previousUv = float2(previousNdc.x * 0.5f + 0.5f, 0.5f - previousNdc.y * 0.5f);
    const bool previousOrthographic = HikariIsOrthographic(gPrevViewProj);
    const float3 previousForward = previousOrthographic ? normalize(gPrevViewProj[2].xyz) : 0.0f.xxx;
    const float previousDistance = HikariVolumeCameraDistance(
        center, gPrevCameraPos.xyz, previousForward, previousOrthographic);
    // 注入の層 k の中心は積分の格納点 k-1/2。積分の写像に半層を足すと注入の texel 中心になる。
    const float previousSlice = DistanceToSlice(previousDistance) + 0.5f / gVolumeSize.z;
    if (!(previousClip.w > 0.0f && all(previousUv >= 0.0f) && all(previousUv <= 1.0f) &&
          previousNdc.z >= 0.0f && previousNdc.z <= 1.0f && previousDistance > gFogDistances.x &&
          previousDistance <= gFogDistances.y))
    {
        return;
    }
    float4 history = gHistoryVolume.SampleLevel(gLinearClamp, float3(previousUv, previousSlice), 0.0f);
    history.rgb *= gFrameParams.y;
    gOutputVolume[dispatchThreadId] = lerp(injected, history, gFrameParams.x);
}

[numthreads(8, 8, 1)]
void IntegrateCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint3 volumeSize = uint3(gVolumeSize.xyz + 0.5f);
    if (dispatchThreadId.x >= volumeSize.x || dispatchThreadId.y >= volumeSize.y)
    {
        return;
    }
    const float sliceScale = 1.0f / max((float)volumeSize.z - 1.0f, 1.0f);
    float3 integratedScattering = 0.0f.xxx;
    float integratedTransmittance = 1.0f;
    float previousDistance = FractionDistance(0.0f);
    gOutputVolume[uint3(dispatchThreadId.xy, 0u)] = float4(0.0f, 0.0f, 0.0f, 1.0f);
    [loop]
    for (uint slice = 1u; slice < volumeSize.z; ++slice)
    {
        const float distanceToCamera = FractionDistance((float)slice * sliceScale);
        const float stepLength = distanceToCamera - previousDistance;
        const float4 injected = gInjectedVolume.Load(int4(dispatchThreadId.xy, slice, 0));
        const float extinction = max(injected.a, 0.0f);
        // 全媒体の係数は注入で合成済み。区間内で一定として一度だけ積分する。
        integratedScattering += integratedTransmittance * max(injected.rgb, 0.0f.xxx) *
            HikariExtinctionIntegral(extinction.xxx, stepLength).x;
        integratedTransmittance *= exp(-extinction * stepLength);
        gOutputVolume[uint3(dispatchThreadId.xy, slice)] = float4(integratedScattering, integratedTransmittance);
        previousDistance = distanceToCamera;
    }
}

Texture2D<float> gCompositeDepth : register(t4);
Texture3D<float4> gIntegratedVolume : register(t5);
Texture2D<float> gCompositeWaterDepth : register(t6);
// 空気遠近の体積（Pre-Exposure 済みの内散乱光と平均透過率）。奥行きは d = max * w^2。
Texture3D<float4> gAerialPerspectiveVolume : register(t9);
RWTexture2D<float4> gSceneColorUav : register(u1);

float ResolveNearestCompositeDepth(float opaqueDepth, float waterDepth)
{
    // D32 visibility clears to 1.0.  The water visibility PS clamps a
    // far-plane water texel just below that value so it remains a valid
    // nearest receiver without ever treating an untouched texel as water.
    const bool opaqueValid =
        isfinite(opaqueDepth) && opaqueDepth >= 0.0f && opaqueDepth < 1.0f;
    const bool waterValid =
        isfinite(waterDepth) && waterDepth >= 0.0f && waterDepth < 1.0f;
    if (waterValid && (!opaqueValid || waterDepth < opaqueDepth))
    {
        return waterDepth;
    }
    return opaqueValid ? opaqueDepth : 1.0f;
}

float3 HueRamp(float value)
{
    const float3 phase = frac(value + float3(0.0f, 0.6667f, 0.3333f));
    return saturate(abs(phase * 6.0f - 3.0f) - 1.0f);
}

[numthreads(8, 8, 1)]
void CompositeCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint sceneWidth = 0;
    uint sceneHeight = 0;
    gSceneColorUav.GetDimensions(sceneWidth, sceneHeight);
    if (dispatchThreadId.x >= sceneWidth || dispatchThreadId.y >= sceneHeight)
    {
        return;
    }

    const uint2 pixel = dispatchThreadId.xy;
    const float2 uv = (float2(pixel) + 0.5f) / float2(sceneWidth, sceneHeight);
    const float depth = ResolveNearestCompositeDepth(
        gCompositeDepth.Load(int3(pixel, 0)),
        gCompositeWaterDepth.Load(int3(pixel, 0)));
    const float3 worldPosition = HikariUnprojectVolumePosition(uv, depth, gInvViewProj);
    const bool orthographic = HikariIsOrthographic(gInvViewProj);
    float distanceToCamera = gFogDistances.y;
    if (depth < 0.999999f)
    {
        distanceToCamera = orthographic
            ? HikariVolumeCameraDistance(
                  worldPosition,
                  gCameraPos.xyz,
                  HikariOrthographicForwardFromInverseViewProjection(gInvViewProj),
                  true)
            : length(worldPosition - gCameraPos.xyz);
    }
    else if (!orthographic)
    {
        // 地平線より下の空の画素は、霧を基準の高さの平面までで止める。その下は地面の代わりの背景で空気は無い
        // （地下の最も濃い霧が背景を覆わない）。地平線へ向けて距離は最大へ連続に戻る。
        const float3 ray = normalize(worldPosition - gCameraPos.xyz);
        const float height = gCameraPos.y - gVolumetricOptics.x;
        if (ray.y < 0.0f && height > 0.0f)
        {
            distanceToCamera = min(distanceToCamera, height / -ray.y);
        }
    }
    const float slice = DistanceToSlice(distanceToCamera);
    const float4 fog = gAerialPerspective.z < 0.5f || distanceToCamera <= gFogDistances.x
        ? float4(0.0f, 0.0f, 0.0f, 1.0f)
        : gIntegratedVolume.SampleLevel(gLinearClamp, float3(uv, slice), 0.0f);
    // 空気遠近は空ではなく不透明面と水面にだけ掛ける（空は大気の輝度そのもの）。
    float4 aerial = float4(0.0f, 0.0f, 0.0f, 1.0f);
    if (gAerialPerspective.y > 0.5f && depth < 0.999999f)
    {
        uint aerialWidth = 0u;
        uint aerialHeight = 0u;
        uint aerialSlices = 0u;
        gAerialPerspectiveVolume.GetDimensions(aerialWidth, aerialHeight, aerialSlices);
        const float w = sqrt(saturate(distanceToCamera / max(gAerialPerspective.x, 1.0f)));
        const float slices = max((float)aerialSlices, 1.0f);
        aerial = gAerialPerspectiveVolume.SampleLevel(
            gLinearClamp, float3(uv, max(w - 0.5f / slices, 0.5f / slices)), 0.0f);
        // 最初の層より手前はカメラ位置の恒等へ距離に比例して弱める（近距離の内散乱と減衰は距離に比例する）。層の座標 w は
        // 距離の平方根なので w に比例させると近くを過大に見積もり、閉じた部屋の中まで昼の空の青い霞が数倍濃く乗った。
        const float nearFraction = saturate(w * slices);
        aerial = lerp(float4(0.0f, 0.0f, 0.0f, 1.0f), aerial, nearFraction * nearFraction);
    }

    const uint debugMode =
        (uint)(gVolumetricDebugMode + 0.5f);
    float3 outputColor = 0.0f.xxx;
    if (debugMode == 1u)
    {
        outputColor = fog.rgb;
    }
    else if (debugMode == 2u)
    {
        outputColor = fog.aaa;
    }
    else if (debugMode == 3u)
    {
        const float quantizedSlice = min(
            floor(slice * gVolumeSize.z),
            gVolumeSize.z - 1.0f) /
            max(gVolumeSize.z - 1.0f, 1.0f);
        outputColor = HueRamp(quantizedSlice);
    }
    else
    {
        const float3 scene = gSceneColorUav[pixel].rgb;
        outputColor = (scene * aerial.a + aerial.rgb) * fog.a + fog.rgb;
    }
    gSceneColorUav[pixel] = float4(outputColor, 1.0f);
}
