#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"

static const uint HIKARI_WORLD_TEXT_FIXED = 0u;
static const uint HIKARI_WORLD_TEXT_CAMERA_FACING = 1u;
static const uint HIKARI_WORLD_TEXT_FIXED_Y_AXIS = 2u;

struct WorldTextGlyphInstance
{
    float4 position;
    float4 orientation;
    float4 localRect;
    float4 uvRect;
    float4 fillColorOpacity;
    float4 outlineColorWidth;
    uint textureDescriptorIndex;
    uint facingMode;
    uint reserved0;
    uint reserved1;
};

StructuredBuffer<WorldTextGlyphInstance> gWorldTextGlyphs : register(t1);
Texture2D<float4> gWorldTextAtlases[3968] : register(t20);
SamplerState gWorldTextSampler : register(s0);

cbuffer CameraConstants : register(b0)
{
    float4x4 gViewProjection;
    float4x4 gInverseViewProjection;
    float4 gCameraPosition;
    float4 gTimeParams;
    float4 gSimulationTimeParams;
    float4 gScreenParams;
};

cbuffer WorldTextDrawConstants : register(b1)
{
    uint gWorldTextBaseIndex;
    uint gWorldTextCount;
    uint gWorldTextReserved0;
    uint gWorldTextReserved1;
};

struct WorldTextVertex
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint textureDescriptorIndex : TEXCOORD1;
    float4 fillColorOpacity : COLOR0;
    float4 outlineColorWidth : COLOR1;
};

float3 NormalizeOr(float3 value, float3 fallbackValue)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8f
        ? value * rsqrt(lengthSquared)
        : fallbackValue;
}

float4 NormalizeQuaternion(float4 value)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8f
        ? value * rsqrt(lengthSquared)
        : float4(0.0f, 0.0f, 0.0f, 1.0f);
}

float3 RotateByQuaternion(float3 value, float4 quaternion)
{
    return value + 2.0f * cross(
        quaternion.xyz,
        cross(quaternion.xyz, value) + quaternion.w * value);
}

void ResolveCameraBasis(out float3 right, out float3 up, out float3 forward)
{
    float4 centerH = mul(
        gInverseViewProjection,
        float4(0.0f, 0.0f, 0.0f, 1.0f));
    float4 rightH = mul(
        gInverseViewProjection,
        float4(1.0f, 0.0f, 0.0f, 1.0f));
    float4 upH = mul(
        gInverseViewProjection,
        float4(0.0f, 1.0f, 0.0f, 1.0f));
    centerH.xyz /= max(abs(centerH.w), 1.0e-6f);
    rightH.xyz /= max(abs(rightH.w), 1.0e-6f);
    upH.xyz /= max(abs(upH.w), 1.0e-6f);
    right = NormalizeOr(
        rightH.xyz - centerH.xyz,
        float3(1.0f, 0.0f, 0.0f));
    up = NormalizeOr(
        upH.xyz - centerH.xyz,
        float3(0.0f, 1.0f, 0.0f));
    forward = NormalizeOr(
        cross(right, up),
        float3(0.0f, 0.0f, 1.0f));
}

[outputtopology("triangle")]
[numthreads(4, 1, 1)]
void WorldTextMS(
    uint3 groupId : SV_GroupID,
    uint groupThreadId : SV_GroupThreadID,
    out vertices WorldTextVertex outputVertices[4],
    out indices uint3 outputTriangles[2])
{
    const uint localIndex = groupId.x;
    const bool validGlyph = localIndex < gWorldTextCount;
    SetMeshOutputCounts(validGlyph ? 4u : 0u, validGlyph ? 2u : 0u);
    if (!validGlyph)
    {
        return;
    }

    const WorldTextGlyphInstance instance =
        gWorldTextGlyphs[gWorldTextBaseIndex + localIndex];
    float3 cameraRight;
    float3 cameraUp;
    float3 cameraForward;
    ResolveCameraBasis(cameraRight, cameraUp, cameraForward);
    float3 right = cameraRight;
    float3 up = cameraUp;
    if (instance.facingMode == HIKARI_WORLD_TEXT_FIXED)
    {
        const float4 orientation = NormalizeQuaternion(instance.orientation);
        right = NormalizeOr(
            RotateByQuaternion(float3(1.0f, 0.0f, 0.0f), orientation),
            cameraRight);
        up = NormalizeOr(
            RotateByQuaternion(float3(0.0f, 1.0f, 0.0f), orientation),
            cameraUp);
    }
    else if (instance.facingMode == HIKARI_WORLD_TEXT_FIXED_Y_AXIS)
    {
        up = float3(0.0f, 1.0f, 0.0f);
        const float3 toCamera = NormalizeOr(
            gCameraPosition.xyz - instance.position.xyz,
            -cameraForward);
        const float3 normal = NormalizeOr(
            float3(toCamera.x, 0.0f, toCamera.z),
            float3(0.0f, 0.0f, -1.0f));
        right = NormalizeOr(cross(normal, up), cameraRight);
    }

    static const float2 baseUvs[4] = {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f)
    };
    const float2 baseUv = baseUvs[groupThreadId];
    const float2 corner = float2(baseUv.x - 0.5f, 0.5f - baseUv.y);
    const float2 localPosition =
        instance.localRect.xy + corner * instance.localRect.zw;
    const float3 worldPosition = instance.position.xyz +
        right * localPosition.x + up * localPosition.y;

    WorldTextVertex output = (WorldTextVertex)0;
    output.position = mul(gViewProjection, float4(worldPosition, 1.0f));
    output.uv = lerp(instance.uvRect.xy, instance.uvRect.zw, baseUv);
    output.textureDescriptorIndex = instance.textureDescriptorIndex;
    output.fillColorOpacity = instance.fillColorOpacity;
    output.outlineColorWidth = instance.outlineColorWidth;
    outputVertices[groupThreadId] = output;

    if (groupThreadId == 0u)
    {
        outputTriangles[0] = uint3(0u, 1u, 2u);
    }
    else if (groupThreadId == 1u)
    {
        outputTriangles[1] = uint3(0u, 2u, 3u);
    }
}

float4 WorldTextPS(WorldTextVertex input) : SV_TARGET
{
    const uint textureIndex = NonUniformResourceIndex(
        input.textureDescriptorIndex);
    const float distance = gWorldTextAtlases[textureIndex].Sample(
        gWorldTextSampler,
        input.uv).a;
    const float antialias = max(fwidth(distance) * 1.35f, 0.004f);
    const float fillCoverage = smoothstep(
        0.5f - antialias,
        0.5f + antialias,
        distance);
    const float outlineEdge = 0.5f - saturate(
        input.outlineColorWidth.a);
    const float outerCoverage = smoothstep(
        outlineEdge - antialias,
        outlineEdge + antialias,
        distance);
    const float outlineCoverage = max(
        outerCoverage - fillCoverage,
        0.0f);
    const float opacity = saturate(input.fillColorOpacity.a);
    const float fillAlpha = fillCoverage * opacity;
    const float outlineAlpha = outlineCoverage * opacity;

    float3 premultiplied = input.outlineColorWidth.rgb * outlineAlpha;
    float alpha = outlineAlpha;
    premultiplied = input.fillColorOpacity.rgb * fillAlpha +
        premultiplied * (1.0f - fillAlpha);
    alpha = fillAlpha + alpha * (1.0f - fillAlpha);
    clip(alpha - (1.0f / 255.0f));
    return float4(premultiplied, alpha);
}
