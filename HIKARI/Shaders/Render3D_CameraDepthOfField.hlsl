#include "Include/Lighting/HIKARI_VolumetricCamera.hlsli"
#include "Include/Lighting/HIKARI_DepthOfFieldMath.hlsli"

cbuffer CameraDepthOfFieldCB : register(b0)
{
    float4x4 gInverseViewProjection;
    float4 gCameraPosition;
    float4 gCameraForward;
    float4 gFocusBand; // focus distance, total focus range, near transition, far transition
    float4 gBlurAndRenderSize; // near radius, far radius, render width, render height
    float4 gFogDistancesAndVolumeSize; // fog near, fog far, volume depth, valid
    float4 gFrameAndQuality; // fixed tap count, orthographic, sprite layers (0:none,1:sharp,2:blur), camera far clip
};

Texture2D<float4> gSceneColor : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gNearSpriteColor : register(t2);
Texture2D<float4> gFarSpriteColor : register(t3);
Texture2D<float4> gSharpSpriteColor : register(t4);
Texture2D<float2> gNearSpriteMetadata : register(t5);
Texture2D<float2> gFarSpriteMetadata : register(t6);
Texture2D<float> gSharpSpriteDepth : register(t7);
Texture3D<float4> gIntegratedFogVolume : register(t8);

RWTexture2D<float4> gNearSpriteBlur : register(u0);
RWTexture2D<float4> gFarSpriteBlur : register(u1);
RWTexture2D<float2> gNearSpriteBlurMetadata : register(u2);
RWTexture2D<float2> gFarSpriteBlurMetadata : register(u3);
RWTexture2D<float4> gDepthOfFieldOutput : register(u4);

SamplerState gLinearClamp : register(s0);

static const float2 kGatherOffsets[16] =
{
    float2(0.0000f, 0.0000f), float2(0.5278f, -0.0859f), float2(-0.0401f, 0.5361f),
    float2(-0.6704f, -0.1799f), float2(0.2387f, -0.7118f), float2(0.8947f, 0.4124f),
    float2(-0.4202f, 0.8724f), float2(-0.9581f, -0.5114f), float2(0.6196f, -0.9353f),
    float2(0.1483f, 0.9701f), float2(0.9812f, -0.7153f), float2(-0.7432f, 0.6088f),
    float2(-0.2014f, -0.9811f), float2(0.7687f, 0.7340f), float2(-0.9324f, 0.1452f),
    float2(0.4211f, 0.1022f)
};

float2 ResolveCircleOfConfusion(float viewDepth)
{
    return HikariDepthOfFieldCoc(viewDepth, gFocusBand, gBlurAndRenderSize.xy);
}

float3 ReconstructWorldFromViewDepth(float2 uv, float viewDepth)
{
    float3 rayOrigin;
    float3 rayDirection;
    bool orthographic;
    HikariReconstructVolumeRay(uv, gInverseViewProjection, gCameraPosition.xyz,
                               rayOrigin, rayDirection, orthographic);
    if (orthographic)
    {
        return rayOrigin + rayDirection * max(viewDepth, 0.0f);
    }
    const float forwardCosine = max(dot(rayDirection, gCameraForward.xyz), 1.0e-4f);
    return gCameraPosition.xyz + rayDirection * (max(viewDepth, 0.0f) / forwardCosine);
}

float4 ApplyFogToPremultipliedSprite(float4 premultiplied, float2 uv, float viewDepth)
{
    if (gFogDistancesAndVolumeSize.w < 0.5f || premultiplied.a <= 1.0e-6f)
    {
        return premultiplied;
    }
    const float3 worldPosition = ReconstructWorldFromViewDepth(uv, viewDepth);
    const float distanceToCamera = gFrameAndQuality.y > 0.5f
        ? max(viewDepth, 0.0f)
        : length(worldPosition - gCameraPosition.xyz);
    const float nearDistance = max(gFogDistancesAndVolumeSize.x, 0.01f);
    if (distanceToCamera <= nearDistance)
    {
        return premultiplied;
    }
    const float slice = HikariVolumeDistanceToSlice(distanceToCamera, gFogDistancesAndVolumeSize.x,
                                                    gFogDistancesAndVolumeSize.y,
                                                    gFogDistancesAndVolumeSize.z);
    const float4 fog = gIntegratedFogVolume.SampleLevel(gLinearClamp, float3(uv, slice), 0.0f);
    premultiplied.rgb = premultiplied.rgb * fog.a + fog.rgb * premultiplied.a;
    return premultiplied;
}

float ReadRepresentativeDepth(float2 metadata, float alpha)
{
    return metadata.x / max(alpha, 1.0e-5f);
}

float ReadRepresentativeCoc(float2 metadata, float alpha)
{
    return metadata.y / max(alpha, 1.0e-5f);
}

float2 ResolveUnitGatherOffset(uint index)
{
    const float2 offset = kGatherOffsets[index];
    const float lengthSquared = dot(offset, offset);
    return lengthSquared > 1.0f ? offset * rsqrt(lengthSquared) : offset;
}

float4 SampleSpriteLayerColor(float2 uv, bool farLayer)
{
    return farLayer ? gFarSpriteColor.SampleLevel(gLinearClamp, uv, 0.0f)
                    : gNearSpriteColor.SampleLevel(gLinearClamp, uv, 0.0f);
}

float2 SampleSpriteLayerMetadata(float2 uv, bool farLayer)
{
    return farLayer ? gFarSpriteMetadata.SampleLevel(gLinearClamp, uv, 0.0f)
                    : gNearSpriteMetadata.SampleLevel(gLinearClamp, uv, 0.0f);
}

float ResolveSpriteLayerRadius(float2 centerUv, uint tapCount, float maximumRadius, bool farLayer)
{
    if (maximumRadius <= 0.0f)
    {
        return 0.0f;
    }
    float closestDistance = 3.402823466e+38f;
    float selectedRadius = 0.0f;
    [loop]
    for (uint tap = 0u; tap < tapCount; ++tap)
    {
        const float2 offset = ResolveUnitGatherOffset(tap) * maximumRadius;
        const float distancePixels = length(offset);
        const float2 sampleUv = saturate(centerUv + offset / gBlurAndRenderSize.zw);
        const float4 premultiplied = SampleSpriteLayerColor(sampleUv, farLayer);
        if (premultiplied.a <= 1.0e-5f)
        {
            continue;
        }
        const float2 metadata = SampleSpriteLayerMetadata(sampleUv, farLayer);
        const float candidateRadius = max(0.0f, ReadRepresentativeCoc(metadata, premultiplied.a));
        if (distancePixels <= candidateRadius && distancePixels < closestDistance)
        {
            closestDistance = distancePixels;
            selectedRadius = candidateRadius;
        }
    }
    return selectedRadius;
}

void GatherSpriteLayer(float2 centerUv, uint tapCount, float radius, bool farLayer,
                       out float4 color, out float2 metadata)
{
    color = 0.0f.xxxx;
    metadata = 0.0f.xx;
    if (radius <= 0.0f)
    {
        return;
    }
    float totalWeight = 0.0f;
    float depthSum = 0.0f;
    float alphaSum = 0.0f;
    [loop]
    for (uint tap = 0u; tap < tapCount; ++tap)
    {
        const float2 offset = ResolveUnitGatherOffset(tap) * radius;
        const float distancePixels = length(offset);
        const float weight = rcp(1.0f + distancePixels * distancePixels * 0.002f);
        totalWeight += weight;
        const float2 sampleUv = saturate(centerUv + offset / gBlurAndRenderSize.zw);
        float4 premultiplied = SampleSpriteLayerColor(sampleUv, farLayer);
        if (premultiplied.a <= 1.0e-5f)
        {
            continue;
        }
        const float2 sourceMetadata = SampleSpriteLayerMetadata(sampleUv, farLayer);
        const float depth = ReadRepresentativeDepth(sourceMetadata, premultiplied.a);
        const float sourceRadius = max(0.0f, ReadRepresentativeCoc(sourceMetadata, premultiplied.a));
        if (distancePixels > sourceRadius)
        {
            continue;
        }
        premultiplied = ApplyFogToPremultipliedSprite(premultiplied, sampleUv, depth);
        color += premultiplied * weight;
        depthSum += depth * premultiplied.a * weight;
        alphaSum += premultiplied.a * weight;
    }
    const float normalization = rcp(max(totalWeight, 1.0e-5f));
    color *= normalization;
    metadata = float2(depthSum, alphaSum) * normalization;
}

[numthreads(8, 8, 1)]
void GatherSpriteLayersCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint blurWidth;
    uint blurHeight;
    gNearSpriteBlur.GetDimensions(blurWidth, blurHeight);
    if (dispatchThreadId.x >= blurWidth || dispatchThreadId.y >= blurHeight)
    {
        return;
    }

    const uint tapCount = clamp((uint)gFrameAndQuality.x, 1u, 16u);
    const float2 centerUv = (float2(dispatchThreadId.xy) + 0.5f) / float2(blurWidth, blurHeight);
    const float nearRadius = ResolveSpriteLayerRadius(centerUv, tapCount, gBlurAndRenderSize.x, false);
    const float farRadius = ResolveSpriteLayerRadius(centerUv, tapCount, gBlurAndRenderSize.y, true);
    float4 nearColor;
    float2 nearMetadata;
    GatherSpriteLayer(centerUv, tapCount, nearRadius, false, nearColor, nearMetadata);
    float4 farColor;
    float2 farMetadata;
    GatherSpriteLayer(centerUv, tapCount, farRadius, true, farColor, farMetadata);
    gNearSpriteBlur[dispatchThreadId.xy] = nearColor;
    gFarSpriteBlur[dispatchThreadId.xy] = farColor;
    gNearSpriteBlurMetadata[dispatchThreadId.xy] = nearMetadata;
    gFarSpriteBlurMetadata[dispatchThreadId.xy] = farMetadata;
}

float ReadOpaqueViewDepth(uint2 pixel, out bool hasDepth)
{
    const float depth = gSceneDepth.Load(int3(pixel, 0));
    hasDepth = depth < 0.999999f;
    if (!hasDepth)
    {
        return 0.0f;
    }
    const float2 sampleUv = (float2(pixel) + 0.5f) / gBlurAndRenderSize.zw;
    const float3 worldPosition = HikariUnprojectVolumePosition(sampleUv, depth, gInverseViewProjection);
    return max(dot(worldPosition - gCameraPosition.xyz, gCameraForward.xyz), 0.0f);
}

float ResolveOpaqueNearGatherRadius(uint2 pixel, float centerViewDepth, bool centerHasDepth, uint tapCount)
{
    const float maximumRadius = max(gBlurAndRenderSize.x, 0.0f);
    float radius = centerHasDepth ? ResolveCircleOfConfusion(centerViewDepth).x : 0.0f;
    if (maximumRadius <= 0.0f || radius >= maximumRadius)
    {
        return min(radius, maximumRadius);
    }

    // 近景CoCが0の出力にも、ぼけ footprint が届く被写体の半径を伝える。
    const int2 maximumPixel = int2(gBlurAndRenderSize.zw) - 1;
    [loop]
    for (uint tap = 0u; tap < tapCount; ++tap)
    {
        const float2 unitOffset = ResolveUnitGatherOffset(tap);
        const float offsetLengthSquared = dot(unitOffset, unitOffset);
        // 固定tapでも小さいCoCの外側拡散を検出できるよう、探索点を中心へ寄せる。
        const float offsetScale = offsetLengthSquared * offsetLengthSquared;
        const float2 offset = unitOffset * (maximumRadius * offsetScale);
        const int2 samplePixel = clamp(int2(round(float2(pixel) + offset)), int2(0, 0), maximumPixel);
        const float2 pixelOffset = float2(samplePixel) - float2(pixel);
        const float distancePixels = length(pixelOffset);
        if (distancePixels <= 0.0f)
        {
            continue;
        }

        bool sampleHasDepth;
        const float sampleViewDepth = ReadOpaqueViewDepth(uint2(samplePixel), sampleHasDepth);
        if (!sampleHasDepth || (centerHasDepth && sampleViewDepth > centerViewDepth + 0.01f))
        {
            continue;
        }

        const float sampleNearRadius = ResolveCircleOfConfusion(sampleViewDepth).x;
        if (sampleNearRadius > 0.0f && distancePixels <= sampleNearRadius + 0.5f)
        {
            radius = max(radius, sampleNearRadius);
        }
    }
    return min(radius, maximumRadius);
}

float4 SampleOpaqueColorFootprint(float2 pixelPosition, float blurRadius, uint tapCount)
{
    const float2 renderSize = gBlurAndRenderSize.zw;
    const float2 centerUv = (pixelPosition + 0.5f) / renderSize;
    const float footprintWidth = min(max(blurRadius, 0.0f) * rsqrt(max((float)tapCount, 1.0f)), 4.0f);
    if (footprintWidth < 1.0f)
    {
        return gSceneColor.SampleLevel(gLinearClamp, centerUv, 0.0f);
    }

    const float2 halfFootprintUv = (0.5f * footprintWidth) / renderSize;
    const float4 topLeft = gSceneColor.SampleLevel(gLinearClamp, centerUv - halfFootprintUv, 0.0f);
    const float4 topRight = gSceneColor.SampleLevel(gLinearClamp, centerUv + float2(halfFootprintUv.x,
                                                                                    -halfFootprintUv.y),
                                                    0.0f);
    const float4 bottomLeft = gSceneColor.SampleLevel(gLinearClamp, centerUv + float2(-halfFootprintUv.x,
                                                                                       halfFootprintUv.y),
                                                     0.0f);
    const float4 bottomRight = gSceneColor.SampleLevel(gLinearClamp, centerUv + halfFootprintUv, 0.0f);
    return (topLeft + topRight + bottomLeft + bottomRight) * 0.25f;
}

float4 GatherOpaqueBase(uint2 pixel, float centerViewDepth, bool centerHasDepth)
{
    const uint tapCount = clamp((uint)gFrameAndQuality.x, 1u, 16u);
    const int2 maximumPixel = int2(gBlurAndRenderSize.zw) - 1;
    float4 center = gSceneColor.Load(int3(pixel, 0));
    if (max(gBlurAndRenderSize.x, gBlurAndRenderSize.y) <= 0.0f) return center;
    const float2 centerCoc = centerHasDepth ? ResolveCircleOfConfusion(centerViewDepth) : 0.0f.xx;
    const float farRadius = min(max(centerCoc.y, 0.0f), max(gBlurAndRenderSize.y, 0.0f));
    const float nearRadius = ResolveOpaqueNearGatherRadius(pixel, centerViewDepth, centerHasDepth, tapCount);
    float4 nearSum = 0.0f.xxxx;
    float4 farSum = 0.0f.xxxx;
    float nearWeightSum = 0.0f;
    float farWeightSum = 0.0f;
    float nearTotalWeight = 0.0f;
    float4 result = center;

    // Blur半径ごとにtapを張り直し、有効な局所サンプルの重みだけで色を正規化する。
    [loop]
    for (uint tap = 0u; tap < tapCount; ++tap)
    {
        const float2 unitOffset = ResolveUnitGatherOffset(tap);
        if (farRadius > 0.0f)
        {
            const float2 offset = unitOffset * farRadius;
            const float distancePixels = length(offset);
            const float2 samplePosition = float2(pixel) + offset;
            const float weight = rcp(1.0f + distancePixels * distancePixels * 0.002f);
            const int2 samplePixel = clamp(int2(round(samplePosition)), int2(0, 0), maximumPixel);
            bool sampleHasDepth;
            const float sampleViewDepth = ReadOpaqueViewDepth(uint2(samplePixel), sampleHasDepth);
            // 奥の背景色は、より手前の不透明面へ広げない。
            if (sampleHasDepth && sampleViewDepth <= centerViewDepth + 0.01f)
            {
                const float2 sampleCoc = ResolveCircleOfConfusion(sampleViewDepth);
                if (sampleCoc.y > 0.0f)
                {
                    farSum += SampleOpaqueColorFootprint(samplePosition, farRadius, tapCount) * weight;
                    farWeightSum += weight;
                }
            }
        }

        if (nearRadius > 0.0f)
        {
            const float2 offset = unitOffset * nearRadius;
            const float distancePixels = length(offset);
            const float2 samplePosition = float2(pixel) + offset;
            const float weight = rcp(1.0f + distancePixels * distancePixels * 0.002f);
            const int2 samplePixel = clamp(int2(round(samplePosition)), int2(0, 0), maximumPixel);
            nearTotalWeight += weight;

            bool sampleHasDepth;
            const float sampleViewDepth = ReadOpaqueViewDepth(uint2(samplePixel), sampleHasDepth);
            if (sampleHasDepth && (!centerHasDepth || sampleViewDepth <= centerViewDepth + 0.01f))
            {
                const float sampleNearRadius = ResolveCircleOfConfusion(sampleViewDepth).x;
                if (sampleNearRadius > 0.0f && distancePixels <= sampleNearRadius + 0.5f)
                {
                    nearSum += SampleOpaqueColorFootprint(samplePosition, nearRadius, tapCount) * weight;
                    nearWeightSum += weight;
                }
            }
        }
    }

    if (farWeightSum > 1.0e-5f)
    {
        result = farSum / farWeightSum;
    }
    if (nearWeightSum > 1.0e-5f)
    {
        const float nearCoverage = saturate(nearWeightSum / max(nearTotalWeight, 1.0e-5f));
        result = lerp(result, nearSum / nearWeightSum, nearCoverage);
    }
    return result;
}

float4 SampleSpriteBlurColor(float2 uv, uint2 size, bool farLayer)
{
    const float2 position = uv * float2(size) - 0.5f;
    const int2 base = int2(floor(position));
    const float2 fraction = frac(position);
    const int2 maximumPixel = int2(size) - 1;
    const int2 p00 = clamp(base, int2(0, 0), maximumPixel);
    const int2 p10 = clamp(base + int2(1, 0), int2(0, 0), maximumPixel);
    const int2 p01 = clamp(base + int2(0, 1), int2(0, 0), maximumPixel);
    const int2 p11 = clamp(base + int2(1, 1), int2(0, 0), maximumPixel);
    const float4 c00 = farLayer ? gFarSpriteBlur[p00] : gNearSpriteBlur[p00];
    const float4 c10 = farLayer ? gFarSpriteBlur[p10] : gNearSpriteBlur[p10];
    const float4 c01 = farLayer ? gFarSpriteBlur[p01] : gNearSpriteBlur[p01];
    const float4 c11 = farLayer ? gFarSpriteBlur[p11] : gNearSpriteBlur[p11];
    return lerp(lerp(c00, c10, fraction.x), lerp(c01, c11, fraction.x), fraction.y);
}

float2 SampleSpriteBlurMetadata(float2 uv, uint2 size, bool farLayer)
{
    const float2 position = uv * float2(size) - 0.5f;
    const int2 base = int2(floor(position));
    const float2 fraction = frac(position);
    const int2 maximumPixel = int2(size) - 1;
    const int2 p00 = clamp(base, int2(0, 0), maximumPixel);
    const int2 p10 = clamp(base + int2(1, 0), int2(0, 0), maximumPixel);
    const int2 p01 = clamp(base + int2(0, 1), int2(0, 0), maximumPixel);
    const int2 p11 = clamp(base + int2(1, 1), int2(0, 0), maximumPixel);
    const float2 c00 = farLayer ? gFarSpriteBlurMetadata[p00] : gNearSpriteBlurMetadata[p00];
    const float2 c10 = farLayer ? gFarSpriteBlurMetadata[p10] : gNearSpriteBlurMetadata[p10];
    const float2 c01 = farLayer ? gFarSpriteBlurMetadata[p01] : gNearSpriteBlurMetadata[p01];
    const float2 c11 = farLayer ? gFarSpriteBlurMetadata[p11] : gNearSpriteBlurMetadata[p11];
    return lerp(lerp(c00, c10, fraction.x), lerp(c01, c11, fraction.x), fraction.y);
}

[numthreads(8, 8, 1)]
void CompositeCameraDepthOfFieldCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint sceneWidth;
    uint sceneHeight;
    gDepthOfFieldOutput.GetDimensions(sceneWidth, sceneHeight);
    if (dispatchThreadId.x >= sceneWidth || dispatchThreadId.y >= sceneHeight)
    {
        return;
    }

    const uint2 pixel = dispatchThreadId.xy;
    const float2 uv = (float2(pixel) + 0.5f) / float2(sceneWidth, sceneHeight);
    bool centerHasDepth;
    float centerViewDepth = ReadOpaqueViewDepth(pixel, centerHasDepth);
    if (!centerHasDepth)
    {
        centerViewDepth = max(gFocusBand.x, gFrameAndQuality.w);
    }
    float4 result = GatherOpaqueBase(pixel, centerViewDepth, centerHasDepth);
    if (gFrameAndQuality.z > 0.5f)
    {
        uint2 blurSize = uint2(0, 0);
        const float2 blurUv = uv;
        if (gFrameAndQuality.z > 1.5f)
        {
            gNearSpriteBlur.GetDimensions(blurSize.x, blurSize.y);
            float4 farSprite = SampleSpriteBlurColor(blurUv, blurSize, true);
            const float2 farDepthCoverage = SampleSpriteBlurMetadata(blurUv, blurSize, true);
            const float farSpriteDepth = farDepthCoverage.x / max(farDepthCoverage.y, 1.0e-5f);
            if (centerHasDepth && farSpriteDepth > centerViewDepth + 0.01f)
            {
                farSprite = 0.0f.xxxx;
            }
            result = farSprite + result * (1.0f - farSprite.a);
        }

        float4 sharpSprite = gSharpSpriteColor.Load(int3(pixel, 0));
        if (sharpSprite.a > 1.0e-5f)
        {
            const float sharpDepth = gSharpSpriteDepth.Load(int3(pixel, 0)) / sharpSprite.a;
            sharpSprite = ApplyFogToPremultipliedSprite(sharpSprite, uv, sharpDepth);
        }
        result = sharpSprite + result * (1.0f - sharpSprite.a);

        if (gFrameAndQuality.z > 1.5f)
        {
            float4 nearSprite = SampleSpriteBlurColor(blurUv, blurSize, false);
            const float2 nearDepthCoverage = SampleSpriteBlurMetadata(blurUv, blurSize, false);
            const float nearSpriteDepth = nearDepthCoverage.x / max(nearDepthCoverage.y, 1.0e-5f);
            if (centerHasDepth && nearSpriteDepth > centerViewDepth + 0.01f)
            {
                nearSprite = 0.0f.xxxx;
            }
            result = nearSprite + result * (1.0f - nearSprite.a);
        }
    }
    gDepthOfFieldOutput[pixel] = result;
}
