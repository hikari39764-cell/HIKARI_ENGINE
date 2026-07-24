#ifndef HIKARI_SURFACE_FEATURE_SHADING_INCLUDED
#define HIKARI_SURFACE_FEATURE_SHADING_INCLUDED

#include "Include/Forward/HIKARI_SurfaceFeatureCoverage.hlsli"

bool HikariObjectDataHasMaterialFx(HikariMeshObjectData objectData)
{
    if (objectData.fxFlags != 0u)
    {
        return true;
    }

    [unroll]
    for (uint i = 0u; i < 8u; ++i)
    {
        if (any(abs(objectData.fxUser[i]) > 0.000001f))
        {
            return true;
        }
    }
    return false;
}

float HikariSurfaceFeatureHash21(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float2 HikariRotateSurfaceFeatureUv(float2 uv, float angle)
{
    const float s = sin(angle);
    const float c = cos(angle);
    return float2(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
}

float2 HikariResolveSurfaceFeatureUv(float3 worldPosition, float3 worldNormal)
{
    const float3 n = abs(worldNormal);
    if (n.y >= n.x && n.y >= n.z)
    {
        return worldPosition.xz;
    }
    if (n.x >= n.z)
    {
        return worldPosition.zy;
    }
    return worldPosition.xy;
}

void HikariResolveSurfaceFeatureTriangle(
    float2 uv,
    float lineWidth,
    out float edgeMask,
    out float fillMask,
    out float triangleSeed,
    out float2 triangleCenterUv)
{
    const float triangleHeight = 0.8660254f;
    const float2 p = float2(uv.x + uv.y * 0.5f, uv.y * triangleHeight);
    const float2 cell = floor(p);
    const float2 f = frac(p);
    const float upper = step(1.0f, f.x + f.y);

    const float3 lowerBary = float3(f.x, f.y, 1.0f - f.x - f.y);
    const float3 upperBary = float3(1.0f - f.x, 1.0f - f.y, f.x + f.y - 1.0f);
    const float3 bary = max(lerp(lowerBary, upperBary, upper), 0.0f.xxx);
    const float edgeDistance = min(min(bary.x, bary.y), bary.z);
    const float aa = max(fwidth(edgeDistance), 0.001f);
    const float width = clamp(lineWidth, 0.002f, 0.24f);
    edgeMask = 1.0f - smoothstep(width, width + aa, edgeDistance);
    fillMask = smoothstep(width * 0.8f, width * 1.85f + aa, edgeDistance);

    const float2 centerP = cell + lerp(
        float2(0.3333333f, 0.3333333f),
        float2(0.6666667f, 0.6666667f),
        upper);
    triangleCenterUv = float2(
        centerP.x - (centerP.y / triangleHeight) * 0.5f,
        centerP.y / triangleHeight);
    triangleSeed = HikariSurfaceFeatureHash21(
        cell + float2(upper * 17.0f, upper * 31.0f));
}

float3 HikariApplyStaticMaterialFx(
    float3 litColor,
    float3 worldPosition,
    float3 geometricNormal,
    float3 shadingNormal,
    float3 viewDirection,
    float4 pixelPosition,
    float timeSeconds,
    HikariMeshObjectData featureData)
{
    const float4 fxUser0 = featureData.fxUser[0];
    const float4 fxUser1 = featureData.fxUser[1];
    const HikariSurfaceFeatureCoverage coverage =
        HikariEvaluateStaticMaterialFxCoverage(
            worldPosition,
            fxUser0,
            fxUser1);

    const float rimStrength = fxUser0.x;
    const float rimPower = max(fxUser0.y, 0.01f);
    const float pulseSpeed = fxUser1.x;
    const float edgeBoost = fxUser1.y;
    const float pulse = 0.5f + 0.5f * sin(timeSeconds * pulseSpeed);
    const float rim = pow(
        1.0f - saturate(dot(shadingNormal, viewDirection)),
        rimPower);
    const float3 rimColor =
        float3(0.15f, 0.75f, 1.0f) *
        rim * rimStrength * (0.75f + pulse * 0.25f);
    const float3 edgeColor =
        float3(0.4f, 0.5f, 1.0f) * coverage.edgeBand * edgeBoost;
    float3 finalColor = litColor * coverage.edge + rimColor + edgeColor;

    const float4 fxUser2 = featureData.fxUser[2];
    const float sceneColorDistortionStrength = fxUser2.x;
    const float sceneColorMix = saturate(fxUser2.y);
    const float sceneColorNoiseScale = max(fxUser2.z, 0.0001f);
    if (sceneColorMix > 0.0001f)
    {
        const float2 screenUv = pixelPosition.xy * gScreenParams.zw;
        const float n0 = HikariSurfaceFeatureNoise3D(
            worldPosition * sceneColorNoiseScale);
        const float n1 = HikariSurfaceFeatureNoise3D(
            worldPosition * sceneColorNoiseScale + float3(13.1f, 7.7f, 3.3f));
        const float2 distortion =
            (float2(n0, n1) * 2.0f - 1.0f) * sceneColorDistortionStrength;
        const float3 sceneColor = gSceneColorTex.Sample(
            gLinearWrap,
            saturate(screenUv + distortion)).rgb;
        finalColor = lerp(finalColor, sceneColor, sceneColorMix);
    }

    const float3 scanOrigin = featureData.fxUser[3].xyz;
    const float scanTime = featureData.fxUser[3].w;
    const float scanRadius = max(featureData.fxUser[4].x, 0.0f);
    const float scanBandWidth = max(featureData.fxUser[4].y, 0.001f);
    const float scanSpeed = max(featureData.fxUser[4].z, 0.001f);
    const float scanIntensity = max(featureData.fxUser[4].w, 0.0f);
    const float4 scanColor = featureData.fxUser[5];
    const float triangleCellSize = max(featureData.fxUser[6].x, 0.001f);
    const float triangleLineWidth = clamp(featureData.fxUser[6].y, 0.001f, 0.45f);
    const float triangleNoiseScale = max(featureData.fxUser[6].z, 0.001f);
    const float triangleFlicker = saturate(featureData.fxUser[6].w);
    const float scanAfterglowStrength = max(featureData.fxUser[7].x, 0.0f);
    const float scanFrontLineStrength = max(featureData.fxUser[7].y, 0.0f);
    const float scanGeometryEdgeStrength = max(featureData.fxUser[7].z, 0.0f);
    const float scanDistortionStrength = max(featureData.fxUser[7].w, 0.0f);

    if (scanIntensity <= 0.0001f || scanRadius <= 0.0001f)
    {
        return finalColor;
    }

    const float2 surfacePlane =
        HikariResolveSurfaceFeatureUv(worldPosition, geometricNormal);
    const float2 originPlane =
        HikariResolveSurfaceFeatureUv(scanOrigin, geometricNormal);
    const float distanceOnSurface = length(surfacePlane - originPlane);
    const float waveRadius = scanTime * scanSpeed;
    const float bandDistance = abs(distanceOnSurface - waveRadius);
    const float waveBand = 1.0f - smoothstep(
        scanBandWidth,
        scanBandWidth * 1.35f,
        bandDistance);
    const float radiusMask = 1.0f - smoothstep(
        scanRadius,
        scanRadius + scanBandWidth,
        distanceOnSurface);
    const float waveAlive = 1.0f - smoothstep(
        scanRadius + scanBandWidth * (0.5f + scanAfterglowStrength * 1.8f),
        scanRadius + scanBandWidth * (1.0f + scanAfterglowStrength * 2.4f),
        waveRadius);

    const float2 triangleDrift = float2(scanTime * 0.055f, -scanTime * 0.032f);
    const float2 randomTile = floor(
        surfacePlane / max(triangleCellSize * 3.25f, 0.001f));
    const float randomA = HikariSurfaceFeatureHash21(
        randomTile + float2(19.17f, 7.31f));
    const float randomB = HikariSurfaceFeatureHash21(
        randomTile + float2(3.91f, 23.53f));
    const float randomAngle = (randomA - 0.5f) * 0.68f;
    const float warpNoiseA = HikariSurfaceFeatureNoise3D(
        float3(surfacePlane * 0.23f, scanTime * 0.16f));
    const float warpNoiseB = HikariSurfaceFeatureNoise3D(
        float3(
            surfacePlane * 0.23f + float2(11.3f, 5.7f),
            scanTime * 0.16f + 4.1f));
    float2 warpedPlane = originPlane + HikariRotateSurfaceFeatureUv(
        surfacePlane - originPlane,
        randomAngle);
    warpedPlane +=
        (float2(randomA, randomB) - 0.5f) * triangleCellSize * 0.52f;
    warpedPlane +=
        (float2(warpNoiseA, warpNoiseB) - 0.5f) * triangleCellSize * 0.34f;

    const float2 scanUv = warpedPlane / triangleCellSize + triangleDrift;
    float triangleEdge = 0.0f;
    float triangleFill = 0.0f;
    float triangleSeed = 0.0f;
    float2 triangleCenterUv = 0.0f.xx;
    HikariResolveSurfaceFeatureTriangle(
        scanUv,
        triangleLineWidth,
        triangleEdge,
        triangleFill,
        triangleSeed,
        triangleCenterUv);

    const float2 triangleCenterPlane =
        (triangleCenterUv - triangleDrift) * triangleCellSize;
    const float triangleDistance = length(triangleCenterPlane - originPlane);
    const float triangleAge = (waveRadius - triangleDistance) / scanBandWidth;
    const float stagger = (triangleSeed - 0.5f) * 0.7f;
    const float detailNoise = HikariSurfaceFeatureNoise3D(
        float3(scanUv * triangleNoiseScale, scanTime * 1.7f));
    const float triangleEnter = smoothstep(
        -0.32f,
        0.24f,
        triangleAge + stagger + detailNoise * 0.12f);
    const float triangleLeave = 1.0f - smoothstep(
        1.25f,
        2.35f,
        triangleAge + stagger * 0.35f);
    const float triangleSweep = triangleEnter * triangleLeave;
    const float hotFront = 1.0f - smoothstep(
        scanBandWidth * 0.08f,
        scanBandWidth * 0.48f,
        abs(triangleDistance - waveRadius + stagger * scanBandWidth * 0.35f));
    const float frontLine = 1.0f - smoothstep(
        scanBandWidth * 0.015f,
        scanBandWidth * 0.13f,
        abs(triangleDistance - waveRadius + stagger * scanBandWidth * 0.18f));
    const float shardBrightness = lerp(0.72f, 1.28f, triangleSeed);
    const float flicker = lerp(
        1.0f,
        0.68f + 0.32f * sin(scanTime * 24.0f + triangleSeed * 28.0f),
        triangleFlicker);

    const float smallTriangleCellSize = max(triangleCellSize * 0.48f, 0.001f);
    const float2 smallDrift = float2(-scanTime * 0.083f, scanTime * 0.061f);
    const float2 smallRandomTile = floor(
        surfacePlane / max(smallTriangleCellSize * 4.0f, 0.001f));
    const float smallRandomA = HikariSurfaceFeatureHash21(
        smallRandomTile + float2(41.9f, 5.2f));
    const float smallRandomB = HikariSurfaceFeatureHash21(
        smallRandomTile + float2(9.6f, 37.4f));
    float2 smallWarpedPlane = surfacePlane;
    smallWarpedPlane +=
        (float2(smallRandomA, smallRandomB) - 0.5f) *
        smallTriangleCellSize * 0.75f;
    smallWarpedPlane +=
        (float2(detailNoise, warpNoiseB) - 0.5f) *
        smallTriangleCellSize * 0.36f;
    const float2 smallScanUv =
        smallWarpedPlane / smallTriangleCellSize + smallDrift;
    float smallTriangleEdge = 0.0f;
    float smallTriangleFill = 0.0f;
    float smallTriangleSeed = 0.0f;
    float2 smallTriangleCenterUv = 0.0f.xx;
    HikariResolveSurfaceFeatureTriangle(
        smallScanUv,
        clamp(triangleLineWidth * 0.78f, 0.001f, 0.45f),
        smallTriangleEdge,
        smallTriangleFill,
        smallTriangleSeed,
        smallTriangleCenterUv);

    const float2 smallTriangleCenterPlane =
        (smallTriangleCenterUv - smallDrift) * smallTriangleCellSize;
    const float smallTriangleDistance = length(
        smallTriangleCenterPlane - originPlane);
    const float smallTriangleAge =
        (waveRadius - smallTriangleDistance) / scanBandWidth;
    const float smallStagger = (smallTriangleSeed - 0.5f) * 1.15f;
    const float smallDetailNoise = HikariSurfaceFeatureNoise3D(
        float3(smallScanUv * triangleNoiseScale * 1.65f, scanTime * 2.35f));
    const float smallPresence = smoothstep(
        0.20f,
        0.92f,
        smallTriangleSeed + smallDetailNoise * 0.26f);
    const float smallTriangleEnter = smoothstep(
        -0.42f,
        0.20f,
        smallTriangleAge + smallStagger + smallDetailNoise * 0.18f);
    const float smallTriangleLeave = 1.0f - smoothstep(
        0.95f,
        2.8f,
        smallTriangleAge + smallStagger * 0.25f);
    const float smallTriangleSweep =
        smallTriangleEnter * smallTriangleLeave * smallPresence;
    const float smallHotFront = 1.0f - smoothstep(
        scanBandWidth * 0.06f,
        scanBandWidth * 0.42f,
        abs(
            smallTriangleDistance - waveRadius +
            smallStagger * scanBandWidth * 0.22f));

    const float trailAge = triangleAge + stagger * 0.22f;
    float afterglow =
        smoothstep(0.45f, 1.25f, trailAge) *
        (1.0f - smoothstep(2.35f, 5.5f, trailAge));
    afterglow *= scanAfterglowStrength;

    const float3 edgeNormal = normalize(geometricNormal);
    const float normalEdge = length(ddx(edgeNormal)) + length(ddy(edgeNormal));
    const float silhouetteEdge = pow(
        1.0f - saturate(dot(edgeNormal, viewDirection)),
        3.5f);
    const float geometryEdge = saturate(
        normalEdge * 3.4f + silhouetteEdge * 0.55f) *
        scanGeometryEdgeStrength;

    const float fillLayer =
        triangleFill * triangleSweep * waveBand * shardBrightness;
    const float afterglowLayer =
        triangleFill * afterglow * shardBrightness *
        (0.72f + detailNoise * 0.28f);
    const float smallFillLayer =
        smallTriangleFill * smallTriangleSweep * waveBand *
        (0.44f + smallTriangleSeed * 0.44f);
    const float smallEdgeLayer =
        smallTriangleEdge * smallHotFront * smallPresence * 0.72f;
    const float edgeLayer = triangleEdge * saturate(
        hotFront * 1.45f +
        triangleSweep * 0.65f +
        afterglowLayer * 0.35f);
    const float frontLineLayer =
        frontLine * scanFrontLineStrength *
        (0.28f + triangleFill * 0.42f + triangleEdge * 1.15f);
    const float geometryEdgeLayer = geometryEdge * saturate(
        hotFront * 0.75f +
        triangleSweep * 0.55f +
        afterglowLayer * 0.28f);
    const float frontWash = hotFront * (0.18f + triangleFill * 0.42f);
    const float scanMask = saturate(
        fillLayer * 0.85f +
        afterglowLayer * 0.65f +
        smallFillLayer * 0.55f +
        smallEdgeLayer * 0.85f +
        edgeLayer * 1.25f +
        frontLineLayer * 1.55f +
        geometryEdgeLayer * 0.95f +
        frontWash) * radiusMask * waveAlive * scanColor.a * flicker;

    if (scanDistortionStrength > 0.00001f && scanMask > 0.0001f)
    {
        const float2 screenUv = pixelPosition.xy * gScreenParams.zw;
        float2 distortion = normalize(
            float2(detailNoise - 0.5f, triangleSeed - 0.5f) + 0.0001f.xx);
        distortion *= scanDistortionStrength * saturate(
            frontLine + hotFront * 0.5f + afterglowLayer * 0.25f);
        const float3 distortedSceneColor = gSceneColorTex.Sample(
            gLinearWrap,
            saturate(screenUv + distortion)).rgb;
        finalColor = lerp(
            finalColor,
            distortedSceneColor,
            saturate(scanMask * 0.12f));
    }

    const float overlayBoost = saturate((
        fillLayer * 0.45f +
        afterglowLayer * 0.38f +
        smallFillLayer * 0.32f +
        smallEdgeLayer * 0.42f +
        edgeLayer * 0.8f +
        frontLineLayer * 0.95f +
        geometryEdgeLayer * 0.55f) * scanIntensity * 0.2f);
    const float3 frontColor = lerp(
        scanColor.rgb,
        1.0f.xxx,
        saturate(frontLine * 0.8f));
    const float3 overlayColor = lerp(
        scanColor.rgb,
        frontColor,
        saturate(frontLineLayer)) * (
            0.72f + hotFront * 1.1f +
            smallEdgeLayer * 0.34f +
            edgeLayer * 0.42f +
            geometryEdgeLayer * 0.38f);
    finalColor *= 1.0f + scanColor.rgb * overlayBoost;
    finalColor += overlayColor * scanMask * scanIntensity;
    return finalColor;
}

#endif
