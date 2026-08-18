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
    float compression)
{
    // Grid は local space に固定し、hash は中心・角度・位相だけを変える。
    // 時間を cell ownership に混ぜず、矩形単位の点滅を防ぐ。
    const float2 gridPosition = localXZ * 2.35f;
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

    const float brush = HikariWaterSdfCoverage(
        HikariWaterCapsuleSdf(markPosition, 0.14f, 0.018f));
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

    const float phaseWave =
        0.5f + 0.5f * cos(animationTime * 1.35f + phase);
    const float phaseGate = smoothstep(0.58f, 0.92f, phaseWave);
    const float sunGate = smoothstep(0.76f, 0.96f, sunAlignment);
    const float crestGate = smoothstep(0.16f, 0.62f, compression);
    return mark * phaseGate * sunGate * crestGate;
}

float2 HikariWaterSafeNormalize2(
    float2 value,
    float2 fallback)
{
    const float lengthSquared = dot(value, value);
    return HikariIsFiniteFloat(lengthSquared) &&
            lengthSquared > 1.0e-12f
        ? value * rsqrt(lengthSquared)
        : fallback;
}

float HikariWaterSparklingSurfaceFootprint(
    float2 localXZ,
    float2 worldXZ,
    float scale)
{
    const float2 localDerivative = fwidth(localXZ * scale);
    const float2 worldDerivative = fwidth(worldXZ * scale);
    return max(
        max(length(localDerivative), length(worldDerivative)),
        1.0e-4f);
}

float HikariWaterSparklingRibbonMask(
    float2 localXZ,
    float2 worldXZ,
    float animationTime,
    float compression,
    float highlightAlignment,
    float ribbonWidth,
    float distanceToCamera)
{
    const float2 localAnchor = HikariFiniteOr(localXZ, 0.0f.xx);
    const float2 worldAnchor = HikariFiniteOr(worldXZ, 0.0f.xx);
    const float2 surfaceDirection = HikariWaterSafeNormalize2(
        float2(0.82f, 0.57f),
        float2(1.0f, 0.0f));
    const float2 crossDirection = float2(
        -surfaceDirection.y,
        surfaceDirection.x);
    // 低周波キャリアは object/world の両方へ固定する。カメラへロックせず、
    // screen-space の固定格子にもならない。
    const float ribbonPhase =
        dot(localAnchor, surfaceDirection * 0.74f) +
        dot(worldAnchor, crossDirection * 0.045f) +
        animationTime * 0.24f +
        compression * 3.15f +
        highlightAlignment * 2.60f;
    const float carrier =
        0.5f + 0.5f * cos(ribbonPhase);
    const float safeRibbonWidth = clamp(
        HikariFiniteOr(ribbonWidth, 0.18f),
        0.02f,
        0.80f);
    const float carrierWidth = max(
        safeRibbonWidth,
        HikariWaterSparklingSurfaceFootprint(
            localAnchor,
            worldAnchor,
            0.74f) * 1.5f);
    const float ribbonCarrier = smoothstep(
        0.50f - carrierWidth,
        0.50f + carrierWidth,
        carrier);
    const float compressionBand = smoothstep(
        0.08f,
        0.62f,
        compression);
    const float alignmentWidth = max(
        safeRibbonWidth,
        fwidth(highlightAlignment) * 2.0f);
    const float alignmentBand = smoothstep(
        0.78f - alignmentWidth,
        0.94f + alignmentWidth,
        highlightAlignment);
    const float distanceDensity = lerp(
        0.42f,
        1.0f,
        saturate(1.0f - distanceToCamera / 240.0f));
    const float breakup = 0.68f + 0.32f * (
        0.5f + 0.5f * sin(
            ribbonPhase * 0.43f - animationTime * 0.11f));
    return saturate(
        ribbonCarrier *
        lerp(0.35f, 1.0f, compressionBand) *
        lerp(0.30f, 1.0f, alignmentBand) *
        breakup *
        distanceDensity);
}

float HikariWaterSparklingDashMask(
    float2 localXZ,
    float2 worldXZ,
    float animationTime,
    float glintDensity,
    float glintFlowSpeed,
    float distanceToCamera)
{
    const float density = saturate(HikariFiniteOr(
        glintDensity,
        0.48f));
    const float flowSpeed = max(HikariFiniteOr(
        glintFlowSpeed,
        0.72f), 0.0f);
    const float distanceLod = saturate(
        1.0f - distanceToCamera / 210.0f);
    const float effectiveDensity = density * lerp(
        0.24f,
        1.0f,
        distanceLod);
    const float2 localAnchor = HikariFiniteOr(localXZ, 0.0f.xx);
    const float2 worldAnchor = HikariFiniteOr(worldXZ, 0.0f.xx);
    const float2 anchor =
        localAnchor * 0.46f +
        worldAnchor * 0.032f;
    const float flowTime = animationTime * flowSpeed;
    const float2 field0 = float2(
        sin(anchor.y * 0.77f + flowTime * 0.61f),
        cos(anchor.x * 0.53f - flowTime * 0.47f));
    const float2 field1 = float2(
        cos(anchor.y * 0.41f - flowTime * 0.38f + anchor.x * 0.16f),
        sin(anchor.x * 0.68f + flowTime * 0.52f - anchor.y * 0.21f));
    const float2 q0 = anchor * (0.78f + effectiveDensity * 1.65f) +
        field0 * 0.56f +
        flowTime * float2(0.085f, -0.052f);
    const float2 q1 = anchor * (1.12f + effectiveDensity * 2.20f) +
        field1 * 0.42f +
        flowTime * float2(-0.061f, 0.073f);
    const float2 direction0 = HikariWaterSafeNormalize2(
        float2(0.91f, 0.42f),
        float2(1.0f, 0.0f));
    const float2 direction1 = HikariWaterSafeNormalize2(
        float2(-0.35f, 0.94f),
        float2(0.0f, 1.0f));
    const float2 perpendicular0 = float2(
        -direction0.y,
        direction0.x);
    const float2 perpendicular1 = float2(
        -direction1.y,
        direction1.x);
    const float dashWave0 = 0.5f + 0.5f * cos(
        dot(q0, direction0) * 3.45f +
        flowTime * 0.68f);
    const float dashWave1 = 0.5f + 0.5f * cos(
        dot(q1, direction1) * 4.10f -
        flowTime * 0.51f);
    const float laneWave0 = 0.5f + 0.5f * cos(
        dot(q0, perpendicular0) * 1.35f +
        sin(dot(q0, direction1) * 0.71f));
    const float laneWave1 = 0.5f + 0.5f * cos(
        dot(q1, perpendicular1) * 1.18f +
        cos(dot(q1, direction0) * 0.63f));
    const float clusterField0 = HikariWaterValueNoise(
        q0 * 0.34f + field1 * 0.19f);
    const float clusterField1 = HikariWaterValueNoise(
        q1 * 0.27f - field0 * 0.23f + 19.0f);
    const float dash0 =
        HikariWaterSmoothBand(dashWave0, 0.76f) *
        HikariWaterSmoothBand(laneWave0, 0.58f) *
        HikariWaterSmoothBand(clusterField0, 0.57f);
    const float dash1 =
        HikariWaterSmoothBand(dashWave1, 0.80f) *
        HikariWaterSmoothBand(laneWave1, 0.54f) *
        HikariWaterSmoothBand(clusterField1, 0.62f);
    const float footprint = HikariWaterSparklingSurfaceFootprint(
        localAnchor,
        worldAnchor,
        1.0f + effectiveDensity * 1.8f);
    const float pixelDensity = saturate(
        1.0f - footprint * (1.75f + effectiveDensity * 2.75f));
    return saturate(
        (dash0 * 0.62f + dash1 * 0.38f) *
        effectiveDensity *
        lerp(0.20f, 1.0f, distanceLod) *
        pixelDensity);
}

float HikariWaterSparklingStarMask(
    float2 localXZ,
    float2 worldXZ,
    float animationTime,
    float glintDensity,
    float glintFlowSpeed,
    float highlightAlignment,
    float distanceToCamera)
{
    const float density = saturate(HikariFiniteOr(
        glintDensity,
        0.48f));
    const float flowSpeed = max(HikariFiniteOr(
        glintFlowSpeed,
        0.72f), 0.0f);
    const float distanceLod = saturate(
        1.0f - distanceToCamera / 260.0f);
    const float2 localAnchor = HikariFiniteOr(localXZ, 0.0f.xx);
    const float2 worldAnchor = HikariFiniteOr(worldXZ, 0.0f.xx);
    const float starScale = lerp(
        1.35f,
        3.80f,
        density * lerp(0.30f, 1.0f, distanceLod));
    const float2 starCoordinate =
        (localAnchor * 0.31f + worldAnchor * 0.055f) * starScale;
    const float2 cellId = floor(starCoordinate);
    float2 cellPosition = frac(starCoordinate) - 0.5f.xx;
    const float seed = HikariWaterHash21(
        cellId + float2(17.0f, 43.0f));
    const float occupancy = HikariWaterHash21(
        cellId + float2(61.0f, 29.0f));
    const float occupancyThreshold = lerp(
        0.992f,
        0.80f,
        density);
    const float occupancyAa = max(
        fwidth(occupancy) * 1.5f,
        HikariWaterSparklingSurfaceFootprint(
            localAnchor,
            worldAnchor,
            starScale) * 0.18f);
    const float occupied = smoothstep(
        occupancyThreshold - occupancyAa,
        occupancyThreshold + occupancyAa,
        occupancy);
    const float2 center = (
        float2(
            HikariWaterHash21(cellId + float2(7.0f, 89.0f)),
            HikariWaterHash21(cellId + float2(97.0f, 13.0f))) -
        0.5f.xx) * 0.48f;
    cellPosition -= center;
    const float angle = HikariWaterHash21(
        cellId + float2(31.0f, 71.0f)) * 6.28318530718f;
    cellPosition = HikariWaterRotate2D(cellPosition, angle);
    const float2 starDrift = 0.055f * float2(
        sin(animationTime * (0.17f + flowSpeed * 0.03f) + seed * 6.283f),
        cos(animationTime * (0.13f + flowSpeed * 0.021f) + seed * 4.713f));
    cellPosition += starDrift;
    const float starFootprint = max(
        HikariWaterSparklingSurfaceFootprint(
            localAnchor,
            worldAnchor,
            starScale),
        0.0015f);
    const float starRadius = 0.022f + starFootprint * 1.85f;
    const float starRay0 = HikariWaterSdfCoverage(
        HikariWaterCapsuleSdf(
            cellPosition,
            0.090f,
            starRadius));
    const float starRay1 = HikariWaterSdfCoverage(
        HikariWaterCapsuleSdf(
            HikariWaterRotate2D(cellPosition, 1.57079632679f),
            0.070f,
            starRadius * 0.82f));
    const float starCore = HikariWaterSdfCoverage(
        length(cellPosition) - starRadius * 1.25f);
    const float starShape = max(
        max(starRay0, starRay1) * 0.88f,
        starCore);
    const float phase = animationTime * (
        0.31f + flowSpeed * 0.045f) + seed * 6.28318530718f;
    const float twinkle = smoothstep(
        0.66f,
        0.96f,
        0.5f + 0.5f * cos(phase));
    const float alignment = smoothstep(
        0.80f,
        0.97f,
        highlightAlignment);
    return saturate(
        starShape *
        occupied *
        twinkle *
        alignment *
        density *
        lerp(0.12f, 1.0f, distanceLod));
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
    const float budget = max(0.05f, gain * 0.80f);
    const float budgetScale = budget / max(budget + peak, 1.0e-4f);
    return safeRadiance * budgetScale * gain;
}

float3 HikariWaterApplyExactFresnelCorrection(
    float3 providerRadiance,
    float exactFresnel,
    float providerFresnel)
{
    const float3 safeRadiance = max(
        HikariFiniteOr(providerRadiance, 0.0f.xxx),
        0.0f.xxx);
    const float correction = saturate(HikariFiniteOr(
        exactFresnel,
        0.02037f)) / max(
            HikariFiniteOr(providerFresnel, 0.02037f),
            0.02037f);
    return safeRadiance * min(correction, 4.0f);
}

float3 HikariWaterApplySpectralReflectionBudget(
    float3 radiance,
    float reflectionStrength)
{
    const float gain = clamp(HikariFiniteOr(
        reflectionStrength,
        0.62f),
        0.0f,
        2.0f);
    const float3 safeRadiance = max(
        HikariFiniteOr(radiance, 0.0f.xxx),
        0.0f.xxx);
    const float peak = max(
        max(safeRadiance.r, safeRadiance.g),
        safeRadiance.b);
    // Keep low/medium sky contrast intact. Only extreme HDR outliers receive
    // a soft knee; the old all-range rational budget made the sky look like a
    // uniformly tinted plastic sheet.
    const float softKnee = 1.0f / (
        1.0f + max(peak - 3.0f, 0.0f) * 0.10f);
    return min(
        safeRadiance * (gain * softKnee),
        32.0f.xxx);
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
    const bool animeSparkling =
        p.shadingModel >= 1.5f &&
        p.shadingModel < 2.5f;
    const bool realisticSpectral =
        p.shadingModel >= 2.5f &&
        p.shadingModel < 3.5f;
    const bool spectralActive = realisticSpectral &&
        (p.shortWaveStrength > 1.0e-4f ||
            p.capillaryStrength > 1.0e-4f);
    const bool animeLike = anime || animeSparkling;
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
    // RealisticSpectralSummer does not reuse the two-carrier graphic micro
    // normal. Its only short-scale normal source is the spectral seam below;
    // retaining the fixed carriers here is what turns the water into parallel
    // satin stripes when the FFT provider is unavailable.
    float3 normalWS = realisticSpectral
        ? geometricNormal
        : HikariWaterResolveMicroNormal(
            geometricNormal,
            input.tangentWS,
            localXZ,
            animationTime,
            p.microWaveStrength,
            animeLike);
    const float3 macroNormalWS = normalWS;
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
    HikariWaterSpectralSlopeResult spectralSlope;
    spectralSlope.slope = 0.0f.xx;
    spectralSlope.variance = 0.0f;
    float spectralSlopeWeight = 0.0f;
    if (spectralActive)
    {
        spectralSlope = HikariWaterResolveSpectralSlope(
            worldXZ,
            animationTime,
            p.windDirectionRadians,
            p.windSpeed,
            p.shortWaveStrength,
            p.capillaryStrength);
        spectralSlopeWeight = HikariWaterSpectralResolvedSlopeWeight(
            worldXZ,
            distanceToCamera);
        normalWS = HikariWaterSpectralNormalFromSlope(
            normalWS,
            spectralSlope.slope,
            spectralSlopeWeight);
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
    const float sparklingDistortionStrength = animeSparkling
        ? deepDistortionStrength * lerp(
            1.0f,
            0.30f,
            shallowMask)
        : distortionStrength;
    const float3 sceneRefraction = HikariSampleWaterRefraction(
        input,
        normalWS,
        screenUv,
        centerDepth,
        sparklingDistortionStrength,
        opticalDistance,
        refractionConfidence);

    const float3 transmittance = saturate(HikariFiniteOr(
        HikariReferenceTransmittance(
            p.depthTransmittance,
            p.absorptionDistance,
            opticalDistance),
        1.0f.xxx));
    const float3 transmittedColor = HikariFiniteOr(
        sceneRefraction * transmittance +
            p.scatteringColor * (1.0f.xxx - transmittance),
        p.scatteringColor);
    const float3 spectralScatteringColor = min(
        max(HikariFiniteOr(p.scatteringColor, 0.0f.xxx), 0.0f.xxx),
        float3(0.075f, 0.16f, 0.15f));
    const float3 spectralTransmissionFloor = max(
        spectralScatteringColor * 0.10f,
        float3(0.0025f, 0.018f, 0.022f));
    const float3 spectralTransmissionColor = max(
        HikariFiniteOr(
            max(sceneRefraction, 0.0f.xxx) * transmittance +
                spectralScatteringColor *
                    (1.0f.xxx - transmittance) * 0.24f,
            0.0f.xxx),
        spectralTransmissionFloor);

    const float waterF0 = HikariDielectricF0FromIor(1.0f, 1.333f);
    float roughness = animeLike
        ? max(p.surfaceRoughness, 0.16f)
        : p.surfaceRoughness;
    if (spectralActive)
    {
        // 解像できない短坡度は pixel footprint / distance とともに
        // roughness へ移し、遠景での aliasing と過剰な鋭さを抑える。
        roughness = HikariWaterSpectralRoughnessFromFootprint(
            roughness,
            spectralSlope.variance,
            worldXZ,
            distanceToCamera,
            spectralSlopeWeight);
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
    const float shadowFactor = saturate(HikariFiniteOr(
        SampleDirectionalShadow(
            input.worldPosWS,
            geometricNormal,
            HikariForwardVertexMetaReceiveShadow(input.forwardMeta)),
        1.0f));
    float3 directionalReflection = 0.0f.xxx;
    if (!spectralActive)
    {
        directionalReflection = HikariFiniteOr(
            HikariEvaluateDirectPbr(
                reflectionContext,
                light,
                gDirectionalColor.rgb,
                gDirectionalIntensity) * shadowFactor,
            0.0f.xxx);
    }
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
    const float fresnel = spectralActive
        ? HikariWaterExactDielectricFresnel(
            dot(normalWS, view),
            1.0f,
            1.333f)
        : approximateFresnel;
    const float providerFresnel = saturate(HikariFiniteOr(
        HikariFresnelSchlickRoughness(
            saturate(dot(normalWS, view)),
            waterF0.xxx,
            roughness).x,
        waterF0));
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
    const float3 providerEnvironmentReflection = spectralActive
        ? HikariWaterApplyExactFresnelCorrection(
            environmentReflection,
            fresnel,
            providerFresnel)
        : environmentReflection;
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

    const float layeredNoise = animeLike && !spectralActive
        ? HikariWaterLayeredNoise(
            localXZ,
            animationTime)
        : 0.5f;
    const float shoreFoam = centerDepth.valid
        ? 1.0f - smoothstep(0.18f, 1.65f, columnDepth)
        : 0.0f;
    const float crestFoam = smoothstep(
        animeLike ? 0.18f : 0.30f,
        animeLike ? 0.62f : 0.78f,
        compression) *
        (spectralActive
            ? 0.34f
            : smoothstep(0.42f, 0.78f, layeredNoise));
    const float foam = saturate(
        (shoreFoam * (animeLike ? 0.80f : 0.58f) + crestFoam) *
        p.foamAmount);

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
            HikariWaterSmoothBand(sunFacing, 0.32f) * shadowFactor;
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
        graphicBody = lerp(
            graphicBody,
            grazingSummer,
            viewStep * 0.30f);
        graphicBody = lerp(
            graphicBody,
            sunlitSummer,
            sunStep * 0.18f);
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
        color = lerp(
            transmittedColor,
            graphicBody,
            graphicWeight);

        const float3 halfVector = HikariSafeNormalize(
            light + view,
            normalWS);
        const float highlightAlignment =
            saturate(dot(normalWS, halfVector));
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
            compression);
        const float crestRibbon = saturate(
            HikariWaterSmoothBand(compression, 0.27f) -
            HikariWaterSmoothBand(compression, 0.58f));
        const float3 summerSun = HikariFiniteOr(
            gDirectionalColor.rgb * max(
                HikariFiniteOr(gDirectionalIntensity, 0.0f),
                0.0f),
            0.0f.xxx);
        color += summerSun *
            (sunRibbon * 0.12f + sparkle * 0.58f) * shadowFactor;
        const float environmentReflectionWeight = lerp(
            0.42f + 0.48f * fresnel,
            1.0f,
            shallowMask);
        color += resolvedEnvironmentReflection *
            environmentReflectionWeight;
        color += (directionalReflection + localReflection) * 0.35f;
        const float3 crestRibbonColor = lerp(
            shallowSummer,
            float3(0.16f, 0.84f, 0.88f),
            0.62f);
        color = lerp(
            color,
            crestRibbonColor,
            crestRibbon * (0.34f + 0.20f * sunStep));

        const float foamCoverage =
            HikariWaterSmoothBand(foam, 0.08f);
        const float foamCore =
            HikariWaterSmoothBand(foam, 0.43f);
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
    else if (animeSparkling)
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
            HikariWaterSmoothBand(sunFacing, 0.32f) * shadowFactor;
        const float compressionStep =
            HikariWaterSmoothBand(compression, 0.22f);
        const float3 shallowSummer = max(
            p.scatteringColor,
            float3(0.025f, 0.24f, 0.32f));
        const float3 deepSummer = max(
            p.depthTransmittance,
            float3(0.015f, 0.09f, 0.19f));
        const float3 middleSummer = lerp(
            shallowSummer,
            deepSummer,
            0.44f) + float3(0.0f, 0.045f, 0.062f);
        const float3 abyssSummer = deepSummer *
            float3(0.56f, 0.70f, 0.88f);
        float3 sparklingBody = lerp(
            shallowSummer,
            middleSummer,
            depthStep0);
        sparklingBody = lerp(
            sparklingBody,
            deepSummer,
            depthStep1);
        sparklingBody = lerp(
            sparklingBody,
            abyssSummer,
            depthStep2);
        sparklingBody = lerp(
            sparklingBody,
            lerp(deepSummer, float3(0.035f, 0.31f, 0.45f), 0.68f),
            viewStep * 0.26f);
        sparklingBody = lerp(
            sparklingBody,
            lerp(shallowSummer, float3(0.12f, 0.70f, 0.72f), 0.50f),
            sunStep * 0.16f);
        sparklingBody = lerp(
            sparklingBody,
            lerp(shallowSummer, float3(0.10f, 0.76f, 0.82f), 0.62f),
            compressionStep * 0.22f);
        const float graphicWeight = lerp(
            saturate(0.66f + depthStep1 * 0.13f +
                depthStep2 * 0.08f + compressionStep * 0.06f),
            p.shallowGraphicWeight,
            shallowMask);
        color = lerp(
            transmittedColor,
            sparklingBody,
            graphicWeight);

        const float3 halfVector = HikariSafeNormalize(
            light + view,
            normalWS);
        const float highlightAlignment = saturate(dot(
            normalWS,
            halfVector));
        const float ribbonMask = HikariWaterSparklingRibbonMask(
            localXZ,
            worldXZ,
            animationTime * 0.84f,
            compression,
            highlightAlignment,
            p.ribbonWidth,
            distanceToCamera);
        const float dashMask = HikariWaterSparklingDashMask(
            localXZ,
            worldXZ,
            animationTime * 1.07f,
            p.glintDensity,
            p.glintFlowSpeed,
            distanceToCamera);
        const float starMask = HikariWaterSparklingStarMask(
            localXZ,
            worldXZ,
            animationTime * 0.61f,
            p.glintDensity,
            p.glintFlowSpeed,
            highlightAlignment,
            distanceToCamera);
        const float sunGlintStrength = clamp(HikariFiniteOr(
            p.sunGlintStrength,
            0.78f), 0.0f, 2.0f);
        const float glintEnergy = saturate(
            ribbonMask * 0.72f +
            dashMask * 0.52f +
            starMask * 0.96f);
        const float3 sunRadiance = HikariWaterSoftCompressHdr(
            HikariFiniteOr(
                gDirectionalColor.rgb * max(
                    HikariFiniteOr(gDirectionalIntensity, 0.0f),
                    0.0f),
                0.0f.xxx),
            0.28f);

        // SSR は置換 provider として扱う。共有 trace の confidence が
        // hit を棄却・欠損した場合だけ Probe/Sky を残す。
        const float3 probeSkyProvider = max(
            HikariFiniteOr(environmentReflection, 0.0f.xxx),
            0.0f.xxx);
        const float3 ssrProvider = max(
            HikariFiniteOr(waterSsr.radiance, 0.0f.xxx),
            0.0f.xxx) * fresnel;
        const float3 resolvedSparklingProvider = lerp(
            probeSkyProvider,
            ssrProvider,
            waterSsrReplacementWeight);
        const float3 softDirectionalReflection =
            HikariWaterSoftCompressHdr(
                directionalReflection,
                0.32f);
        const float3 softLocalReflection = HikariWaterSoftCompressHdr(
            localReflection,
            0.42f);
        const float3 boundedSunGlint = sunRadiance *
            (sunGlintStrength * glintEnergy * shadowFactor);
        const float3 reflectionEnergy =
            resolvedSparklingProvider +
            softDirectionalReflection *
                (0.20f + sunGlintStrength * 0.24f) +
            softLocalReflection * 0.22f +
            boundedSunGlint;
        // Probe/Sky、SSR、directional/local PBR、3 層の glint はすべて
        // scene-linear の単一有限予算を共有する。
        color += HikariWaterApplyReflectionBudget(
            reflectionEnergy,
            p.reflectionStrength);

        const float crestRibbon = saturate(
            HikariWaterSmoothBand(compression, 0.27f) -
            HikariWaterSmoothBand(compression, 0.58f));
        const float3 crestRibbonColor = lerp(
            shallowSummer,
            float3(0.16f, 0.82f, 0.88f),
            0.60f);
        color = lerp(
            color,
            crestRibbonColor,
            crestRibbon * (0.18f + 0.14f * sunStep));

        const float foamCoverage =
            HikariWaterSmoothBand(foam, 0.08f);
        const float foamCore =
            HikariWaterSmoothBand(foam, 0.43f);
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
            saturate(foamEdge * 0.84f + foamCore * 0.16f));
        color = lerp(color, foamWarmCore, foamCore * 0.82f);
    }
    else if (spectralActive)
    {
        // RealisticSpectralSummer keeps the dark participating water body
        // separate from the high-contrast sky provider. No procedural noise
        // term is allowed to manufacture bright reflection marks here.
        color = spectralTransmissionColor * (1.0f - fresnel);

        // SSR is the primary environment provider; its miss falls back to
        // the existing probe/sky environment without adding another direct
        // light contribution.
        const float3 spectralEnvironment = max(
            HikariFiniteOr(resolvedEnvironmentReflection, 0.0f.xxx),
            0.0f.xxx);
        const float3 spectralLocal =
            HikariWaterApplyExactFresnelCorrection(
                localReflection,
                fresnel,
                providerFresnel) * 0.18f;
        const float spectralGlitter =
            HikariWaterEvaluateCoxMunkSpectralGlitter(
                macroNormalWS,
                normalWS,
                view,
                light,
                spectralSlope.slope,
                p.windDirectionRadians,
                p.windSpeed,
                p.shortWaveStrength,
                p.capillaryStrength,
                p.glintDensity,
                p.sunGlintStrength,
                roughness,
                spectralSlopeWeight);
        const float3 sunRadiance = HikariWaterSoftCompressHdr(
            HikariFiniteOr(
                gDirectionalColor.rgb * max(
                    HikariFiniteOr(gDirectionalIntensity, 0.0f),
                    0.0f),
                0.0f.xxx),
            0.24f);
        const float3 boundedSunGlint = sunRadiance *
            (spectralGlitter * shadowFactor);
        const float3 reflectionEnergy =
            spectralEnvironment +
            spectralLocal +
            boundedSunGlint;
        color += HikariWaterApplySpectralReflectionBudget(
            reflectionEnergy,
            p.reflectionStrength);

        const float foamLighting = 0.72f +
            0.28f * saturate(dot(geometricNormal, light));
        color = lerp(
            color,
            p.foamColor * foamLighting,
            foam);
    }
    else
    {
        color = transmittedColor * (1.0f - fresnel) +
            resolvedEnvironmentReflection +
            directionalReflection +
            localReflection;
        const float foamLighting = 0.72f +
            0.28f * saturate(dot(geometricNormal, light));
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
        : animeSparkling
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
