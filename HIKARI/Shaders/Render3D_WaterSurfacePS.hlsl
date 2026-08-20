#ifndef HIKARI_WATER_SURFACE_PS_INCLUDED
#define HIKARI_WATER_SURFACE_PS_INCLUDED

#include "Include/HIKARI_WaterSurfaceParams.hlsli"
#include "Include/Surface/HIKARI_WaterSpectralSlope.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceReflectionTrace.hlsli"
#include "Include/Surface/HIKARI_SurfaceTransmission.hlsli"

float HikariSampleLiquidCoverage(
    HikariWaterSurfaceParams p,
    float2 uv)
{
    if (p.liquidCoverageOffset == 0u)
    {
        return 1.0f;
    }
    const float2 safeUv = saturate(uv);
    const float fu = safeUv.x * (float)p.coverageResolution;
    const float fv = safeUv.y * (float)p.coverageResolution;
    uint x0 = (uint)fu;
    uint y0 = (uint)fv;
    uint x1 = min(x0 + 1u, p.coverageResolution);
    uint y1 = min(y0 + 1u, p.coverageResolution);
    const float tx = fu - (float)x0;
    const float ty = fv - (float)y0;
    const uint stride = p.coverageResolution + 1u;
    const uint base = p.liquidCoverageOffset;
    const float c00 = gSandSurfaceHeights[base + y0 * stride + x0];
    const float c10 = gSandSurfaceHeights[base + y0 * stride + x1];
    const float c01 = gSandSurfaceHeights[base + y1 * stride + x0];
    const float c11 = gSandSurfaceHeights[base + y1 * stride + x1];
    return lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty);
}

float HikariWaterHash21(float2 value)
{
    value = frac(value * float2(123.34f, 456.21f));
    value += dot(value, value + 45.32f);
    return frac(value.x * value.y);
}

float HikariWaterValueNoise(float2 value)
{
    const float2 cell = floor(value);
    const float2 local = frac(value);
    const float2 blend = local * local * (3.0f - 2.0f * local);
    const float n00 = HikariWaterHash21(cell);
    const float n10 = HikariWaterHash21(cell + float2(1.0f, 0.0f));
    const float n01 = HikariWaterHash21(cell + float2(0.0f, 1.0f));
    const float n11 = HikariWaterHash21(cell + float2(1.0f, 1.0f));
    return lerp(
        lerp(n00, n10, blend.x),
        lerp(n01, n11, blend.x),
        blend.y);
}

float HikariWaterLayeredNoise(float2 localXZ, float time)
{
    float noise = 0.0f;
    noise += HikariWaterValueNoise(
        localXZ * 0.34f + float2(0.08f, 0.05f) * time);
    noise += HikariWaterValueNoise(
        localXZ * 0.63f + 13.7f + float2(-0.06f, 0.09f) * time);
    noise += HikariWaterValueNoise(
        localXZ * 1.17f + 29.3f + float2(0.10f, -0.04f) * time);
    return noise * (1.0f / 3.0f);
}

float3 HikariWaterResolveMicroNormal(
    float3 geometricNormal,
    float4 tangentWS,
    float2 localXZ,
    float time,
    float strength,
    bool anime)
{
    const float3 n = HikariSafeNormalize(
        geometricNormal,
        float3(0.0f, 1.0f, 0.0f));
    const float3 tangentFallbackAxis = abs(n.y) < 0.999f
        ? float3(0.0f, 1.0f, 0.0f)
        : float3(1.0f, 0.0f, 0.0f);
    const float3 tangentFallback = HikariSafeNormalize(
        cross(tangentFallbackAxis, n),
        float3(1.0f, 0.0f, 0.0f));
    float3 tangent = tangentWS.xyz - n * dot(tangentWS.xyz, n);
    tangent = HikariSafeNormalize(
        length(tangent) > 1.0e-5f
            ? tangent
            : tangentFallback,
        tangentFallback);
    const float tangentSign =
        HikariFiniteOr(tangentWS.w, 1.0f) < 0.0f ? -1.0f : 1.0f;
    const float3 bitangent = HikariSafeNormalize(
        cross(n, tangent) * tangentSign,
        cross(n, tangentFallback) * tangentSign);

    const float2 direction0 = normalize(float2(0.82f, 0.57f));
    const float2 direction1 = normalize(float2(-0.41f, 0.91f));
    const float phase0 =
        dot(localXZ, direction0) * (anime ? 1.25f : 2.10f) -
        time * 1.15f;
    const float phase1 =
        dot(localXZ, direction1) * (anime ? 2.35f : 4.25f) -
        time * 1.73f;
    const float2 gradient =
        direction0 * cos(phase0) * 0.55f +
        direction1 * cos(phase1) * 0.28f;
    const float amplitude = strength * (anime ? 0.12f : 0.20f);
    return HikariSafeNormalize(
        n - tangent * gradient.x * amplitude -
        bitangent * gradient.y * amplitude,
        n);
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
    out float opticalDistance,
    out float refractionConfidence)
{
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
    opticalDistance = lerp(
        HikariFiniteOr(centerDepth.opticalDistance, 64.0f),
        HikariFiniteOr(candidateOpticalDistance, 0.0f),
        refractionConfidence);
    opticalDistance = max(HikariFiniteOr(opticalDistance, 64.0f), 0.0f);
    const float3 sampledCenterColor =
        gSceneColorTex.Sample(gLinearClamp, screenUv).rgb;
    const float3 centerColor = max(
        HikariFiniteOr(sampledCenterColor, 0.0f.xxx),
        0.0f.xxx);
    const float3 sampledCandidateColor = candidateValid
        ? gSceneColorTex.Sample(gLinearClamp, candidateUv).rgb
        : centerColor;
    const float3 candidateColor = candidateValid
        ? max(
            HikariFiniteOr(sampledCandidateColor, centerColor),
            0.0f.xxx)
        : centerColor;
    return HikariFiniteOr(
        lerp(centerColor, candidateColor, refractionConfidence),
        centerColor);
}

float3 EvaluateWaterLocalLights(
    HikariPbrShadingContext context,
    float3 worldPosition,
    float distanceToCamera,
    float2 pixelPosition)
{
    return AccumulateLocalLightsPbr(
        context,
        worldPosition,
        distanceToCamera,
        pixelPosition);
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
            HikariWaterHash21(cellId + float2(11.0f, 37.0f)),
            HikariWaterHash21(cellId + float2(53.0f, 19.0f))) -
        0.5f.xx) * 0.48f;
    const float angle = HikariWaterHash21(
        cellId + float2(29.0f, 71.0f)) * 6.28318530718f;
    const float phase = HikariWaterHash21(
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

float3 HikariWaterSoftCompressHdr(
    float3 radiance,
    float knee)
{
    const float3 safeRadiance = max(
        HikariFiniteOr(radiance, 0.0f.xxx),
        0.0f.xxx);
    const float safeKnee = max(
        HikariFiniteOr(knee, 0.30f),
        0.01f);
    return safeRadiance / (1.0f.xxx + safeRadiance * safeKnee);
}

float3 HikariWaterApplyReflectionBudget(
    float3 radiance,
    float reflectionStrength)
{
    const float gain = clamp(HikariFiniteOr(
        reflectionStrength,
        0.62f), 0.0f, 2.0f);
    const float3 safeRadiance = max(
        HikariFiniteOr(radiance, 0.0f.xxx),
        0.0f.xxx);
    const float peak = max(
        max(safeRadiance.r, safeRadiance.g),
        safeRadiance.b);
    // 有理 knee により全 provider と glint lobe を同じ scene-linear 予算へ
    // 収め、低 radiance の形は過度に平坦化しない。
    // At gain=1 the old 0.8 asymptote sat below the default 0.8 bloom
    // threshold, so even a strong summer sun could never produce a readable
    // HDR water highlight. Keep the provider sum bounded, but leave enough
    // scene-linear headroom for tone mapping and bloom to preserve the lobe.
    const float budget = max(0.10f, gain * 1.50f);
    const float budgetScale = budget / max(budget + peak, 1.0e-4f);
    return safeRadiance * budgetScale * gain;
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
    float3 normalWS = HikariWaterResolveMicroNormal(
        geometricNormal,
        input.tangentWS,
        localXZ,
        animationTime,
        p.microWaveStrength,
        anime);
    const float3 toCamera = gCameraPos.xyz - input.worldPosWS;
    const float distanceToCamera = max(HikariFiniteOr(
        length(toCamera),
        1.0e-4f), 1.0e-4f);
    const float3 view = HikariSafeNormalize(toCamera, geometricNormal);
    const float3 light = HikariSafeNormalize(
        -gDirectionalDir.xyz,
        float3(0.0f, 1.0f, 0.0f));
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
        opticalDistance,
        refractionConfidence);

    const float3 transmittance = saturate(HikariFiniteOr(
        HikariReferenceTransmittance(
            p.depthTransmittance,
            p.absorptionDistance,
            opticalDistance),
        1.0f.xxx));
    const float3 safeScatteringColor = max(
        HikariFiniteOr(p.scatteringColor, 0.0f.xxx),
        0.0015f.xxx);
    const float3 transmittedBase = HikariFiniteOr(
        sceneRefraction * transmittance +
            safeScatteringColor * (1.0f.xxx - transmittance),
        safeScatteringColor);
    // Beer-Lambert remains the authored transport term, while the bounded
    // scattering floor prevents a valid deep-water sample from collapsing to
    // black when refraction and exposure are both low.
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
        HikariBuildPbrShadingContext(
            0.0f.xxx,
            0.0f,
            roughness,
            1.0f.xxx,
            1.0f,
            normalWS,
            view);
    reflectionContext.specularF0 = waterF0.xxx;
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
        HikariEvaluateDirectPbr(
            reflectionContext,
            light,
            gDirectionalColor.rgb,
            gDirectionalIntensity) *
            max(p.directSunStrength, 0.0f) * directShadow,
        0.0f.xxx);
    const float3 localReflection = HikariFiniteOr(
        EvaluateWaterLocalLights(
            reflectionContext,
            input.worldPosWS,
            distanceToCamera,
            input.position.xy),
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
    const float approximateFresnel = saturate(HikariFiniteOr(
        HikariFresnelSchlick(
            saturate(dot(normalWS, view)),
            waterF0.xxx).x,
        waterF0));
    const float fresnel = realistic
        ? HikariWaterExactDielectricFresnel(
            dot(normalWS, view),
            1.0f,
            1.333f)
        : approximateFresnel;
    HikariScreenSpaceReflectionTraceConfig waterSsrConfig;
    waterSsrConfig.valid =
        gScreenSpaceTraceValid != 0u &&
        gScreenSpaceReflectionParams0.x > 0.0f
            ? 1u
            : 0u;
    waterSsrConfig.maxSteps = (uint)max(
        HikariFiniteOr(gScreenSpaceReflectionParams1.z, 0.0f),
        0.0f);
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
        1.0f);
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
    const float waterSsrReplacementWeight = saturate(HikariFiniteOr(
        waterSsr.confidence * gScreenSpaceReflectionParams0.x,
        0.0f));
    // Ambient IBL already evaluates its own BRDF/Fresnel response through
    // reflectionContext. Only the raw SSR radiance receives the water Fresnel
    // factor before it replaces the IBL provider.
    const float3 providerEnvironmentReflection = max(
        HikariFiniteOr(environmentReflection, 0.0f.xxx),
        0.0f.xxx);
    const float3 resolvedEnvironmentReflection = HikariFiniteOr(
        lerp(
            providerEnvironmentReflection,
            HikariFiniteOr(
                waterSsr.radiance,
                providerEnvironmentReflection) * fresnel,
            waterSsrReplacementWeight),
        providerEnvironmentReflection);

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
        ? HikariWaterLayeredNoise(localXZ, animationTime)
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
        localReflection * (realistic ? 1.0f : 0.35f),
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
            0.68f + depthStep1 * 0.12f +
            depthStep2 * 0.08f + compressionStep * 0.06f);
        const float graphicWeight = lerp(
            deepGraphicWeight,
            p.shallowGraphicWeight,
            shallowMask);
        color = lerp(transmissionTerm, graphicBody, graphicWeight);

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
        const float3 sunRadiance = HikariWaterSoftCompressHdr(
            HikariFiniteOr(
                gDirectionalColor.rgb * max(
                    HikariFiniteOr(gDirectionalIntensity, 0.0f),
                    0.0f),
                0.0f.xxx),
            0.28f);
        const float glintMask = saturate(
            sunRibbon * 0.36f + sparkle);
        // Anime sparkle is a bounded directional-sun lobe, never an
        // unconditional emissive lift. Its energy joins the same reflection
        // budget as sky/SSR and local/direct PBR.
        const float3 animeSunGlint = sunRadiance *
            (glintMask * max(p.sunGlintStrength, 0.0f) *
                max(p.directSunStrength, 0.0f) * directShadow * 0.45f);
        const float environmentWeight = lerp(
            0.52f + 0.28f * fresnel,
            1.0f,
            shallowMask);
        const float3 reflectionEnergy =
            environmentTerm * environmentWeight +
            directionalReflection * 0.72f +
            localTerm * 0.35f +
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
        color = lerp(
            color,
            crestRibbonColor,
            crestRibbon * (0.34f + 0.20f * sunStep));

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
        const float3 reflectionEnergy =
            environmentTerm + directionalReflection + localTerm;
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
    const float3 resolvedColor = colorFitsHdrTarget
        ? max(color, 0.0f.xxx)
        : summerFallback;

    // SceneColor は上で明示的に合成済み。alpha=1 の premultiplied blend で
    // destination の二重取り込みを避ける。
    return float4(resolvedColor, 1.0f);
}

#endif
