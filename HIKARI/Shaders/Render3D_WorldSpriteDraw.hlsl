#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"

static const uint HIKARI_WORLD_SPRITE_FIXED = 0u;
static const uint HIKARI_WORLD_SPRITE_CAMERA_FACING = 1u;
static const uint HIKARI_WORLD_SPRITE_FIXED_Y_AXIS = 2u;
static const uint HIKARI_WORLD_SPRITE_ALPHA_OPAQUE = 0u;
static const uint HIKARI_WORLD_SPRITE_ALPHA_MASK = 1u;
static const uint HIKARI_WORLD_SPRITE_ALPHA_BLEND = 2u;
static const uint HIKARI_WORLD_SPRITE_ALPHA_HYBRID_CORE = 3u;
static const uint HIKARI_WORLD_SPRITE_ALPHA_HYBRID_FRINGE = 4u;
static const float HIKARI_WORLD_SPRITE_OPAQUE_CORE_ALPHA = 250.0f / 255.0f;

struct WorldSpriteInstance
{
    float4 position;
    float4 orientation;
    float4 sizePivot;
    float4 tintOpacity;
    uint textureDescriptorIndex;
    uint facingMode;
    uint alphaMode;
    float alphaCutoff;
};

StructuredBuffer<WorldSpriteInstance> gWorldSprites : register(t1);
Texture2D<float4> gWorldSpriteTextures[3968] : register(t20);
SamplerState gWorldSpriteSampler : register(s0);

cbuffer CameraConstants : register(b0)
{
    float4x4 gViewProjection;
    float4x4 gInverseViewProjection;
    float4 gCameraPosition;
    float4 gTimeParams;
    float4 gSimulationTimeParams;
    float4 gScreenParams;
};

cbuffer WorldSpriteDrawConstants : register(b1)
{
    uint gWorldSpriteBaseIndex;
    uint gWorldSpriteCount;
    uint gWorldSpriteReserved0;
    uint gWorldSpriteReserved1;
};

struct WorldSpriteVertex
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint textureDescriptorIndex : TEXCOORD1;
    nointerpolation uint alphaMode : TEXCOORD2;
    nointerpolation float alphaCutoff : TEXCOORD3;
    float4 tintOpacity : COLOR0;
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
void WorldSpriteMS(
    uint3 groupId : SV_GroupID,
    uint groupThreadId : SV_GroupThreadID,
    out vertices WorldSpriteVertex outputVertices[4],
    out indices uint3 outputTriangles[2])
{
    const uint localIndex = groupId.x;
    const bool validSprite = localIndex < gWorldSpriteCount;
    SetMeshOutputCounts(
        validSprite ? 4u : 0u,
        validSprite ? 2u : 0u);
    if (!validSprite)
    {
        return;
    }
    const WorldSpriteInstance instance =
        gWorldSprites[gWorldSpriteBaseIndex + localIndex];

    float3 cameraRight;
    float3 cameraUp;
    float3 cameraForward;
    ResolveCameraBasis(cameraRight, cameraUp, cameraForward);
    float3 right = cameraRight;
    float3 up = cameraUp;
    if (instance.facingMode == HIKARI_WORLD_SPRITE_FIXED)
    {
        const float4 orientation = NormalizeQuaternion(
            instance.orientation);
        right = NormalizeOr(
            RotateByQuaternion(
                float3(1.0f, 0.0f, 0.0f), orientation),
            cameraRight);
        up = NormalizeOr(
            RotateByQuaternion(
                float3(0.0f, 1.0f, 0.0f), orientation),
            cameraUp);
    }
    else if (instance.facingMode == HIKARI_WORLD_SPRITE_FIXED_Y_AXIS)
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

    static const float2 uvs[4] = {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f)
    };
    const float2 uv = uvs[groupThreadId];
    const float2 normalizedLocal =
        float2(uv.x, 1.0f - uv.y) - instance.sizePivot.zw;
    const float2 localPosition = normalizedLocal * instance.sizePivot.xy;
    const float3 worldPosition = instance.position.xyz +
        right * localPosition.x + up * localPosition.y;

    WorldSpriteVertex output = (WorldSpriteVertex)0;
    output.position = mul(
        gViewProjection,
        float4(worldPosition, 1.0f));
    output.uv = uv;
    output.textureDescriptorIndex = instance.textureDescriptorIndex;
    output.alphaMode = instance.alphaMode;
    output.alphaCutoff = instance.alphaCutoff;
    output.tintOpacity = instance.tintOpacity;
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

float4 WorldSpritePS(WorldSpriteVertex input) : SV_TARGET
{
    const uint textureIndex = NonUniformResourceIndex(
        input.textureDescriptorIndex);
    const float4 sampled = gWorldSpriteTextures[textureIndex].Sample(
        gWorldSpriteSampler,
        input.uv);
    const float alpha = saturate(
        sampled.a * input.tintOpacity.a);
    float3 color = sampled.rgb * input.tintOpacity.rgb;
    if (input.alphaMode == HIKARI_WORLD_SPRITE_ALPHA_MASK)
    {
        clip(alpha - input.alphaCutoff);
        return float4(color, 1.0f);
    }
    if (input.alphaMode == HIKARI_WORLD_SPRITE_ALPHA_OPAQUE)
    {
        return float4(color, 1.0f);
    }
    if (input.alphaMode == HIKARI_WORLD_SPRITE_ALPHA_HYBRID_CORE)
    {
        clip(alpha - HIKARI_WORLD_SPRITE_OPAQUE_CORE_ALPHA);
        return float4(color, 1.0f);
    }
    if (input.alphaMode == HIKARI_WORLD_SPRITE_ALPHA_HYBRID_FRINGE)
    {
        clip(alpha - (1.0f / 255.0f));
        clip(HIKARI_WORLD_SPRITE_OPAQUE_CORE_ALPHA - alpha - 1.0e-5f);
    }
    return float4(color * alpha, alpha);
}
