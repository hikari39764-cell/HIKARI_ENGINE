#include "Include/Lighting/HIKARI_LocalLightContract.hlsli"
#include "Include/Lighting/HIKARI_ShadowCascadeContract.hlsli"
#include "Include/Lighting/HIKARI_VolumetricCamera.hlsli"
#include "Include/Lighting/HIKARI_VolumetricScattering.hlsli"
#include "Include/Lighting/HIKARI_CloudSolarVisibility.hlsli"
#include "Include/Lighting/HIKARI_LocalFogDensity.hlsli"

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
    float4 gFrameParams;
    float gVolumetricDebugMode;
    float gLocalLightDepthScale;
    float gLocalLightDepthBias;
    float gLocalFogVolumeCount;
    uint gLocalLightCount;
    uint gLocalLightClusterCountX;
    uint gLocalLightClusterCountY;
    uint gLocalLightClusterCountZ;
    uint gLocalLightTileSizePixels;
    uint gLocalLightClusterIndexCount;
    float gLocalLightNearDistance;
    float gLocalLightFarDistance;
    HikariLocalShadowData gLocalShadow;
    HikariCloudSolarVisibilityData gCloudSolarVisibility;
    HikariLocalFogVolume gLocalFogVolumes[32];
};

#include "Include/Lighting/HIKARI_LocalLightList.hlsli"

Texture2D<float> gSceneDepth : register(t0);
Texture2D<float> gShadowMap : register(t1);
Texture2D gLocalShadowAtlas : register(t7);
Texture2D<float4> gCloudSolarAtlas : register(t8);
Texture3D<float4> gHistoryVolume : register(t2);
RWTexture3D<float4> gOutputVolume : register(u0);
SamplerState gLinearClamp : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

float DistanceToSlice(float distanceToCamera)
{
    return HikariVolumeDistanceToSlice(distanceToCamera, gFogDistances.x, gFogDistances.y, gVolumeSize.z);
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

float3 EvaluateLighting(
    float3 worldPosition,
    float3 viewRay,
    float3 cameraForward,
    float2 pixelPosition,
    uint localLightDepthSlice,
    float3 directionalRadiance)
{
    float3 lighting = gAmbientColorIntensity.rgb *
        gAmbientColorIntensity.a * max(gVolumetricOptics.z, 0.0f);
    lighting += directionalRadiance *
        max(gVolumetricOptics.w, 0.0f) *
        ShadowVisibility(worldPosition, cameraForward)*HikariSampleCloudSolarVisibility(gCloudSolarAtlas,gCloudSolarVisibility,worldPosition);

    const uint2 localLightCluster =
        HikariResolveLocalLightClusterFromDepthSlice(
        pixelPosition,
        localLightDepthSlice);
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
        HikariLocalLightSample lightSample;
        if (!HikariResolveLocalLightSample(
                gHikariLocalLights[lightIndex],
                worldPosition,
                lightSample))
        {
            continue;
        }
        const float pointPhase = HikariHenyeyGreenstein(
            dot(viewRay, lightSample.directionToLight),
            gFogDistances.w);
        const float visibility = HikariSampleLocalLightShadow(gLocalShadowAtlas, gShadowSampler,
            gLocalShadow, gHikariLocalLights[lightIndex], worldPosition, 0.0f.xxx, false);
        lighting += lightSample.radiance * pointPhase * visibility;
    }
    return lighting;
}

[numthreads(8, 8, 1)]
void InjectAndIntegrateCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint3 volumeSize = uint3(gVolumeSize.xyz + 0.5f);
    if (dispatchThreadId.x >= volumeSize.x || dispatchThreadId.y >= volumeSize.y)
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(volumeSize.xy);
    uint sceneWidth = 0u;
    uint sceneHeight = 0u;
    gSceneDepth.GetDimensions(sceneWidth, sceneHeight);
    const float2 pixelPosition =
        uv * float2(sceneWidth, sceneHeight);
    float3 rayOrigin;
    float3 ray;
    bool orthographic;
    HikariReconstructVolumeRay(uv, gInvViewProj, gCameraPos.xyz, rayOrigin, ray, orthographic);
    const float3 cameraForward = HikariVolumeCameraForward(gInvViewProj, gCameraPos.xyz, orthographic);
    const float3 toDirectionalLight =
        normalize(-gDirectionalDirIntensity.xyz);
    const float directionalPhase = HikariHenyeyGreenstein(
        dot(ray, toDirectionalLight),
        gFogDistances.w);
    const float3 directionalRadiance =
        gDirectionalColorShadow.rgb *
        gDirectionalDirIntensity.w *
        directionalPhase;
    float3 integratedScattering = 0.0f.xxx;
    float integratedTransmittance = 1.0f;
    const float nearDistance =
        max(0.01f, gFogDistances.x);
    const float farDistance =
        max(nearDistance + 0.01f, gFogDistances.y);
    // 光源 Cluster は放射状距離ではなく CPU と同じ視線軸の深度を使う。
    const float fogLogStep =
        log(farDistance / nearDistance) /
        max((float)volumeSize.z - 1.0f, 1.0f);
    const float sliceDistanceMultiplier =
        exp(fogLogStep);
    float distanceToCamera = nearDistance;
    float previousDistance = nearDistance;
    const bool useHistory = gVolumeSize.w > 0.5f && gFrameParams.x > 0.0f;
    const bool previousOrthographic = useHistory && HikariIsOrthographic(gPrevViewProj);
    const float3 previousForward = previousOrthographic
        ? normalize(gPrevViewProj[2].xyz)
        : 0.0f.xxx;

    [loop]
    for (uint slice = 0; slice < volumeSize.z; ++slice)
    {
        const float stepLength = max(0.0f, distanceToCamera - previousDistance);
        const float sampleDistance = (distanceToCamera + previousDistance) * 0.5f;
        const float3 worldPosition = rayOrigin + ray * sampleDistance;
        const float heightAboveBase = max(
            worldPosition.y - gVolumetricOptics.x,
            0.0f);
        const float heightDensity = exp(
            -heightAboveBase * gFogDistances.z);
        float extinction = max(0.0f, gFogColorDensity.a) * heightDensity;
        float3 scatteringCoefficient = extinction * gFogColorDensity.rgb * max(gVolumetricOptics.y, 0.0f);
        [loop]
        for (uint volumeIndex = 0; volumeIndex < (uint)gLocalFogVolumeCount; ++volumeIndex)
        {
            const HikariLocalFogVolume volume = gLocalFogVolumes[volumeIndex];
            const float localExtinction = HikariLocalFogExtinction(volume, worldPosition);
            extinction += localExtinction;
            scatteringCoefficient += localExtinction * volume.colorDensity.rgb * volume.noiseEdgeScattering.w;
        }
        const float stepTransmittance = exp(-extinction * stepLength);
        const uint localLightDepthSlice =
            HikariResolveLocalLightDepthSlice(
                HikariVolumeViewDepth(worldPosition, gCameraPos.xyz, cameraForward));
        const float3 lighting = EvaluateLighting(
            worldPosition,
            ray,
            cameraForward,
            pixelPosition,
            localLightDepthSlice,
            directionalRadiance);
        // 全媒体の係数を合成してから一度だけ積分する。重なりでも fog を二重合成しない。
        const float3 stepScattering = gLocalFogVolumeCount > 0.0f
            ? lighting * scatteringCoefficient * HikariExtinctionIntegral(extinction.xxx, stepLength)
            : lighting * gFogColorDensity.rgb * (1.0f - stepTransmittance) * max(gVolumetricOptics.y, 0.0f);
        integratedScattering += integratedTransmittance * stepScattering;
        integratedTransmittance *= stepTransmittance;

        float4 integrated = float4(integratedScattering, integratedTransmittance);
        if (useHistory)
        {
            const float3 endpoint = rayOrigin + ray * distanceToCamera;
            float4 previousClip = mul(gPrevViewProj, float4(endpoint, 1.0f));
            float3 previousNdc = previousClip.xyz / max(abs(previousClip.w), 1e-6f);
            float2 previousUv = float2(
                previousNdc.x * 0.5f + 0.5f,
                0.5f - previousNdc.y * 0.5f);
            const float previousSlice = DistanceToSlice(HikariVolumeCameraDistance(
                endpoint, gPrevCameraPos.xyz, previousForward, previousOrthographic));
            if (previousClip.w > 0.0f && all(previousUv >= 0.0f) && all(previousUv <= 1.0f) &&
                previousNdc.z >= 0.0f && previousNdc.z <= 1.0f)
            {
                const float4 history = gHistoryVolume.SampleLevel(
                    gLinearClamp, float3(previousUv, previousSlice), 0.0f);
                const float currentLuma = dot(integrated.rgb, float3(0.2126f, 0.7152f, 0.0722f));
                const float historyLuma = dot(history.rgb, float3(0.2126f, 0.7152f, 0.0722f));
                const float relativeLumaDelta = abs(historyLuma - currentLuma) /
                    max(max(historyLuma, currentLuma), 0.05f);
                const float transmittanceDelta = abs(history.a - integrated.a);
                // 遮蔽物が動いて暗くなった froxel へ以前の明るい散乱を持ち越さない。
                const bool discontinuity = relativeLumaDelta > 0.25f || transmittanceDelta > 0.05f ||
                    (currentLuma <= 1e-5f && historyLuma > 1e-5f);
                const float historyConfidence = discontinuity ? 0.0f : exp2(
                    -relativeLumaDelta * 1.5f - transmittanceDelta * 8.0f);
                integrated = lerp(
                    integrated,
                    history,
                    saturate(gFrameParams.x * historyConfidence));
            }
        }
        gOutputVolume[uint3(dispatchThreadId.xy, slice)] = integrated;
        previousDistance = distanceToCamera;
        distanceToCamera *= sliceDistanceMultiplier;
    }
}

Texture2D<float> gCompositeDepth : register(t4);
Texture3D<float4> gIntegratedVolume : register(t5);
Texture2D<float> gCompositeWaterDepth : register(t6);
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
    const float slice = DistanceToSlice(distanceToCamera);
    const float4 fog = distanceToCamera <= gFogDistances.x
        ? float4(0.0f, 0.0f, 0.0f, 1.0f)
        : gIntegratedVolume.SampleLevel(gLinearClamp, float3(uv, slice), 0.0f);

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
        outputColor = scene * fog.a + fog.rgb;
    }
    gSceneColorUav[pixel] = float4(outputColor, 1.0f);
}
