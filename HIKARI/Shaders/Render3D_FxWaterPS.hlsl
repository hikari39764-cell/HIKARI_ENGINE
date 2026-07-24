#ifndef HIKARI_WATER_EMBEDDED
#define HIKARI_WATER_EMBEDDED 0
#endif

#undef gFxUser0
#undef gFxUser1
#undef gFxUser2
#undef gFxUser3
#undef gFxUser4
#undef gFxUser5
#undef gFxUser6
#undef gFxUser7
#define gFxUser0 waterObjectData.fxUser[0]
#define gFxUser1 waterObjectData.fxUser[1]
#define gFxUser2 waterObjectData.fxUser[2]
#define gFxUser3 waterObjectData.fxUser[3]
#define gFxUser4 waterObjectData.fxUser[4]
#define gFxUser5 waterObjectData.fxUser[5]
#define gFxUser6 waterObjectData.fxUser[6]
#define gFxUser7 waterObjectData.fxUser[7]

#define gWaterDepthScale gFxUser4.x
#define gWaterDepthBias  gFxUser4.y
#define gWaterDepthPower gFxUser4.z
#define gWaterDepthBlend gFxUser4.w

#define gWaterAlphaShallow gFxUser5.x
#define gWaterAlphaDeep    gFxUser5.y
#define gWaterAlphaFresnel gFxUser5.z
#define gWaterAlphaMin     gFxUser5.w

#define gWaterDetailStrength gFxUser6.x
#define gWaterDetailScale    gFxUser6.y
#define gWaterDetailSpeed    gFxUser6.z
#define gWaterDetailFadeDist gFxUser6.w

#define gWaterFoamWidth      gFxUser7.x
#define gWaterFoamStrength   gFxUser7.y
#define gWaterFoamPower      gFxUser7.z
#define gWaterFoamNoise      gFxUser7.w

#define gWaterRefractionStrength gFxUser2.w
#define gWaterSceneColorMix      gFxUser3.w

#if !HIKARI_WATER_EMBEDDED
cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gTimeParams;
    float4 gScreenParams;
};

#include "Include/HIKARI_MeshObjectData.hlsli"
#include "Include/HIKARI_DebugViewCommon.hlsli"
#include "Include/Forward/HIKARI_ForwardVertexMeta.hlsli"

cbuffer LightCB : register(b2)
{
    float4 gDirectionalDir;
    float4 gDirectionalColor;
    float4 gAmbientColor;
    float4 gSpecularParams;
    float4 gPointLightPosRange[8];
    float4 gPointLightColorIntensity[8];
    float gDirectionalIntensity;
    float gAmbientIntensity;
    uint gPointLightCount;
    float gLightPadding;
    float4 gFogColorDensity;
    float4 gFogParams;
    uint gDebugView;
    float gForwardCostMode;
    float2 gDebugPadding;
};

cbuffer ShadowCB : register(b4)
{
    float4x4 gShadowLightViewProj;
    uint gShadowEnabled;
    float gShadowDepthBias;
    float gShadowNormalBias;
    float gShadowStrength;
    uint gShadowPcfEnabled;
    float gShadowPcfRadius;
    float gShadowTexelSizeX;
    float gShadowTexelSizeY;
    float gShadowEdgeFade;
    float3 gShadowPadding;
};

cbuffer SkyEnvironmentCB : register(b5)
{
    float4 gSkyZenithExposure;
    float4 gSkyHorizonReflection;
    float4 gSkyGroundAmbient;
    float4 gSkyParams;
    float4 gIblParams;
};

#define gSkyZenithColor gSkyZenithExposure.rgb
#define gSkyExposure gSkyZenithExposure.a
#define gSkyHorizonColor gSkyHorizonReflection.rgb
#define gSkyReflectionIntensity gSkyHorizonReflection.a
#define gSkyGroundColor gSkyGroundAmbient.rgb
#define gSkyAmbientFromSky gSkyGroundAmbient.a
#define gSkyMode gSkyParams.x
#define gSkyHasCubemap gSkyParams.y
#define gSkyHorizonPower gSkyParams.z
#define gSkyYaw gSkyParams.w
#define gIblHasIrradiance gIblParams.x
#define gIblHasPrefiltered gIblParams.y
#define gIblHasBrdfLut gIblParams.z
#define gIblPrefilteredMipCount gIblParams.w

Texture2D gShadowMap : register(t2) ;
TextureCube gSkyCube : register(t6);
Texture2D gSceneDepth : register(t7);
Texture2D gSceneColorTex : register(t8);
SamplerComparisonState gShadowSampler : register(s1);
SamplerState gSkySampler : register(s0);

#ifndef WATER_DEBUG_SCENE_DEPTH
#define WATER_DEBUG_SCENE_DEPTH 0
#endif

#ifndef WATER_DEBUG_DEPTH_DIFF
#define WATER_DEBUG_DEPTH_DIFF 0
#endif

#ifndef WATER_DEBUG_SCENE_COLOR
#define WATER_DEBUG_SCENE_COLOR 0
#endif

#ifndef WATER_DEBUG_REFRACTION_COVERAGE
#define WATER_DEBUG_REFRACTION_COVERAGE 0
#endif

#ifndef WATER_DEBUG_ALPHA
#define WATER_DEBUG_ALPHA 0
#endif

#ifndef WATER_DEBUG_FOAM
#define WATER_DEBUG_FOAM 0
#endif

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    // Keep TEXCOORD slots aligned between VS and MeshletMS layouts for PSO linkage.
    // forwardMeta bit layout: HIKARI_ForwardVertexMeta.hlsli.
    nointerpolation uint forwardMeta : TEXCOORD3;
    nointerpolation uint objectDataIndex : TEXCOORD4;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD5;
    nointerpolation uint debugSurfaceId : TEXCOORD6;
};
#else
#define gSceneDepth gSceneDepthTex
#define gSkySampler gLinearWrap
#endif

float SampleSceneDepth(float4 svPosition)
{
    uint depthWidth = 1u;
    uint depthHeight = 1u;
    gSceneDepth.GetDimensions(depthWidth, depthHeight);

    int2 pixel = int2(floor(svPosition.xy));
    pixel = clamp(
        pixel,
        int2(0, 0),
        int2(max(int(depthWidth) - 1, 0), max(int(depthHeight) - 1, 0)));
    return gSceneDepth.Load(int3(pixel, 0)).r;
}

bool IsWaterPixelOccludedByOpaqueDepth(float4 svPosition)
{
    const float sceneDepth = SampleSceneDepth(svPosition);
    if (sceneDepth >= 0.9999f)
    {
        return false;
    }

    const float waterDepth = saturate(svPosition.z);
    return sceneDepth + 1e-5f < waterDepth;
}

float2 ComputeScreenUv(float4 svPosition)
{
    return svPosition.xy * gScreenParams.zw;
}

float3 ReconstructWorldFromDepth(float2 uv, float depth)
{
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 world = mul(gInvViewProj, float4(ndc, depth, 1.0f));
    return world.xyz / max(abs(world.w), 1e-5f);
}

float ComputeRawWaterDepthDiff(float4 svPosition, float3 waterWorldPos)
{
    float sceneDepth = SampleSceneDepth(svPosition);
    if (sceneDepth >= 0.9999f)
    {
        return 1.0f;
    }

    float2 uv = ComputeScreenUv(svPosition);
    float3 sceneWorld = ReconstructWorldFromDepth(uv, sceneDepth);
    float3 viewVector = waterWorldPos - gCameraPos.xyz;
    float viewLength = max(length(viewVector), 1e-4f);
    float3 viewDir = viewVector / viewLength;
    float viewDepthDiff = dot(sceneWorld - waterWorldPos, viewDir);

    return max(0.0f, viewDepthDiff * 0.0125f);
}

float ResolveWaterDepthScale(HikariMeshObjectData waterObjectData)
{
    float depthScale = gWaterDepthScale;
    if (depthScale <= 0.0001f)
    {
        depthScale = 80.0f;
    }
    return depthScale;
}

float ResolveWaterDepthBlend(HikariMeshObjectData waterObjectData)
{
    return saturate(gWaterDepthBlend);
}

float ComputeWaterDepthFactor(
    float4 svPosition,
    float3 waterWorldPos,
    HikariMeshObjectData waterObjectData)
{
    float rawDiff = ComputeRawWaterDepthDiff(svPosition, waterWorldPos);

    float depthScale = ResolveWaterDepthScale(waterObjectData);

    float depthBias = gWaterDepthBias;

    float depthPower = gWaterDepthPower;
    if (depthPower <= 0.0001f)
    {
        depthPower = 1.0f;
    }

    float depthFactor = saturate(rawDiff * depthScale + depthBias);
    depthFactor = pow(depthFactor, depthPower);

    return depthFactor;
}

float ComputeWaterAlpha(float depthFactor, float fresnel, HikariMeshObjectData waterObjectData)
{
    const float alphaDepthFactor = depthFactor * ResolveWaterDepthBlend(waterObjectData);

    float shallowAlpha = gWaterAlphaShallow;
    if (shallowAlpha <= 0.0001f)
    {
        shallowAlpha = 0.35f;
    }

    float deepAlpha = gWaterAlphaDeep;
    if (deepAlpha <= 0.0001f)
    {
        deepAlpha = 0.85f;
    }

    float alphaFresnel = gWaterAlphaFresnel;
    if (alphaFresnel <= 0.0001f)
    {
        alphaFresnel = 0.25f;
    }

    float minAlpha = gWaterAlphaMin;

    float waterAlpha = lerp(shallowAlpha, deepAlpha, alphaDepthFactor);
    waterAlpha += fresnel * alphaFresnel;
    waterAlpha = max(waterAlpha, minAlpha);

    return saturate(waterAlpha);
}

float DetailWave(float2 p, float time)
{
    float w1 = sin(p.x * 1.7f + time * 1.3f);
    float w2 = cos(p.y * 2.1f - time * 1.1f);
    float w3 = sin((p.x + p.y) * 1.4f + time * 0.8f);

    return (w1 + w2 + w3) / 3.0f;
}

float2 DetailWaveGradient(float2 p, float time)
{
    const float e = 0.05f;

    float h = DetailWave(p, time);
    float hx = DetailWave(p + float2(e, 0.0f), time);
    float hz = DetailWave(p + float2(0.0f, e), time);

    return float2(h - hx, h - hz) / e;
}

float3 ApplyWaterDetailNormal(
    float3 n,
    float3 worldPosWS,
    float distToCamera,
    HikariMeshObjectData waterObjectData)
{
    float detailStrength = gWaterDetailStrength;
    if (detailStrength <= 0.0001f)
    {
        detailStrength = 0.08f;
    }

    float detailScale = gWaterDetailScale;
    if (detailScale <= 0.0001f)
    {
        detailScale = 4.0f;
    }

    float detailSpeed = gWaterDetailSpeed;
    if (detailSpeed <= 0.0001f)
    {
        detailSpeed = 1.0f;
    }

    float detailFadeDist = gWaterDetailFadeDist;
    if (detailFadeDist <= 0.0001f)
    {
        detailFadeDist = 120.0f;
    }

    float detailFade = 1.0f - saturate(distToCamera / detailFadeDist);
    float2 detailGrad = DetailWaveGradient(worldPosWS.xz * detailScale, gTimeParams.x * detailSpeed);

    return normalize(n + float3(detailGrad.x, 0.0f, detailGrad.y) * detailStrength * detailFade);
}

float ComputeWaterFoam(
    float4 svPosition,
    float3 worldPosWS,
    HikariMeshObjectData waterObjectData)
{
    float rawDiff = ComputeRawWaterDepthDiff(svPosition, worldPosWS);

    float foamStrength = gWaterFoamStrength;
    if (foamStrength <= 0.0001f)
    {
        return 0.0f;
    }

    float foamWidth = gWaterFoamWidth;
    if (foamWidth <= 0.00001f)
    {
        foamWidth = 0.003f;
    }

    float foamPower = gWaterFoamPower;
    if (foamPower <= 0.0001f)
    {
        foamPower = 1.5f;
    }

    float foamNoise = gWaterFoamNoise;
    if (foamNoise < 0.0f)
    {
        foamNoise = 0.0f;
    }

    float foam = 1.0f - saturate(rawDiff / foamWidth);
    foam = pow(saturate(foam), foamPower);

    if (foamNoise > 0.0001f)
    {
        float noiseA = sin(worldPosWS.x * 8.0f + worldPosWS.z * 5.5f + gTimeParams.x * 1.7f);
        float noiseB = cos(worldPosWS.x * 3.5f - worldPosWS.z * 7.0f + gTimeParams.x * 1.2f);
        float noise = saturate((noiseA + noiseB) * 0.25f + 0.5f);

        foam *= lerp(1.0f, noise, saturate(foamNoise));
    }

    return saturate(foam * foamStrength);
}

float2 ComputeWaterSceneColorDistortion(
    float3 normalWS,
    float3 worldPosWS,
    HikariMeshObjectData waterObjectData)
{
    float refractionStrength = max(0.0f, gWaterRefractionStrength);

    float detailScale = gWaterDetailScale;
    if (detailScale <= 0.0001f)
    {
        detailScale = 4.0f;
    }

    float detailSpeed = gWaterDetailSpeed;
    if (detailSpeed <= 0.0001f)
    {
        detailSpeed = 1.0f;
    }

    float2 waveGrad = DetailWaveGradient(
        worldPosWS.xz * detailScale * 0.45f,
        gTimeParams.x * detailSpeed);

    float2 normalOffset = normalWS.xz * 0.6f + waveGrad * 0.4f;
    float2 uvOffset = normalOffset * refractionStrength;

    // Treat saved refraction values as artistic strength, but keep screen-space
    // sampling local. Large UV offsets pull unrelated opaque geometry through the
    // water when another plane exists below it.
    float2 maxUvOffset = gScreenParams.zw * 8.0f;
    return clamp(uvOffset, -maxUvOffset, maxUvOffset);
}

float ComputeWaterSceneColorCoverageMask(
    float4 svPosition,
    float3 worldPosWS,
    HikariMeshObjectData waterObjectData)
{
    const float depthBlend = ResolveWaterDepthBlend(waterObjectData);
    if (depthBlend <= 0.0001f)
    {
        return 0.0f;
    }

    float sceneDepth = SampleSceneDepth(svPosition);
    float rawDiff = ComputeRawWaterDepthDiff(svPosition, worldPosWS);

    if (sceneDepth >= 0.9999f || rawDiff <= 0.000001f)
    {
        return 0.0f;
    }

    float depthScale = ResolveWaterDepthScale(waterObjectData);

    float scaledDepth = rawDiff * depthScale + gWaterDepthBias;
    return smoothstep(0.08f, 0.85f, scaledDepth) * depthBlend;
}

float ComputeWaterSceneColorRefractionMask(float coverageMask, float depthFactor, float fresnel)
{
    float deepWaterFade = 1.0f - saturate(depthFactor * 0.7f);
    float facingFade = 1.0f - saturate(fresnel);

    return saturate(coverageMask * deepWaterFade * facingFade);
}

float3 ApplyWaterSceneColorRefraction(
    float3 waterColor,
    float4 svPosition,
    float3 normalWS,
    float3 worldPosWS,
    float depthFactor,
    float fresnel,
    HikariMeshObjectData waterObjectData,
    out float refractionCoverage)
{
    refractionCoverage = 0.0f;

    float sceneColorMix = saturate(gWaterSceneColorMix);
    if (sceneColorMix <= 0.0001f)
    {
        return waterColor;
    }

    float coverageMask =
        ComputeWaterSceneColorCoverageMask(svPosition, worldPosWS, waterObjectData);
    if (coverageMask <= 0.0001f)
    {
        return waterColor;
    }

    refractionCoverage = coverageMask;

    float refractionMask = ComputeWaterSceneColorRefractionMask(coverageMask, depthFactor, fresnel);
    if (refractionMask <= 0.0001f)
    {
        return waterColor;
    }

    // SceneColor is a snapshot captured before the DepthAware phase.
    // Do not sample the currently bound render target directly.
    float2 screenUv = svPosition.xy * gScreenParams.zw;
    float2 distortion = ComputeWaterSceneColorDistortion(normalWS, worldPosWS, waterObjectData);
    float3 sceneColor = gSceneColorTex.Sample(gSkySampler, saturate(screenUv + distortion)).rgb;

    float waterTint = saturate(depthFactor * 0.45f + fresnel * 0.65f);
    float3 refracted = lerp(sceneColor, waterColor, waterTint);

    float refractionWeight = sceneColorMix * refractionMask;
    return lerp(waterColor, refracted, refractionWeight);
}

#if !HIKARI_WATER_EMBEDDED
float3 ApplyFog(float3 color, float3 worldPosWS)
{
    if (gFogParams.x < 0.5f)
    {
        return color;
    }

    float dist = length(gCameraPos.xyz - worldPosWS);
    float fogRange = max(0.001f, gFogParams.z - gFogParams.y);
    float fogFactor = saturate((dist - gFogParams.y) / fogRange);
    fogFactor = saturate(fogFactor * max(0.0f, gFogColorDensity.a) * dist);

    return lerp(color, gFogColorDensity.rgb, fogFactor);
}

#include "Include/Forward/HIKARI_ShadowSampling.hlsli"

float3 RotateSkyYaw(float3 dir, float yaw)
{
    float s = sin(yaw);
    float c = cos(yaw);

    return float3(
        dir.x * c - dir.z * s,
        dir.y,
        dir.x * s + dir.z * c
    );

}

float3 EvaluateSkyApprox(float3 dir)
{
    dir = normalize(RotateSkyYaw(dir, gSkyYaw));

    float y = saturate(dir.y * 0.5f + 0.5f);

    float3 upper = lerp(gSkyHorizonColor, gSkyZenithColor, y);
    float3 lower = lerp(gSkyGroundColor, gSkyHorizonColor, y);
    float3 color = (dir.y >= 0.0f) ? upper : lower;

    float horizon = pow(
        saturate(1.0f  - abs(dir.y)),
        max(0.01f,gSkyHorizonPower)
    );

    color = lerp(color, gSkyHorizonColor, horizon * 0.25f);

    float exposure = max(0.0f, gSkyExposure);
    color *= exposure;

    return color;

}

float3 SampleSkyEnvironment(float3 dir)
{
    float3 sky = EvaluateSkyApprox(dir);

    uint mode = (uint)(gSkyMode + 0.5f);

    if(mode == 2u && gSkyHasCubemap > 0.5f)
    {
        float3 cubeDir = normalize(RotateSkyYaw(dir, gSkyYaw));
        sky = gSkyCube.Sample(gSkySampler, cubeDir).rgb;
        sky *= max(0.0f, gSkyExposure);
    }

    return sky;

}
#else
#define EvaluateSkyApprox HikariEvaluateSkyApprox
#define SampleSkyEnvironment HikariSampleSkyEnvironment
#endif

float4 HikariShadeWaterSurface(
    PSInput input,
    HikariMeshObjectData waterObjectData)
{
#if WATER_DEBUG_SCENE_DEPTH
    float sceneDepth = SampleSceneDepth(input.position);
    float vi = saturate((1.0f - sceneDepth) * 80.0f);
    return float4(vi.xxx, 1.0f);
#endif

#if WATER_DEBUG_DEPTH_DIFF
    float depthFactor =
        ComputeWaterDepthFactor(input.position, input.worldPosWS, waterObjectData);
    return float4(depthFactor.xxx, 1.0f);
#endif

    clip(IsWaterPixelOccludedByOpaqueDepth(input.position) ? -1.0f : 1.0f);

    float3 n = normalize(input.normalWS);

    float distToCamera = length(gCameraPos.xyz - input.worldPosWS);

    n = ApplyWaterDetailNormal(n, input.worldPosWS, distToCamera, waterObjectData);

    float farNormalFade = saturate((distToCamera - 40.0f) / 140.0f);

    float3 flatNormal = float3(0.0f, 1.0f, 0.0f);

    n = normalize(lerp(n, flatNormal, farNormalFade * 0.85f));

    float3 v = normalize(gCameraPos.xyz - input.worldPosWS);

    float3 l = normalize(-gDirectionalDir.xyz);
    float3 h = normalize(l + v);

    float3 waterColor = gFxUser1.xyz;
    if (dot(waterColor, waterColor) < 1e-5f)
    {
        waterColor = float3(0.05f, 0.35f, 0.75f);
    }

    float rimStrength = gFxUser0.w;

    float fresnelPower = gFxUser2.x;
    if (fresnelPower <= 0.0001f)
    {
        fresnelPower = 5.0f;
    }

    float reflectionStrength = gFxUser2.y;
    if (reflectionStrength <= 0.0001f)
    {
        reflectionStrength = 0.85f;
    }

    float specularStrength = gFxUser2.z;
    if (specularStrength <= 0.0001f)
    {
        specularStrength = 1.2f;
    }

    float3 shallowColor = gFxUser3.rgb;
    if (dot(shallowColor, shallowColor) < 1e-5f)
    {
        shallowColor = float3(0.12f, 0.55f, 0.68f);
    }

    float ndotl = saturate(dot(n, l));

    float fresnel = pow(1.0f - saturate(dot(n, v)), fresnelPower);
    fresnel = saturate(fresnel * reflectionStrength);


    float3 reflectDir = reflect(-v, n);
    reflectDir.y = abs(reflectDir.y);


    float shallowMix = saturate(n.y);
    float oldNormalShallow = shallowMix * 0.20f;

    float depthFactor =
        ComputeWaterDepthFactor(input.position, input.worldPosWS, waterObjectData);

    float depthBlend = ResolveWaterDepthBlend(waterObjectData);

    float3 depthWaterColor = lerp(shallowColor, waterColor, depthFactor);
    float3 normalWaterColor = lerp(waterColor, shallowColor, oldNormalShallow);
    float3 baseWater = lerp(normalWaterColor, depthWaterColor, saturate(depthBlend));

    float shadowFactor = SampleDirectionalShadow(
        input.worldPosWS,
        n,
        HikariForwardVertexMetaReceiveShadow(input.forwardMeta));

    float3 ambient = gAmbientColor.rgb * max(gAmbientIntensity, 0.05f);
    float3 sun = gDirectionalColor.rgb * gDirectionalIntensity * ndotl * shadowFactor;

    float specular = pow(saturate(dot(n, h)), 96.0f) * specularStrength;
    specular *= (1.0f - farNormalFade * 0.75f);
    float3 specularColor = gDirectionalColor.rgb * gDirectionalIntensity * specular * shadowFactor;

    float reflectionShadow = lerp(0.05f, 1.0f, shadowFactor);

    float3 reflection = SampleSkyEnvironment(reflectDir);

    float horizonFade = saturate((distToCamera - 80.0f) / 160.0f);
    float3 horizonDir = normalize(float3(v.x,0.05f,v.z));
    float3 horizonSky = EvaluateSkyApprox(horizonDir);

    reflection = lerp(reflection, horizonSky, horizonFade * 0.65f);
    reflection *= max(0.0f, gSkyReflectionIntensity);
    reflection *= reflectionShadow;
    
    float3 color = baseWater * (ambient + sun * 0.55f);
    float reflectionMix = saturate(fresnel * 0.75f);
    color = lerp(color, reflection, reflectionMix);
    color += specularColor;

    float rim = pow(1.0f - saturate(dot(n, v)), 2.0f) * rimStrength;
    color += float3(0.25f, 0.75f, 1.0f) * rim;

    if (gDebugView == 1)
    {
        return float4(n * 0.5f + 0.5f, 1.0f);
    }

#if WATER_DEBUG_SCENE_COLOR
    {
        float2 screenUv = input.position.xy * gScreenParams.zw;
        float2 distortion =
            ComputeWaterSceneColorDistortion(n, input.worldPosWS, waterObjectData) * 4.0f;
        float3 sceneColor = gSceneColorTex.Sample(gSkySampler, saturate(screenUv + distortion)).rgb;
        return float4(sceneColor, 1.0f);
    }
#endif

#if WATER_DEBUG_REFRACTION_COVERAGE
    {
        float coverage =
            ComputeWaterSceneColorCoverageMask(input.position, input.worldPosWS, waterObjectData);
        return float4(coverage.xxx, 1.0f);
    }
#endif

    if (gDebugView == 8)
    {
        return float4(shadowFactor.xxx, 1.0f);
    }
    if (gDebugView == 9)
    {
        return float4(ndotl.xxx, 1.0f);
    }
    if (gDebugView == 11)
    {
        float sceneDepth = SampleSceneDepth(input.position);
        return float4(sceneDepth.xxx, 1.0f);
    }
    if (gDebugView == 12)
    {
        float2 screenUv = input.position.xy * gScreenParams.zw;
        return float4(gSceneColorTex.Sample(gSkySampler, saturate(screenUv)).rgb, 1.0f);
    }

    float4 geometryDebugColor;
    if (HikariTryResolveGeometryDebugView(
        gDebugView,
        HikariForwardVertexMetaClusterId(input.forwardMeta),
        input.debugSurfaceId,
        HikariForwardVertexMetaLodIndex(input.forwardMeta),
        HikariForwardVertexMetaDrawBucket(input.forwardMeta),
        1.0f,
        geometryDebugColor))
    {
        return geometryDebugColor;
    }

    float refractionCoverage = 0.0f;
    color = ApplyWaterSceneColorRefraction(
        color,
        input.position,
        n,
        input.worldPosWS,
        depthFactor,
        fresnel,
        waterObjectData,
        refractionCoverage);

    float foam = ComputeWaterFoam(input.position, input.worldPosWS, waterObjectData);
    float3 foamColor = float3(0.85f, 0.95f, 1.0f);

#if WATER_DEBUG_FOAM
    return float4(foam.xxx, 1.0f);
#endif

    color = lerp(color, foamColor, foam);

    float waterAlpha = ComputeWaterAlpha(depthFactor, fresnel, waterObjectData);
    float refractionAlpha = saturate(waterAlpha + refractionCoverage * 0.28f);
    waterAlpha = lerp(waterAlpha, refractionAlpha, refractionCoverage);
    waterAlpha = saturate(waterAlpha + foam * 0.35f);

#if WATER_DEBUG_ALPHA
    return float4(waterAlpha.xxx, 1.0f);
#endif

    color = ApplyFog(color, input.worldPosWS);

    return float4(color, waterAlpha);
}

#if !HIKARI_WATER_EMBEDDED
float4 main(PSInput input) : SV_TARGET
{
    HikariMeshObjectData waterObjectData =
        HikariGetMeshObjectDataForSurfaceIndex(
            input.objectDataIndex,
            input.surfaceGpuSceneIndex);
    return HikariShadeWaterSurface(input, waterObjectData);
}
#endif
