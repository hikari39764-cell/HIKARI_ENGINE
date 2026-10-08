#ifndef HIKARI_LOCAL_LIGHT_CONTRACT_INCLUDED
#define HIKARI_LOCAL_LIGHT_CONTRACT_INCLUDED

static const uint HIKARI_LOCAL_LIGHT_TYPE_POINT = 0u;
static const uint HIKARI_LOCAL_LIGHT_TYPE_SPOT = 1u;

// Keep this layout synchronized with
// RENDER3D::LIGHTING::LocalLightGpuData.
struct HikariLocalLightGpuData
{
    float4 positionRange;
    float4 colorIntensity;
    float4 directionInnerCos;
    float outerConeCos;
    float angularFalloff;
    uint kind; // low byte: type, upper16: shadow first face + 1
    float sourceRadius;
};

struct HikariLocalShadowData
{
    float4x4 viewProj[16];
    uint faceCount;
    uint textureDescriptorIndex;
    uint tilesPerRow;
    uint tileResolution;
};

struct HikariLocalLightSample
{
    float3 directionToLight;
    float3 radiance;
    float distanceToLight;
    float attenuation;
    float sourceHalfAngleSineSquared;
};

// 見かけの円盤を立体角で一様に選ぶ。入力は sin^2(halfAngle/2)。
float3 HikariSampleEmitterCone(float3 axis, float halfAngleSineSquared, float2 sampleUv)
{
    const float3 up = abs(axis.z) < 0.999f ? float3(0,0,1) : float3(0,1,0);
    const float3 tangent = normalize(cross(up, axis));
    const float cosine = 1.0f - 2.0f * halfAngleSineSquared * sampleUv.x;
    const float sine = sqrt(saturate(1.0f-cosine*cosine));
    const float azimuth = 6.28318530718f * sampleUv.y;
    return axis * cosine + (tangent*cos(azimuth) + cross(axis,tangent)*sin(azimuth))*sine;
}

// 光度 [cd] から照度 [lux] への距離減衰。逆二乗で、range の手前で滑らかに 0 へ落とす。
// 距離の単位はメートル。発光体半径より内側は半径で頭打ちにする。
float HikariLocalLightRangeAttenuation(
    float distanceToLight,
    float range,
    float sourceRadius = 0.0f)
{
    if (range <= 0.0f || distanceToLight >= range)
        return 0.0f;
    const float normalizedDistance = distanceToLight / range;
    const float squaredDistance = normalizedDistance * normalizedDistance;
    const float window = saturate(1.0f - squaredDistance * squaredDistance);
    return window * window / max(max(distanceToLight * distanceToLight,
        sourceRadius * sourceRadius), 1e-4f);
}

float HikariLocalLightSourceHalfAngleSineSquared(float distanceToLight, float sourceRadius)
{
    const float sine = saturate(sourceRadius / max(distanceToLight, 1e-4f));
    const float sineSquared = sine * sine;
    // sin^2(asin(radius/distance)/2)。小さい発光体でも桁落ちさせない。
    return 0.5f * sineSquared / (1.0f + sqrt(saturate(1.0f - sineSquared)));
}

float HikariLocalLightConeAttenuation(
    float coneCos, float innerCos, float outerCos, float angularFalloff)
{
    const float weight = saturate((coneCos - outerCos) /
        max(innerCos - outerCos, 1e-4f));
    return pow(weight, max(angularFalloff, 0.1f));
}

bool HikariResolveLocalLightSample(
    HikariLocalLightGpuData light,
    float3 worldPosition,
    out HikariLocalLightSample sample)
{
    sample.directionToLight = float3(0.0f, 1.0f, 0.0f);
    sample.radiance = 0.0f.xxx;
    sample.distanceToLight = 0.0f;
    sample.attenuation = 0.0f;
    sample.sourceHalfAngleSineSquared = 0.0f;

    const uint lightType = light.kind & 0xffu;
    if (lightType != HIKARI_LOCAL_LIGHT_TYPE_POINT &&
        lightType != HIKARI_LOCAL_LIGHT_TYPE_SPOT)
    {
        return false;
    }

    const float3 toLight =
        light.positionRange.xyz - worldPosition;
    const float distanceToLight = length(toLight);
    float attenuation = HikariLocalLightRangeAttenuation(
        distanceToLight,
        light.positionRange.w,
        light.sourceRadius);

    sample.directionToLight =
        distanceToLight > 1e-5f
        ? toLight / distanceToLight
        : float3(0.0f, 1.0f, 0.0f);

    if (lightType == HIKARI_LOCAL_LIGHT_TYPE_SPOT)
    {
        const float3 spotDirection =
            light.directionInnerCos.xyz;
        const float coneCos = dot(
            spotDirection,
            -sample.directionToLight);
        const float innerCos = light.directionInnerCos.w;
        const float outerCos = light.outerConeCos;
        attenuation *= HikariLocalLightConeAttenuation(
            coneCos, innerCos, outerCos, light.angularFalloff);
    }

    sample.radiance =
        light.colorIntensity.rgb *
        light.colorIntensity.a *
        attenuation;
    sample.distanceToLight = distanceToLight;
    sample.attenuation = attenuation;
    sample.sourceHalfAngleSineSquared = HikariLocalLightSourceHalfAngleSineSquared(
        distanceToLight, light.sourceRadius);
    return attenuation > 0.0f;
}

uint HikariLocalShadowFace(float3 fromLight)
{
    const float3 magnitude = abs(fromLight);
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
        return fromLight.x >= 0.0f ? 0u : 1u;
    if (magnitude.y >= magnitude.z)
        return fromLight.y >= 0.0f ? 2u : 3u;
    return fromLight.z >= 0.0f ? 4u : 5u;
}

bool HikariResolveLocalShadowPlaneDepthGradient(float4x4 shadowViewProj, float4 clipPosition,
    float3 geometricNormalWS, out float2 depthGradient)
{
    depthGradient = 0.0f.xx;
    const float normalLengthSquared = dot(geometricNormalWS, geometricNormalWS);
    if (!all(isfinite(clipPosition)) || clipPosition.w <= 1e-6f ||
        !all(isfinite(geometricNormalWS)) || !isfinite(normalLengthSquared) || normalLengthSquared <= 1e-8f)
        return false;

    const float3 normalWS = geometricNormalWS * rsqrt(normalLengthSquared);
    const float3 referenceAxis = abs(normalWS.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
    const float3 tangentWS = normalize(cross(referenceAxis, normalWS));
    const float3 bitangentWS = cross(normalWS, tangentWS);
    const float4 tangentClip = mul(shadowViewProj, float4(tangentWS, 0.0f));
    const float4 bitangentClip = mul(shadowViewProj, float4(bitangentWS, 0.0f));
    const float inverseClipW2 = rcp(clipPosition.w * clipPosition.w);
    const float3 tangentNdc =
        (tangentClip.xyz * clipPosition.w - clipPosition.xyz * tangentClip.w) * inverseClipW2;
    const float3 bitangentNdc =
        (bitangentClip.xyz * clipPosition.w - clipPosition.xyz * bitangentClip.w) * inverseClipW2;
    if (!all(isfinite(tangentNdc)) || !all(isfinite(bitangentNdc)))
        return false;

    const float2 tangentFaceUv = tangentNdc.xy * float2(0.5f, -0.5f);
    const float2 bitangentFaceUv = bitangentNdc.xy * float2(0.5f, -0.5f);
    const float tangentUvLength = length(tangentFaceUv);
    const float bitangentUvLength = length(bitangentFaceUv);
    const float projectedArea = tangentUvLength * bitangentUvLength;
    const float uvDeterminant = tangentFaceUv.x * bitangentFaceUv.y -
                                bitangentFaceUv.x * tangentFaceUv.y;
    if (!isfinite(tangentUvLength) || !isfinite(bitangentUvLength) || !isfinite(projectedArea) ||
        !isfinite(uvDeterminant) || tangentUvLength <= 1e-6f || bitangentUvLength <= 1e-6f ||
        abs(uvDeterminant) <= max(1e-8f, projectedArea * 1e-3f))
        return false;

    depthGradient = float2((tangentNdc.z * bitangentFaceUv.y - bitangentNdc.z * tangentFaceUv.y) / uvDeterminant,
        (tangentFaceUv.x * bitangentNdc.z - bitangentFaceUv.x * tangentNdc.z) / uvDeterminant);
    return all(isfinite(depthGradient));
}

float HikariLocalShadowTapReferenceDepth(float centerDepth, float2 depthGradient, float2 faceUvDelta,
    float2 sampleAtlasUv, float atlasSize, uint tileResolution)
{
    const float2 phase = frac(sampleAtlasUv * atlasSize - 0.5f);
    const float2 gradientPositive = max(depthGradient, 0.0f.xx);
    const float2 gradientNegative = max(-depthGradient, 0.0f.xx);
    const float footprintDepthBias =
        (dot(gradientPositive, phase) + dot(gradientNegative, 1.0f.xx - phase)) /
        max(float(tileResolution), 1.0f);
    return centerDepth + dot(depthGradient, faceUvDelta) - footprintDepthBias;
}

float HikariSampleLocalLightShadow(Texture2D atlas, SamplerComparisonState comparisonSampler,
    HikariLocalShadowData shadow, HikariLocalLightGpuData light,
    float3 worldPosWS, float3 geometricNormalWS, bool filter = true)
{
    const uint encodedFace = light.kind >> 16u;
    if (encodedFace == 0u || shadow.faceCount == 0u || shadow.tilesPerRow == 0u || shadow.tileResolution == 0u)
        return 1.0f;
    const uint type = light.kind & 255u;
    const uint faceCount = type == HIKARI_LOCAL_LIGHT_TYPE_POINT ? 6u : 1u;
    const uint firstFace = encodedFace - 1u;
    if (firstFace + faceCount > min(shadow.faceCount, 16u)) return 1.0f;
    const float3 fromLight = worldPosWS - light.positionRange.xyz;
    const float distanceToLight = length(fromLight);
    if (distanceToLight < 1e-5f) return 1.0f;
    const uint face = firstFace + (type == HIKARI_LOCAL_LIGHT_TYPE_POINT
        ? HikariLocalShadowFace(fromLight) : 0u);
    float4 clip = mul(shadow.viewProj[face], float4(worldPosWS, 1.0f));
    if (!all(isfinite(clip)) || clip.w <= 1e-6f) return 1.0f;

    // 透視投影のヤコビアンで幾何法線バイアスを一 texel 以内に制限する。
    // 方向光用の固定深度バイアスを距離依存の局所光投影へ流用しない。
    const float3 offset = geometricNormalWS * 0.01f;
    const float4 projectedOffset = mul(shadow.viewProj[face], float4(offset, 0.0f));
    const float2 ndcOffset = (projectedOffset.xy * clip.w - clip.xy * projectedOffset.w) / (clip.w * clip.w);
    const float offsetScale = min(1.0f, (2.0f / shadow.tileResolution) / max(length(ndcOffset), 1e-6f));
    const float3 biasedPosition = worldPosWS + offset * offsetScale -
        fromLight / distanceToLight * min(distanceToLight * 0.01f, max(1e-4f, distanceToLight * 2e-5f));
    clip = mul(shadow.viewProj[face], float4(biasedPosition, 1.0f));
    if (!all(isfinite(clip)) || clip.w <= 1e-6f) return 1.0f;
    const float3 ndc = clip.xyz / clip.w;
    const float2 faceUv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    if (any(faceUv < 0.0f) || any(faceUv > 1.0f) || ndc.z <= 0.0f || ndc.z >= 1.0f)
        return 1.0f;
    const uint grid = shadow.tilesPerRow;
    const float2 tile = float2(face % grid, face / grid);
    const float atlasSize = float(grid * shadow.tileResolution);
    const float2 uv = (tile + faceUv) / float(grid);
    const float2 minimumUv = tile / float(grid) + 0.5f / atlasSize;
    const float2 maximumUv = (tile + 1.0f) / float(grid) - 0.5f / atlasSize;
    if (!filter) return atlas.SampleCmpLevelZero(comparisonSampler,
        clamp(uv, minimumUv, maximumUv), ndc.z);

    float2 depthGradient = 0.0f.xx;
    const bool hasDepthGradient =
        HikariResolveLocalShadowPlaneDepthGradient(shadow.viewProj[face], clip, geometricNormalWS, depthGradient);
    float visibility = 0.0f;
    // 九つの bilinear compare。guard と tile clamp で隣の light を読まない。
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
    {
        const float2 sampleUv = clamp(uv + float2(x,y) / atlasSize, minimumUv, maximumUv);
        const float2 faceUvDelta = (sampleUv - uv) * float(grid);
        const float referenceDepth = hasDepthGradient
            ? HikariLocalShadowTapReferenceDepth(ndc.z, depthGradient, faceUvDelta, sampleUv, atlasSize,
                shadow.tileResolution)
            : ndc.z;
        visibility += atlas.SampleCmpLevelZero(comparisonSampler, sampleUv, referenceDepth);
    }
    return visibility / 9.0f;
}

#endif
