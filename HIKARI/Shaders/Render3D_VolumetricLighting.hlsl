cbuffer VolumetricCB : register(b0)
{
    float4x4 gInvViewProj;
    float4x4 gPrevViewProj;
    float4x4 gLightViewProj;
    float4 gCameraPos;
    float4 gPrevCameraPos;
    float4 gFogColorDensity;
    float4 gFogDistances;
    float4 gDirectionalDirIntensity;
    float4 gDirectionalColorShadow;
    float4 gAmbientColorIntensity;
    float4 gVolumeSize;
    float4 gFrameParams;
    float4 gDebugParams;
    float4 gPointLightPosRange[8];
    float4 gPointLightColorIntensity[8];
};

Texture2D<float> gSceneDepth : register(t0);
Texture2D<float> gShadowMap : register(t1);
Texture3D<float4> gHistoryVolume : register(t2);
RWTexture3D<float4> gOutputVolume : register(u0);
SamplerState gLinearClamp : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

float SliceDistance(float slice)
{
    const float nearDistance = max(0.01f, gFogDistances.x);
    const float farDistance = max(nearDistance + 0.01f, gFogDistances.y);
    return nearDistance * pow(farDistance / nearDistance, saturate(slice));
}

float DistanceToSlice(float distanceToCamera)
{
    const float nearDistance = max(0.01f, gFogDistances.x);
    const float farDistance = max(nearDistance + 0.01f, gFogDistances.y);
    return saturate(log(max(distanceToCamera, nearDistance) / nearDistance) /
        log(farDistance / nearDistance));
}

float3 ReconstructRay(float2 uv)
{
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 1.0f, 1.0f);
    float4 world = mul(gInvViewProj, clip);
    world.xyz /= max(abs(world.w), 1e-6f);
    return normalize(world.xyz - gCameraPos.xyz);
}

float ShadowVisibility(float3 worldPosition)
{
    if (gFrameParams.z < 0.5f)
    {
        return 1.0f;
    }
    float4 lightClip = mul(gLightViewProj, float4(worldPosition, 1.0f));
    float3 lightNdc = lightClip.xyz / max(abs(lightClip.w), 1e-6f);
    float2 uv = float2(lightNdc.x * 0.5f + 0.5f, 0.5f - lightNdc.y * 0.5f);
    if (any(uv < 0.0f) || any(uv > 1.0f) || lightNdc.z <= 0.0f || lightNdc.z >= 1.0f)
    {
        return 1.0f;
    }
    return gShadowMap.SampleCmpLevelZero(gShadowSampler, uv, lightNdc.z - 0.0005f);
}

float HenyeyGreenstein(float cosTheta, float anisotropy)
{
    const float g = clamp(anisotropy, -0.8f, 0.8f);
    const float g2 = g * g;
    return (1.0f - g2) /
        max(4.0f * 3.14159265f * pow(1.0f + g2 - 2.0f * g * cosTheta, 1.5f), 1e-4f);
}

float3 EvaluateLighting(float3 worldPosition, float3 viewRay)
{
    const float3 toDirectionalLight = normalize(-gDirectionalDirIntensity.xyz);
    const float phase = HenyeyGreenstein(dot(viewRay, toDirectionalLight), gFogDistances.w);
    float3 lighting = gAmbientColorIntensity.rgb * gAmbientColorIntensity.a;
    lighting += gDirectionalColorShadow.rgb * gDirectionalDirIntensity.w *
        phase * lerp(1.0f, ShadowVisibility(worldPosition), gDirectionalColorShadow.a);

    const uint pointLightCount = min((uint)(gFrameParams.w + 0.5f), 8u);
    [loop]
    for (uint index = 0; index < pointLightCount; ++index)
    {
        float3 toLight = gPointLightPosRange[index].xyz - worldPosition;
        const float distanceToLight = length(toLight);
        const float range = max(0.001f, gPointLightPosRange[index].w);
        float attenuation = saturate(1.0f - distanceToLight / range);
        attenuation *= attenuation;
        const float pointPhase = HenyeyGreenstein(
            dot(viewRay, toLight / max(distanceToLight, 1e-4f)),
            gFogDistances.w);
        lighting += gPointLightColorIntensity[index].rgb *
            gPointLightColorIntensity[index].a * attenuation * pointPhase;
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
    const float3 ray = ReconstructRay(uv);
    float3 integratedScattering = 0.0f.xxx;
    float integratedTransmittance = 1.0f;
    float previousDistance = max(0.01f, gFogDistances.x);

    [loop]
    for (uint slice = 0; slice < volumeSize.z; ++slice)
    {
        const float sliceCoord = (float(slice) + 0.5f) / float(volumeSize.z);
        const float distanceToCamera = SliceDistance(sliceCoord);
        const float stepLength = max(0.001f, distanceToCamera - previousDistance);
        const float3 worldPosition = gCameraPos.xyz + ray * distanceToCamera;
        const float heightDensity = exp(-max(worldPosition.y, 0.0f) * gFogDistances.z);
        const float extinction = max(0.0f, gFogColorDensity.a) * heightDensity;
        const float stepTransmittance = exp(-extinction * stepLength);
        const float3 lighting = EvaluateLighting(worldPosition, ray);
        const float3 stepScattering = lighting * gFogColorDensity.rgb *
            (1.0f - stepTransmittance);
        integratedScattering += integratedTransmittance * stepScattering;
        integratedTransmittance *= stepTransmittance;

        float4 integrated = float4(integratedScattering, integratedTransmittance);
        if (gVolumeSize.w > 0.5f && gFrameParams.x > 0.0f)
        {
            float4 previousClip = mul(gPrevViewProj, float4(worldPosition, 1.0f));
            float3 previousNdc = previousClip.xyz / max(abs(previousClip.w), 1e-6f);
            float2 previousUv = float2(
                previousNdc.x * 0.5f + 0.5f,
                0.5f - previousNdc.y * 0.5f);
            const float previousSlice = DistanceToSlice(
                length(worldPosition - gPrevCameraPos.xyz));
            if (all(previousUv >= 0.0f) && all(previousUv <= 1.0f) &&
                previousNdc.z >= 0.0f && previousNdc.z <= 1.0f)
            {
                const float4 history = gHistoryVolume.SampleLevel(
                    gLinearClamp, float3(previousUv, previousSlice), 0.0f);
                const float currentLuma = dot(integrated.rgb, float3(0.2126f, 0.7152f, 0.0722f));
                const float historyLuma = dot(history.rgb, float3(0.2126f, 0.7152f, 0.0722f));
                const float relativeLumaDelta = abs(historyLuma - currentLuma) /
                    max(max(historyLuma, currentLuma), 0.05f);
                const float transmittanceDelta = abs(history.a - integrated.a);
                const float historyConfidence = exp2(
                    -relativeLumaDelta * 1.5f - transmittanceDelta * 8.0f);
                integrated = lerp(
                    integrated,
                    history,
                    saturate(gFrameParams.x * historyConfidence));
            }
        }
        gOutputVolume[uint3(dispatchThreadId.xy, slice)] = integrated;
        previousDistance = distanceToCamera;
    }
}

Texture2D<float> gCompositeDepth : register(t4);
Texture3D<float4> gIntegratedVolume : register(t5);
RWTexture2D<float4> gSceneColorUav : register(u1);

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
    const float depth = gCompositeDepth.Load(int3(pixel, 0));
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 world = mul(gInvViewProj, clip);
    world.xyz /= max(abs(world.w), 1e-6f);
    const float distanceToCamera = depth >= 0.999999f
        ? gFogDistances.y
        : length(world.xyz - gCameraPos.xyz);
    const float slice = DistanceToSlice(distanceToCamera);
    const float4 fog = gIntegratedVolume.SampleLevel(
        gLinearClamp, float3(uv, slice), 0.0f);

    const uint debugMode = (uint)(gDebugParams.x + 0.5f);
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
