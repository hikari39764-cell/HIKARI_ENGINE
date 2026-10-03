#ifndef HIKARI_WATER_SURFACE_PS_INCLUDED
#define HIKARI_WATER_SURFACE_PS_INCLUDED

#include "Include/HIKARI_WaterSurfaceParams.hlsli"
#include "Include/Surface/HIKARI_SurfaceFeatureFloats.hlsli"
#include "Include/Surface/HIKARI_WaterSpectralSlope.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceReflectionTrace.hlsli"
#include "Include/Surface/HIKARI_SurfaceTransmission.hlsli"
#include "Include/Water/HIKARI_WaterOptics.hlsli"

// 水面は Forward pass 内で全解像度に追跡する。全体 SSR (半解像度) より
// 1 画素あたりが重いため、同じ距離を半分の歩数で覆い、交差後の二分探索で詰める。
static const float HIKARI_WATER_SSR_STEP_FRACTION = 0.5f;

float HikariSampleLiquidCoverage(
    HikariWaterSurfaceParams p,
    float2 uv)
{
    return p.liquidCoverageOffset == 0u
        ? 1.0f
        : HikariSampleSurfaceFeatureField(p.liquidCoverageOffset, p.coverageResolution, uv);
}

struct HikariWaterDepthSample
{
    bool valid;
    float opticalDistance;
    float3 receiverWorldPosition;
};

HikariWaterDepthSample HikariSampleWaterCenterDepth(
    PSInput input,
    float2 screenUv)
{
    HikariWaterDepthSample result;
    result.valid = false;
    result.opticalDistance = 64.0f;
    result.receiverWorldPosition = input.worldPosWS;
    const float sceneDepth =
        gSceneDepthTex.Load(int3(int2(input.position.xy), 0)).r;
    if (!HikariReconstructWorldPosition(
            screenUv,
            sceneDepth,
            gInvViewProj,
            result.receiverWorldPosition))
    {
        return result;
    }
    const float3 viewDirection = HikariSafeNormalize(
        input.worldPosWS - gCameraPos.xyz,
        float3(0.0f, 0.0f, 1.0f));
    const float signedDistance = dot(
        result.receiverWorldPosition - input.worldPosWS,
        viewDirection);
    if (signedDistance < -0.025f)
    {
        return result;
    }
    result.valid = true;
    result.opticalDistance = max(signedDistance, 0.0f);
    return result;
}

float HikariWaterColumnDepth(
    PSInput input,
    HikariWaterDepthSample centerDepth,
    float3 stableSurfaceNormalWS)
{
    // Scene depth は視線距離なので、安定した水面法線へ射影して
    // カメラ角度に依存しない浅水遷移を作る。欠損・矛盾した深度は
    // 安全側として深水へ退避する。
    const float safeFallbackDepth = 1000.0f;
    if (!centerDepth.valid ||
        !HikariIsFiniteFloat3(centerDepth.receiverWorldPosition))
    {
        return safeFallbackDepth;
    }
    const float normalColumnDepth = dot(
        centerDepth.receiverWorldPosition - input.worldPosWS,
        -stableSurfaceNormalWS);
    if (!HikariIsFiniteFloat(normalColumnDepth) ||
        normalColumnDepth < -0.025f)
    {
        return safeFallbackDepth;
    }
    return max(normalColumnDepth, 0.0f);
}

float3 HikariSampleWaterRefraction(
    PSInput input,
    float3 normalWS,
    float2 screenUv,
    HikariWaterDepthSample centerDepth,
    float distortionStrength,
    float3 authoredFallback,
    out float opticalDistance,
    out float refractionConfidence)
{
    const float3 viewDirection = HikariSafeNormalize(
        gCameraPos.xyz - input.worldPosWS,
        normalWS);
    const float3 refractedDirection = HikariSafeNormalize(
        refract(-viewDirection, normalWS, 1.0f / 1.333f),
        reflect(-viewDirection, normalWS));
    const float3 skyFallback = HikariWaterResolveSkyFallback(
        refractedDirection,
        authoredFallback,
        1.0f);
    float2 projectedOffset = 0.0f.xx;
    HikariProjectWorldDirectionToUvOffset(
        input.worldPosWS,
        normalWS,
        0.35f,
        gViewProj,
        projectedOffset);
    const float maximumOffset =
        10.0f * max(gScreenParams.z, gScreenParams.w);
    const float projectedLength = length(projectedOffset);
    if (projectedLength > maximumOffset)
    {
        projectedOffset *= maximumOffset /
            max(projectedLength, 1.0e-6f);
    }
    const float2 candidateUv =
        screenUv + projectedOffset * distortionStrength;
    const int2 maximumPixel = max(int2(gScreenParams.xy) - 1, 0);
    const int2 candidatePixel = clamp(
        int2(candidateUv * gScreenParams.xy),
        int2(0, 0),
        maximumPixel);
    const float candidateDepth =
        gSceneDepthTex.Load(int3(candidatePixel, 0)).r;
    float3 receiverWorldPosition;
    float candidateOpticalDistance;
    float candidateConfidence;
    const bool candidateValid =
        HikariValidateSceneColorDistortionCandidate(
            input.worldPosWS,
            gCameraPos.xyz,
            candidateUv,
            candidateDepth,
            gInvViewProj,
            gScreenParams.zw,
            receiverWorldPosition,
            candidateOpticalDistance,
            candidateConfidence);

    if (candidateValid && centerDepth.valid)
    {
        const float continuityScale = max(
            HikariFiniteOr(centerDepth.opticalDistance, 64.0f) * 0.75f,
            0.35f);
        candidateConfidence *= exp(
            -abs(HikariFiniteOr(candidateOpticalDistance, 0.0f) -
                HikariFiniteOr(centerDepth.opticalDistance, 64.0f)) /
            continuityScale);
    }
    else if (!centerDepth.valid)
    {
        candidateConfidence = 0.0f;
    }

    refractionConfidence = saturate(HikariFiniteOr(
        candidateConfidence,
        0.0f));
    const float fallbackOpticalDistance = max(
        4.0f,
        HikariFiniteOr(centerDepth.opticalDistance, 0.0f));
    opticalDistance = lerp(
        centerDepth.valid
            ? HikariFiniteOr(centerDepth.opticalDistance, 4.0f)
            : fallbackOpticalDistance,
        HikariFiniteOr(candidateOpticalDistance, 0.0f),
        refractionConfidence);
    opticalDistance = max(HikariFiniteOr(
        opticalDistance,
        fallbackOpticalDistance), 0.0f);
    const float3 sampledCenterColor =
        gSceneColorTex.Sample(gLinearClamp, screenUv).rgb;
    const bool centerCaptureValid =
        (gScreenSpaceTraceFlags &
            HIKARI_SCREEN_SPACE_SCENE_COLOR_CAPTURE_VALID) != 0u &&
        HikariIsFiniteFloat3(sampledCenterColor) &&
        all(abs(sampledCenterColor) < 65504.0f.xxx);
    const float3 centerColor = HikariWaterResolveSceneCapture(
        sampledCenterColor,
        skyFallback,
        centerCaptureValid ? 1.0f : 0.0f,
        1.0f);
    const float3 sampledCandidateColor = candidateValid
        ? gSceneColorTex.Sample(gLinearClamp, candidateUv).rgb
        : centerColor;
    const float3 candidateColor = candidateValid
        ? HikariWaterResolveSceneCapture(
            sampledCandidateColor,
            skyFallback,
            1.0f,
            candidateConfidence)
        : centerColor;
    return HikariFiniteOr(
        lerp(centerColor, candidateColor, refractionConfidence),
        skyFallback);
}

float HikariWaterSmoothBand(float value, float threshold)
{
    const float width = max(fwidth(value) * 1.5f, 0.006f);
    return smoothstep(threshold - width, threshold + width, value);
}

float2 HikariWaterRotate2D(float2 value, float angle)
{
    float sine;
    float cosine;
    sincos(angle, sine, cosine);
    return float2(
        cosine * value.x + sine * value.y,
        -sine * value.x + cosine * value.y);
}

float HikariWaterCapsuleSdf(
    float2 position,
    float halfLength,
    float radius)
{
    position.x -= clamp(position.x, -halfLength, halfLength);
    return length(position) - radius;
}

float HikariWaterSdfCoverage(float signedDistance)
{
    const float antialiasWidth = max(fwidth(signedDistance), 0.0015f);
    return 1.0f - smoothstep(
        -antialiasWidth,
        antialiasWidth,
        signedDistance);
}

float HikariWaterAnimeSparkleMask(
    float2 localXZ,
    float animationTime,
    float sunAlignment,
    float compression,
    float glintDensity,
    float glintFlowSpeed,
    float ribbonWidth)
{
    // Grid は local space に固定し、hash は中心・角度・位相だけを変える。
    // 時間を cell ownership に混ぜず、矩形単位の点滅を防ぐ。
    const float density = saturate(HikariFiniteOr(
        glintDensity,
        0.30f));
    const float flowSpeed = max(HikariFiniteOr(
        glintFlowSpeed,
        0.62f), 0.0f);
    const float safeRibbonWidth = clamp(HikariFiniteOr(
        ribbonWidth,
        0.16f), 0.02f, 0.5f);
    const float2 gridPosition = localXZ * lerp(
        1.55f,
        3.80f,
        density);
    const float2 cellId = floor(gridPosition);
    const float2 cellPosition = frac(gridPosition) - 0.5f;
    const float2 center = (
        float2(
            HikariWaterOpticsHash21(cellId + float2(11.0f, 37.0f)),
            HikariWaterOpticsHash21(cellId + float2(53.0f, 19.0f))) -
        0.5f.xx) * 0.48f;
    const float angle = HikariWaterOpticsHash21(
        cellId + float2(29.0f, 71.0f)) * 6.28318530718f;
    const float phase = HikariWaterOpticsHash21(
        cellId + float2(83.0f, 43.0f)) * 6.28318530718f;
    const float2 markPosition = HikariWaterRotate2D(
        cellPosition - center,
        angle);

    const float ribbonWidth01 = saturate(
        (safeRibbonWidth - 0.02f) / 0.48f);
    const float brush = HikariWaterSdfCoverage(
        HikariWaterCapsuleSdf(
            markPosition,
            lerp(0.08f, 0.22f, ribbonWidth01),
            lerp(0.010f, 0.034f, ribbonWidth01)));
    const float starRay0 = HikariWaterSdfCoverage(
        HikariWaterCapsuleSdf(
            HikariWaterRotate2D(markPosition, 0.78539816339f),
            0.060f,
            0.009f));
    const float starRay1 = HikariWaterSdfCoverage(
        HikariWaterCapsuleSdf(
            HikariWaterRotate2D(markPosition, -0.78539816339f),
            0.060f,
            0.009f));
    const float mark = max(
        brush,
        max(starRay0, starRay1) * 0.82f);

    const float phaseWave = 0.5f + 0.5f * cos(
        animationTime * (1.0f + flowSpeed * 0.56f) +
        phase);
    const float phaseGate = smoothstep(0.58f, 0.92f, phaseWave);
    const float sunGate = smoothstep(0.76f, 0.96f, sunAlignment);
    const float crestGate = smoothstep(0.16f, 0.62f, compression);
    return mark * phaseGate * sunGate * crestGate *
        lerp(0.35f, 1.0f, density);
}

float HikariWaterRealisticSunGlitterLobe(
    float3 normalWS,
    float3 viewDirection,
    float3 lightDirection,
    float roughness,
    float glintDensity,
    float slopeVariance)
{
    // Sun glitter is a continuous microfacet-lighting lobe. Surface motion is
    // already carried by the geometric and spectral normals; imposing a
    // second thresholded sine lattice here produces the regular white oval
    // grid that does not exist on real water.
    const float3 halfVector = HikariSafeNormalize(
        lightDirection + viewDirection,
        normalWS);
    const float alignment = saturate(dot(normalWS, halfVector));
    const float density = saturate(HikariFiniteOr(glintDensity, 0.46f));
    const float variance = saturate(HikariFiniteOr(slopeVariance, 0.0f));
    const float safeRoughness = clamp(
        HikariFiniteOr(roughness, 0.12f),
        0.02f,
        0.45f);
    const float threshold = clamp(
        lerp(0.9985f, 0.9845f, density) -
            safeRoughness * 0.010f - variance * 0.008f,
        0.965f,
        0.999f);
    const float lobeWidth = max(
        fwidth(alignment) * 2.0f,
        0.0012f + safeRoughness * 0.004f + variance * 0.003f);
    const float sunFacet = smoothstep(
        threshold - lobeWidth,
        min(threshold + lobeWidth, 0.9999f),
        alignment);
    const float surfaceLit = smoothstep(
        0.0f,
        0.08f,
        dot(normalWS, lightDirection));
    const float varianceSupport = saturate(
        0.22f + density * 0.38f + sqrt(variance) * 0.55f);
    return saturate(sunFacet * surfaceLit * varianceSupport);
}

float4 HikariShadeWaterSurface(
    PSInput input,
    HikariMeshObjectData waterObjectData)
{
    const HikariWaterSurfaceParams p =
        HikariLoadWaterSurfaceParams(input.surfaceGpuSceneIndex);
    const float liquidCoverage =
        HikariSampleLiquidCoverage(p, input.uv);
    if (liquidCoverage < 0.5f)
    {
        discard;
    }
    const bool anime = p.shadingModel < 0.5f;
    const bool realistic = !anime;
    const float simulationTime = HikariFiniteOr(
        gSimulationTimeParams.x,
        0.0f);
    const float animationTime =
        HikariWrapWaterSimulationTime(simulationTime, p.timeScale);
    const float2 localXZ = HikariFiniteOr(
        input.surfaceFeatureData.xy,
        0.0f.xx);
    const float compression = saturate(HikariFiniteOr(
        input.surfaceFeatureData.z,
        0.0f));

    float3 geometricNormal = HikariSafeNormalize(
        input.normalWS,
        float3(0.0f, 1.0f, 0.0f));
    if (!input.isFrontFace)
    {
        geometricNormal = -geometricNormal;
    }
    const float3 stableSurfaceNormalFallback = input.isFrontFace
        ? geometricNormal
        : -geometricNormal;
    const float3 stableSurfaceNormalUnflipped = HikariSafeNormalize(
        mul(
            (float3x3)waterObjectData.normalMatrix,
            float3(0.0f, 1.0f, 0.0f)),
        stableSurfaceNormalFallback);
    const float3 stableSurfaceNormalWS = input.isFrontFace
        ? stableSurfaceNormalUnflipped
        : -stableSurfaceNormalUnflipped;
    const float3 toCamera = gCameraPos.xyz - input.worldPosWS;
    const float distanceToCamera = max(HikariFiniteOr(
        length(toCamera),
        1.0e-4f), 1.0e-4f);
    const float3 view = HikariSafeNormalize(toCamera, geometricNormal);
    const float3 light = HikariSafeNormalize(
        -gDirectionalDir.xyz,
        float3(0.0f, 1.0f, 0.0f));
    float3 normalWS = HikariWaterResolveMicroNormal(
        geometricNormal,
        input.tangentWS,
        localXZ,
        animationTime,
        p.microWaveStrength,
        anime,
        p.windDirectionRadians,
        p.windSpeed,
        distanceToCamera);
    const float2 worldXZ = HikariFiniteOr(
        input.worldPosWS.xz,
        0.0f.xx);
    HikariWaterSpectralSlopeResult microSlope;
    microSlope.slope = 0.0f.xx;
    microSlope.variance = 0.0f;
    float microSlopeWeight = 0.0f;
    if (realistic &&
        (p.shortWaveStrength > 1.0e-4f ||
            p.capillaryStrength > 1.0e-4f))
    {
        microSlope = HikariWaterResolveSpectralSlope(
            worldXZ,
            animationTime,
            p.windDirectionRadians,
            p.windSpeed,
            p.shortWaveStrength,
            p.capillaryStrength);
        microSlopeWeight = HikariWaterSpectralResolvedSlopeWeight(
            worldXZ,
            distanceToCamera);
        normalWS = HikariWaterSpectralNormalFromSlope(
            normalWS,
            microSlope.slope,
            microSlopeWeight);
    }

    const float2 screenUv = HikariScreenUvFromSvPosition(
        input.position,
        gScreenParams.zw);
    const HikariWaterDepthSample centerDepth =
        HikariSampleWaterCenterDepth(input, screenUv);
    const float columnDepth = HikariWaterColumnDepth(
        input,
        centerDepth,
        stableSurfaceNormalWS);
    const float shallowMask = 1.0f - smoothstep(
        p.shallowClearDepth,
        p.shallowBlendDepth,
        columnDepth);
    float opticalDistance;
    float refractionConfidence;
    const float deepDistortionStrength = anime
        ? 0.55f + p.microWaveStrength * 0.20f
        : 0.85f + p.microWaveStrength * 0.30f;
    const float distortionStrength = anime
        ? deepDistortionStrength * lerp(
            1.0f,
            0.30f,
            shallowMask)
        : deepDistortionStrength;
    const float3 sceneRefraction = HikariSampleWaterRefraction(
        input,
        normalWS,
        screenUv,
        centerDepth,
        distortionStrength,
        p.scatteringColor,
        opticalDistance,
        refractionConfidence);

    const float3 transmittance = HikariWaterResolveBeerLambert(
        p.depthTransmittance,
        p.absorptionDistance,
        opticalDistance);
    const float3 safeScatteringColor = max(
        HikariFiniteOr(p.scatteringColor, 0.0f.xxx),
        0.0015f.xxx);
    const float3 sunRadiance = HikariWaterResolveSunRadiance();
    const float3 transmittedBase =
        HikariWaterResolveSingleLayerScattering(
            sceneRefraction,
            safeScatteringColor,
            transmittance,
            view,
            light,
            sunRadiance,
            max(p.directSunStrength, 0.0f),
            realistic);
    // Beer-Lambert remains the authored transport term. The single-layer
    // solar lobe makes clear water readable without replacing the scene
    // capture or inventing an unbounded emissive contribution.
    const float3 transmissionFloor = max(
        safeScatteringColor * (anime ? 0.12f : 0.08f),
        float3(0.0015f, 0.004f, 0.006f));
    const float3 transmittedColor = max(
        max(transmittedBase, 0.0f.xxx),
        transmissionFloor);

    const float waterF0 = HikariDielectricF0FromIor(1.0f, 1.333f);
    float roughness = max(
        p.surfaceRoughness,
        anime ? 0.18f : 0.12f);
    const float normalFootprint = saturate(
        length(fwidth(normalWS)) * 0.50f);
    roughness = max(roughness, normalFootprint);
    if (realistic && microSlopeWeight > 0.0f)
    {
        // Unresolved short slopes move into roughness with the pixel
        // footprint so distant GGX highlights remain temporally stable.
        roughness = HikariWaterSpectralRoughnessFromFootprint(
            roughness,
            microSlope.variance,
            worldXZ,
            distanceToCamera,
            microSlopeWeight);
    }
    HikariPbrShadingContext reflectionContext =
        HikariBuildForwardPbrShadingContext(
            0.0f.xxx,
            0.0f,
            roughness,
            1.0f.xxx,
            waterF0 / 0.04f,
            normalWS,
            view);
    const float shadowVisibility = saturate(HikariFiniteOr(
        SampleDirectionalShadow(
            input.worldPosWS,
            geometricNormal,
            HikariForwardVertexMetaReceiveShadow(input.forwardMeta)),
        1.0f));
    // Shadow visibility only attenuates direct sun. A bounded influence keeps
    // receiver self-shadow from erasing the full water response; sky/SSR and
    // transmission remain independent providers.
    const float directShadow = lerp(
        1.0f,
        shadowVisibility,
        saturate(p.shadowStrength));
    const float3 directionalReflection = HikariFiniteOr(
        HikariEvaluateDirectionalPbr(
            reflectionContext,
            light,
            gDirectionalColor.rgb,
            gDirectionalIntensity,
            gDirectionalDir.w) *
            max(p.directSunStrength, 0.0f) * directShadow * HikariForwardCloudSolarVisibility(input.worldPosWS),
        0.0f.xxx);
    const float3 localReflection = HikariFiniteOr(
        AccumulateLocalLightsPbr(
            reflectionContext,
            input.worldPosWS,
            input.position.xy, geometricNormal,
            HikariForwardVertexMetaReceiveShadow(input.forwardMeta)),
        0.0f.xxx);
    const float3 environmentReflection = HikariFiniteOr(
        HikariEvaluateAmbientIbl(
            reflectionContext,
            1.0f,
            1.0f,
            input.worldPosWS,
            0.0f.xxx,
            0.0f),
        0.0f.xxx);
    const float fresnel = HikariWaterResolveFresnel(
        dot(normalWS, view),
        realistic);
    const float3 reflectionDirection = HikariSafeNormalize(
        reflect(-view, normalWS),
        normalWS);
    const float3 providerEnvironmentReflection =
        HikariWaterResolveEnvironmentFallback(
            environmentReflection,
            reflectionDirection,
            dot(normalWS, view),
            fresnel,
            roughness,
            1.0f,
            safeScatteringColor);
    HikariScreenSpaceReflectionTraceConfig waterSsrConfig;
    // 反射強度 0 の水面は反射 budget の gain が 0 になるため追跡しない。
    waterSsrConfig.valid =
        (gScreenSpaceTraceFlags &
            HIKARI_SCREEN_SPACE_DEPTH_TRACE_VALID) != 0u &&
        gScreenSpaceReflectionParams0.x > 0.0f &&
        HikariFiniteOr(p.reflectionStrength, 0.0f) > 0.0f
            ? 1u
            : 0u;
    const float globalSsrSteps = max(
        HikariFiniteOr(gScreenSpaceReflectionParams1.z, 0.0f),
        0.0f);
    waterSsrConfig.maxSteps = (uint)ceil(
        globalSsrSteps * HIKARI_WATER_SSR_STEP_FRACTION);
    waterSsrConfig.hzbMipCount =
        max(gScreenSpaceDepthPyramidMipCount, 1u);
    waterSsrConfig.maxDistance = HikariFiniteOr(
        gScreenSpaceReflectionParams0.y,
        0.0f);
    waterSsrConfig.thickness = HikariFiniteOr(
        gScreenSpaceReflectionParams0.z,
        0.0f);
    waterSsrConfig.maxRoughness = HikariFiniteOr(
        gScreenSpaceReflectionParams0.w,
        0.0f);
    waterSsrConfig.stride = HikariFiniteOr(
        gScreenSpaceReflectionParams1.x,
        1.0f) * (globalSsrSteps / max((float)waterSsrConfig.maxSteps, 1.0f));
    waterSsrConfig.edgeFade = HikariFiniteOr(
        gScreenSpaceReflectionParams1.y,
        0.0f);
    const HikariScreenSpaceReflectionTraceResult waterSsr =
        HikariTraceScreenSpaceReflectionFromSurface(
            gSceneColorTex,
            gSceneDepthTex,
            gScreenSpaceDepthPyramid,
            gScreenSpacePackedSurface,
            gLinearClamp,
            gPointClamp,
            input.worldPosWS,
            normalWS,
            roughness,
            gCameraPos.xyz,
            gViewProj,
            gInvViewProj,
            waterSsrConfig);
    // Preserve the existing SSR -> probe/IBL -> sky order. An invalid SSR
    // hit cannot replace a valid provider with black; the helper also uses a
    // max() sky fallback rather than adding a second reflection lobe.
    const float3 resolvedEnvironmentReflection =
        HikariWaterResolveSsrReplacement(
            providerEnvironmentReflection,
            waterSsr,
            fresnel,
            max(HikariFiniteOr(
                gScreenSpaceReflectionParams0.x,
                0.0f),
                0.0f));

    if (gDebugView == HIKARI_DEBUG_VIEW_SSR_CONFIDENCE)
    {
        return float4(HikariFiniteOr(
            waterSsr.confidence,
            0.0f).xxx, 1.0f);
    }
    if (gDebugView == HIKARI_DEBUG_VIEW_SSR_HIT)
    {
        return waterSsr.hit != 0u
            ? float4(waterSsr.hitUv, 1.0f, 1.0f)
            : float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const float layeredNoise = anime
        ? HikariWaterOpticsLayeredNoise(localXZ, animationTime)
        : 0.5f;
    const float shoreFoam = centerDepth.valid
        ? 1.0f - smoothstep(0.18f, 1.65f, columnDepth)
        : 0.0f;
    const float crestFoam = smoothstep(
        anime ? 0.18f : 0.30f,
        anime ? 0.62f : 0.78f,
        compression) *
        (anime
            ? smoothstep(0.42f, 0.78f, layeredNoise)
            : 0.34f);
    const float foam = saturate(
        (shoreFoam * (anime ? 0.80f : 0.58f) + crestFoam) *
        p.foamAmount);

    // Keep transport and reflection providers separate. Shadow influence is
    // intentionally absent from transmission and sky/SSR so receiver
    // self-shadow cannot turn a valid water column into a black patch.
    const float3 transmissionTerm = transmittedColor *
        (1.0f - fresnel) * max(p.transmissionStrength, 0.0f);
    const float3 environmentTerm = HikariFiniteOr(
        resolvedEnvironmentReflection,
        0.0f.xxx);
    const float3 localTerm = HikariFiniteOr(
        localReflection * (realistic ? 1.0f : 0.62f),
        0.0f.xxx);

    float3 color;
    if (anime)
    {
        const float normalizedDepth = saturate(
            opticalDistance / max(p.absorptionDistance, 0.25f));
        const float depthStep0 =
            HikariWaterSmoothBand(normalizedDepth, 0.18f);
        const float depthStep1 =
            HikariWaterSmoothBand(normalizedDepth, 0.46f);
        const float depthStep2 =
            HikariWaterSmoothBand(normalizedDepth, 0.74f);
        const float viewGrazing =
            1.0f - saturate(dot(normalWS, view));
        const float viewStep =
            HikariWaterSmoothBand(viewGrazing, 0.38f);
        const float sunFacing = saturate(dot(normalWS, light));
        const float sunStep =
            HikariWaterSmoothBand(sunFacing, 0.32f) * directShadow;
        const float compressionStep =
            HikariWaterSmoothBand(compression, 0.22f);
        const float3 shallowSummer =
            max(p.scatteringColor, float3(0.02f, 0.22f, 0.28f));
        const float3 deepSummer =
            max(p.depthTransmittance, float3(0.015f, 0.08f, 0.18f));
        const float3 middleSummer = lerp(
            shallowSummer,
            deepSummer,
            0.48f) + float3(0.0f, 0.055f, 0.070f);
        const float3 abyssSummer =
            deepSummer * float3(0.58f, 0.72f, 0.88f);
        float3 graphicBody = lerp(
            shallowSummer,
            middleSummer,
            depthStep0);
        graphicBody = lerp(graphicBody, deepSummer, depthStep1);
        graphicBody = lerp(graphicBody, abyssSummer, depthStep2);
        const float3 grazingSummer = lerp(
            deepSummer,
            float3(0.035f, 0.30f, 0.44f),
            0.68f);
        const float3 sunlitSummer = lerp(
            shallowSummer,
            float3(0.12f, 0.72f, 0.70f),
            0.52f);
        const float3 compressedSummer = lerp(
            shallowSummer,
            float3(0.10f, 0.76f, 0.82f),
            0.62f);
        graphicBody = lerp(graphicBody, grazingSummer, viewStep * 0.30f);
        graphicBody = lerp(graphicBody, sunlitSummer, sunStep * 0.18f);
        graphicBody = lerp(
            graphicBody,
            compressedSummer,
            compressionStep * 0.24f);
        const float deepGraphicWeight = saturate(
            0.22f + depthStep1 * 0.10f +
            depthStep2 * 0.08f + compressionStep * 0.05f);
        const float graphicWeight = lerp(
            deepGraphicWeight,
            p.shallowGraphicWeight,
            shallowMask);
        // Anime ramp is a bounded tint over the physical transport. It must
        // not replace Beer-Lambert transmission or erase a valid reflection
        // provider, which was the source of the old flat blue body.
        const float3 transmissionReference = max(
            transmittedColor,
            float3(0.045f, 0.045f, 0.045f));
        const float3 graphicTint = clamp(
            graphicBody / transmissionReference,
            0.55f.xxx,
            1.85f.xxx);
        color = max(
            transmissionTerm * lerp(
                1.0f.xxx,
                graphicTint,
                saturate(graphicWeight * 0.64f)),
            transmissionTerm * 0.55f);

        const float3 halfVector = HikariSafeNormalize(
            light + view,
            normalWS);
        const float highlightAlignment = saturate(
            dot(normalWS, halfVector));
        const float highlightWidth = max(
            fwidth(highlightAlignment) * 2.0f,
            0.015f);
        const float sunRibbon = smoothstep(
            0.72f - highlightWidth,
            0.82f + highlightWidth,
            highlightAlignment);
        const float sparkle = HikariWaterAnimeSparkleMask(
            localXZ,
            animationTime,
            highlightAlignment,
            compression,
            p.glintDensity,
            p.glintFlowSpeed,
            p.ribbonWidth);
        const float stylizedDetailWeight = 1.0f - smoothstep(
            90.0f,
            240.0f,
            distanceToCamera);
        const float glintMask = saturate(
            sunRibbon * lerp(0.12f, 0.36f, stylizedDetailWeight) +
            sparkle * stylizedDetailWeight);
        // Anime sparkle is a bounded directional-sun lobe, never an
        // unconditional emissive lift. Its energy joins the same reflection
        // budget as sky/SSR and local/direct PBR.
        const float3 animeSunGlint = sunRadiance *
            (glintMask * max(p.sunGlintStrength, 0.0f) *
                max(p.directSunStrength, 0.0f) * directShadow * 0.45f);
        const float environmentWeight = lerp(
            0.68f + 0.22f * fresnel,
            1.0f,
            shallowMask);
        const float3 reflectionEnergy =
            environmentTerm * environmentWeight +
            directionalReflection * 0.88f +
            localTerm +
            animeSunGlint;
        color += HikariWaterApplyReflectionBudget(
            reflectionEnergy,
            p.reflectionStrength);

        const float crestRibbon = saturate(
            HikariWaterSmoothBand(compression, 0.27f) -
            HikariWaterSmoothBand(compression, 0.58f));
        const float3 crestRibbonColor = lerp(
            shallowSummer,
            float3(0.16f, 0.84f, 0.88f),
            0.62f);
        color += crestRibbonColor *
            (crestRibbon * stylizedDetailWeight *
                (0.10f + 0.08f * sunStep));

        const float foamCoverage = HikariWaterSmoothBand(foam, 0.08f);
        const float foamCore = HikariWaterSmoothBand(foam, 0.43f);
        const float foamEdge = foamCoverage * (1.0f - foamCore);
        const float3 foamCyanEdge = lerp(
            float3(0.08f, 0.74f, 0.94f),
            p.foamColor,
            0.18f);
        const float3 foamWarmCore = lerp(
            p.foamColor,
            float3(1.08f, 0.98f, 0.80f),
            0.62f);
        color = lerp(
            color,
            foamCyanEdge,
            saturate(foamEdge * 0.92f + foamCore * 0.20f));
        color = lerp(color, foamWarmCore, foamCore);
    }
    else
    {
        // RealisticClearSummer uses the physical transport term plus one
        // shared reflection budget. SSR replaces the sky provider only when
        // its confidence is valid; no branch can manufacture emissive light.
        const float realisticGlitterMask =
            HikariWaterRealisticSunGlitterLobe(
                normalWS,
                view,
                light,
                roughness,
                p.glintDensity,
                microSlope.variance);
        const float3 realisticSunGlitter = sunRadiance *
            (realisticGlitterMask *
                max(p.sunGlintStrength, 0.0f) *
                max(p.directSunStrength, 0.0f) *
                directShadow * 0.52f);
        const float3 reflectionEnergy =
            environmentTerm + directionalReflection + localTerm +
            realisticSunGlitter;
        color = transmissionTerm + HikariWaterApplyReflectionBudget(
            reflectionEnergy,
            p.reflectionStrength);
        const float foamLighting = 0.72f +
            0.28f * saturate(dot(geometricNormal, light)) * directShadow;
        color = lerp(
            color,
            p.foamColor * foamLighting,
            foam);
    }

#if HIKARI_FORWARD_ENABLE_MATERIAL_FX
#if HIKARI_FORWARD_COST_MODE_STATIC != 255
    static const uint waterForwardCostMode =
        HIKARI_FORWARD_COST_MODE_STATIC;
#else
    const uint waterForwardCostMode =
        (uint)(gForwardCostMode + 0.5f);
#endif
    if (waterForwardCostMode !=
            HIKARI_FORWARD_COST_NO_MATERIAL_EXTRAS &&
        HikariSurfaceHasMaterialFx(input.surfaceFeatureFlags))
    {
        color = HikariApplyStaticMaterialFx(
            color,
            input.worldPosWS,
            geometricNormal,
            normalWS,
            view,
            input.position,
            gTimeParams.x,
            waterObjectData);
    }
#endif

    // 非有限値を alpha=1 で SceneColor に確定書き込みしない。
    // provider が一時的に無効でも、各スタイルの夏色を明示的な最終退避先にする。
    const float3 summerFallback = anime
        ? float3(0.02f, 0.30f, 0.42f)
        : float3(0.015f, 0.18f, 0.24f);
    const bool colorFitsHdrTarget = HikariIsFiniteFloat3(color) &&
        all(abs(color) < 65504.0f.xxx);
    const float3 finiteColor = colorFitsHdrTarget
        ? max(color, 0.0f.xxx)
        : summerFallback;

    // Water used to return before the shared forward fog path and also
    // discarded the captured panorama whenever scene depth was clear. Fog the
    // authored water first, then converge only the distant clear-depth edge
    // to the real captured sky sample for a continuous water/sky horizon.
    const float3 foggedColor = ApplyFog(finiteColor, input.worldPosWS);
    const float3 explicitHorizonSky = HikariWaterResolveSkyFallback(
        -view,
        p.scatteringColor,
        1.0f);
    const float3 resolvedColor = HikariWaterBlendWithSceneHorizon(
        foggedColor,
        sceneRefraction,
        explicitHorizonSky,
        geometricNormal,
        view,
        distanceToCamera,
        centerDepth.valid ? 1.0f : 0.0f);

    // SceneColor は上で明示的に合成済み。alpha=1 の premultiplied blend で
    // destination の二重取り込みを避ける。
    return float4(resolvedColor, 1.0f);
}

#endif
