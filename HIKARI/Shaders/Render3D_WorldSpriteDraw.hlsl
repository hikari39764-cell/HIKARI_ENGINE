#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshMaterialData.hlsli"
#define gWorldSpriteTextures gMaterialTexturePool

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
    float4 position; // xyz: world anchor, w: cast/receive shadow bits (exact integer)
    float4 orientation;
    float4 sizePivot;
    float4 tintOpacity;
    float4 uvRect;
    float4 surface;
    uint textureDescriptorIndex;
    uint facingMode;
    uint alphaMode;
    float alphaCutoff;
};

StructuredBuffer<WorldSpriteInstance> gWorldSprites : register(t1);
SamplerState gWorldSpriteSampler : register(s0);

#include "Include/Render3D/HIKARI_MeshCameraData.hlsli"
#define gViewProjection gViewProj
#define gInverseViewProjection gInvViewProj
#define gCameraPosition gCameraPos
#define gLinearWrap gWorldSpriteSampler
#define gLinearClamp gWorldSpriteSampler
#define gSkyPanoramaSampler gWorldSpriteSampler
#include "Include/Lighting/HIKARI_ViewLightingData.hlsli"
#include "Include/Lighting/HIKARI_LocalLightList.hlsli"
#include "Include/HIKARI_SkyEnvironmentCommon.hlsli"
Texture2D<float> gShadowMap : register(t0, space9);
SamplerComparisonState gShadowSampler : register(s2);
static const uint HIKARI_FORWARD_COST_NO_SHADOW = 3u;
#include "Include/Forward/HIKARI_ShadowSampling.hlsli"
#include "Include/Lighting/HIKARI_SurfaceDirectLighting.hlsli"
#include "Include/Lighting/HIKARI_SimpleSurfaceLighting.hlsli"

cbuffer WorldSpriteDrawConstants : register(b1)
{
    uint gWorldSpriteBaseIndex;
    uint gWorldSpriteCount;
    uint gWorldSpriteReserved0;
    uint gWorldSpritePassKind; // 0: color, 1: surface, 2: depth, 3: shadow
};

cbuffer WorldSpriteShadowView : register(b3) { float4x4 gWorldSpriteLightViewProj; };

struct WorldSpriteVertex
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint textureDescriptorIndex : TEXCOORD1;
    nointerpolation uint alphaMode : TEXCOORD2;
    nointerpolation float alphaCutoff : TEXCOORD3;
    float4 tintOpacity : COLOR0;
    float3 worldPosition : TEXCOORD4;
    float3 normal : TEXCOORD5;
    nointerpolation float4 uvRect : TEXCOORD6;
    nointerpolation float4 surface : TEXCOORD7;
    nointerpolation uint shadowFlags : TEXCOORD8;
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

WorldSpriteVertex BuildWorldSpriteVertex(uint localIndex, uint vertexIndex) {
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
        // 正交投影では全 Sprite に同じ視線を使い、位置ごとの傾きと受光差を防ぐ。
        const float3 toCamera = HikariCameraViewDirection(instance.position.xyz);
        const float3 normal = NormalizeOr(
            float3(toCamera.x, 0.0f, toCamera.z),
            float3(0.0f, 0.0f, -1.0f));
        right = NormalizeOr(cross(up, normal), cameraRight);
    }

    static const float2 uvs[4] = {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f)
    };
    const float2 uv = uvs[vertexIndex];
    const float2 normalizedLocal =
        float2(uv.x, 1.0f - uv.y) - instance.sizePivot.zw;
    const float2 localPosition = normalizedLocal * instance.sizePivot.xy;
    const float3 worldPosition = instance.position.xyz +
        right * localPosition.x + up * localPosition.y;

    WorldSpriteVertex output = (WorldSpriteVertex)0;
    output.position = mul(
        gWorldSpritePassKind == 3u ? gWorldSpriteLightViewProj : gViewProjection,
        float4(worldPosition, 1.0f));
    output.uv = instance.uvRect.xy + uv * instance.uvRect.zw;
    output.uvRect = instance.uvRect;
    output.surface = instance.surface;
    output.shadowFlags = uint(instance.position.w);
    output.worldPosition = worldPosition;
    // D3D の clockwise front face と右手系の screen basis を一致させる。
    // 負号を付けると SV_IsFrontFace で反転した後も常に裏面を照明してしまう。
    output.normal = NormalizeOr(cross(right, up), float3(0,0,1));
    output.textureDescriptorIndex = instance.textureDescriptorIndex;
    output.alphaMode = instance.alphaMode;
    output.alphaCutoff = instance.alphaCutoff;
    output.tintOpacity = instance.tintOpacity;
    return output;
}

[outputtopology("triangle")]
[numthreads(4,1,1)]
void WorldSpriteMS(uint3 groupId : SV_GroupID, uint tid : SV_GroupThreadID,
    out vertices WorldSpriteVertex vertices[4], out indices uint3 triangles[2]) {
    const bool valid = groupId.x < gWorldSpriteCount;
    SetMeshOutputCounts(valid ? 4u : 0u, valid ? 2u : 0u);
    if (!valid) return;
    vertices[tid] = BuildWorldSpriteVertex(groupId.x, tid);
    if (tid == 0) triangles[0] = uint3(0,1,2);
    if (tid == 1) triangles[1] = uint3(0,2,3);
}
WorldSpriteVertex WorldSpriteVS(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    static const uint indices[6] = {0,1,2,0,2,3};
    return BuildWorldSpriteVertex(instance, indices[vertex]);
}

float4 LoadWorldSpritePremultiplied(uint textureIndex, int2 pixel, int2 firstPixel, int2 lastPixel)
{
    const float4 texel = gWorldSpriteTextures[NonUniformResourceIndex(textureIndex)].Load(
        int3(clamp(pixel, firstPixel, lastPixel), 0));
    return float4(texel.rgb * texel.a, texel.a);
}

float4 SampleWorldSpriteAlphaAware(uint textureIndex, float2 uv, float4 uvRect, bool pointSampling)
{
    uint width = 1u, height = 1u;
    gWorldSpriteTextures[NonUniformResourceIndex(textureIndex)].GetDimensions(width, height);
    const uint2 dimensions = max(uint2(width, height), uint2(1u, 1u));
    const int2 firstPixel = int2(round(uvRect.xy * dimensions));
    const int2 lastPixel = min(int2(round((uvRect.xy+uvRect.zw)*dimensions))-1, int2(dimensions)-1);
    if (pointSampling) return gWorldSpriteTextures[NonUniformResourceIndex(textureIndex)].Load(
        int3(clamp(int2(uv*dimensions), firstPixel, lastPixel),0));
    const float2 pixel = saturate(uv) * float2(dimensions) - 0.5f;
    const int2 basePixel = int2(floor(pixel));
    const float2 blend = frac(pixel);
    const float4 p00 = LoadWorldSpritePremultiplied(textureIndex, basePixel, firstPixel, lastPixel);
    const float4 p10 = LoadWorldSpritePremultiplied(textureIndex, basePixel + int2(1, 0), firstPixel, lastPixel);
    const float4 p01 = LoadWorldSpritePremultiplied(textureIndex, basePixel + int2(0, 1), firstPixel, lastPixel);
    const float4 p11 = LoadWorldSpritePremultiplied(textureIndex, basePixel + int2(1, 1), firstPixel, lastPixel);
    const float4 filtered = lerp(lerp(p00, p10, blend.x), lerp(p01, p11, blend.x), blend.y);
    return float4(filtered.rgb / max(filtered.a, 1.0e-6f), filtered.a);
}

// 色・深度・表面・影で同じ texture/atlas/alpha coverage を使用する。
float WorldSpriteCoverage(WorldSpriteVertex input, out float3 baseColor)
{
    const float4 sampled = SampleWorldSpriteAlphaAware(input.textureDescriptorIndex,
        input.uv, input.uvRect, input.surface.w > 0.5f);
    const float alpha = saturate(sampled.a * input.tintOpacity.a);
    baseColor = sampled.rgb * input.tintOpacity.rgb;
    if (input.alphaMode == HIKARI_WORLD_SPRITE_ALPHA_MASK) {
        clip(alpha - input.alphaCutoff);
        return 1.0f;
    }
    if (input.alphaMode == HIKARI_WORLD_SPRITE_ALPHA_OPAQUE) return 1.0f;
    if (input.alphaMode == HIKARI_WORLD_SPRITE_ALPHA_HYBRID_CORE) {
        clip(alpha - HIKARI_WORLD_SPRITE_OPAQUE_CORE_ALPHA);
        return 1.0f;
    }
    if (input.alphaMode == HIKARI_WORLD_SPRITE_ALPHA_HYBRID_FRINGE) {
        clip(alpha - (1.0f / 255.0f));
        clip(HIKARI_WORLD_SPRITE_OPAQUE_CORE_ALPHA - alpha - 1.0e-5f);
    }
    return alpha;
}

float4 WorldSpritePS(WorldSpriteVertex input, bool frontFace : SV_IsFrontFace) : SV_TARGET
{
    float3 color;
    const float alpha = WorldSpriteCoverage(input, color);
    if (input.surface.x > 0.5f && gWorldSpriteReserved0 != 0u) {
        const float3 normal = frontFace ? input.normal : -input.normal;
        color = HikariShadeSimpleSurface(color,0.0f,input.surface.y,1.0f.xxx,input.surface.z,1.0f,
            normal,normal,input.worldPosition,input.position.xy,(input.shadowFlags >> 1u) & 1u);
    }
    return float4(color * alpha, alpha);
}

void WorldSpriteDepthPS(WorldSpriteVertex input)
{
    if (gWorldSpritePassKind == 3u && (input.shadowFlags & 1u) == 0u) discard;
    float3 ignored;
    WorldSpriteCoverage(input, ignored);
}

#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"
float4 WorldSpriteSurfacePS(WorldSpriteVertex input, bool frontFace : SV_IsFrontFace) : SV_TARGET0
{
    float3 ignored;
    WorldSpriteCoverage(input, ignored);
    return HikariPackSceneSurface(frontFace ? input.normal : -input.normal, input.surface.y, 0.0f);
}
struct WorldSpriteSurfaceOutput {
    float4 surface : SV_TARGET0;
    uint2 features : SV_TARGET1;
    float4 outlineStyle : SV_TARGET2;
};
WorldSpriteSurfaceOutput WorldSpriteSurfaceFeaturePS(WorldSpriteVertex input, bool frontFace : SV_IsFrontFace)
{
    WorldSpriteSurfaceOutput output = (WorldSpriteSurfaceOutput)0;
    output.surface = WorldSpriteSurfacePS(input, frontFace);
    return output;
}
