#ifndef HIKARI_PROJECT_SPRITE_INCLUDED
#define HIKARI_PROJECT_SPRITE_INCLUDED

#include "../Include/Math/HIKARI_BillboardMath.hlsli"
#include "../Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "../Include/Lighting/HIKARI_DepthOfFieldMath.hlsli"
#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "../Include/HIKARI_MeshMaterialData.hlsli"
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
    float4 position; // xyz: world anchor, w: cast/receive/rotation/flip bits (exact integer)
    float4 orientation;
    float4 sizePivot;
    float4 tintOpacity;
    float4 uvRect;
    float4 surface;
    uint textureDescriptorIndex;
    uint facingMode;
    uint alphaMode;
    float alphaCutoff;
    uint normalTextureDescriptorIndex;
    uint3 reserved;
    float4 sourceCanvasRect; // trim offset in source pixels, followed by untrimmed source dimensions
};

StructuredBuffer<WorldSpriteInstance> gWorldSprites : register(t1);
StructuredBuffer<float4> gWorldSpriteParameters : register(t2);
SamplerState gWorldSpriteSampler : register(s0);

#include "../Include/Render3D/HIKARI_MeshCameraData.hlsli"
#define gViewProjection gViewProj
#define gInverseViewProjection gInvViewProj
#define gCameraPosition gCameraPos
#define gLinearWrap gWorldSpriteSampler
#define gLinearClamp gWorldSpriteSampler
#define gSkyPanoramaSampler gWorldSpriteSampler
#include "../Include/Lighting/HIKARI_ViewLightingData.hlsli"
#include "../Include/Lighting/HIKARI_LocalLightList.hlsli"
#include "../Include/HIKARI_SkyEnvironmentCommon.hlsli"
Texture2D<float> gShadowMap : register(t0, space9);
SamplerComparisonState gShadowSampler : register(s2);
static const uint HIKARI_FORWARD_COST_NO_SHADOW = 3u;
#include "../Include/Forward/HIKARI_ShadowSampling.hlsli"
#include "../Include/Lighting/HIKARI_SurfaceDirectLighting.hlsli"
#include "../Include/Lighting/HIKARI_SimpleSurfaceLighting.hlsli"

cbuffer WorldSpriteDrawConstants : register(b1)
{
    uint gWorldSpriteBaseIndex;
    uint gWorldSpriteCount;
    uint gWorldSpriteReserved0;
    uint gWorldSpritePassKind; // 0: color, 1: surface, 2: depth, 3: shadow, 4: DOF capture
    float gWorldSpriteLogicalTimeSeconds;
};

cbuffer WorldSpritePassData : register(b3) { float4x4 gWorldSpritePassData; };

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
    nointerpolation uint spriteFlags : TEXCOORD8;
    nointerpolation uint normalTextureDescriptorIndex : TEXCOORD9;
    float3 tangent : TEXCOORD10;
    nointerpolation uint parameterBlockIndex : TEXCOORD11;
    nointerpolation float4 sourceCanvasRect : TEXCOORD12;
};

// This is the stable project-facing contract. Engine-reserved members are read only through helpers.
struct HikariSpriteShaderInput
{
    float2 canvasUV;
    float2 canvasSizePixels;
    float2 trimOffsetPixels;
    float2 trimSizePixels;
    float3 sourceRgb;
    float sourceAlpha;
    float timeSeconds;
    bool rotatedClockwise;
    bool flipX;
    uint _engineParameterBlockIndex;
    uint _engineTextureDescriptorIndex;
    float4 _engineAtlasUvRect;
};

#if !defined(HIKARI_WORLD_SPRITE_BUILTIN)
float3 HikariProjectSpriteColor(HikariSpriteShaderInput input);
#endif

float4 HikariSpriteGetParameter(HikariSpriteShaderInput input, uint slot)
{
    if (slot >= 16u)
    {
        return 0.0f.xxxx;
    }
    return gWorldSpriteParameters[input._engineParameterBlockIndex * 16u + slot];
}

// Callback output is decoded linear albedo RGB in [0,1] before tint/lighting. The wrapper owns original alpha.
// Projects must not clip/discard or write alpha/depth; the fixed HSHADERC entries preserve coverage and DOF.

WorldSpriteVertex BuildWorldSpriteVertex(uint localIndex, uint vertexIndex) {
    const WorldSpriteInstance instance =
        gWorldSprites[gWorldSpriteBaseIndex + localIndex];

    float3 cameraRight;
    float3 cameraUp;
    float3 cameraForward;
    HikariResolveBillboardCameraBasis(gInverseViewProjection, cameraRight, cameraUp, cameraForward);
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
    const uint spriteFlags = uint(instance.position.w);
    const bool flipX = (spriteFlags & 8u) != 0u;
    const float2 normalizedLocal =
        float2(uv.x, 1.0f - uv.y) - instance.sizePivot.zw;
    const float2 localPosition = normalizedLocal * instance.sizePivot.xy;
    const float3 worldPosition = instance.position.xyz +
        right * localPosition.x + up * localPosition.y;

    WorldSpriteVertex output = (WorldSpriteVertex)0;
    output.position = mul(
        gWorldSpritePassKind == 3u ? gWorldSpritePassData : gViewProjection,
        float4(worldPosition, 1.0f));
    output.uv = instance.uvRect.xy +
        HikariMapPackedSpriteUv(uv, (spriteFlags & 4u) != 0u, flipX) * instance.uvRect.zw;
    output.uvRect = instance.uvRect;
    output.surface = instance.surface;
    output.spriteFlags = spriteFlags;
    output.worldPosition = worldPosition;
    // D3D の clockwise front face と右手系の screen basis を一致させる。
    // 負号を付けると SV_IsFrontFace で反転した後も常に裏面を照明してしまう。
    output.normal = NormalizeOr(cross(right, up), float3(0,0,1));
    output.tangent = right;
    output.textureDescriptorIndex = instance.textureDescriptorIndex;
    output.normalTextureDescriptorIndex = instance.normalTextureDescriptorIndex;
    output.alphaMode = instance.alphaMode;
    output.alphaCutoff = instance.alphaCutoff;
    output.tintOpacity = instance.tintOpacity;
    output.parameterBlockIndex = instance.reserved.x;
    output.sourceCanvasRect = instance.sourceCanvasRect;
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

#if !defined(HIKARI_WORLD_SPRITE_BUILTIN)
HikariSpriteShaderInput BuildHikariSpriteShaderInput(WorldSpriteVertex input, float3 sourceRgb, float sourceAlpha)
{
    const bool rotatedClockwise = (input.spriteFlags & 4u) != 0u;
    const bool flipX = (input.spriteFlags & 8u) != 0u;
    const uint textureIndex = NonUniformResourceIndex(input.textureDescriptorIndex);
    uint pageWidth = 1u;
    uint pageHeight = 1u;
    gWorldSpriteTextures[textureIndex].GetDimensions(pageWidth, pageHeight);
    const float2 pageSize = max(float2(pageWidth, pageHeight), 1.0f.xx);
    const float2 packedSize = input.uvRect.zw * pageSize;
    const float2 trimSize = rotatedClockwise ? packedSize.yx : packedSize;
    float2 trimOffset = input.sourceCanvasRect.xy;
    float2 canvasSize = input.sourceCanvasRect.zw;
    if (canvasSize.x <= 0.0f || canvasSize.y <= 0.0f)
    {
        // Legacy static textures and grid frames define their canvas as the current UV rectangle.
        trimOffset = 0.0f.xx;
        canvasSize = trimSize;
    }

    const float2 packedUv = saturate((input.uv - input.uvRect.xy) / max(input.uvRect.zw, 1.0e-8f.xx));
    const float2 geometryUv = HikariUnmapPackedSpriteUv(packedUv, rotatedClockwise, flipX);
    float2 displayedTrimOffset = trimOffset;
    if (flipX)
    {
        displayedTrimOffset.x = canvasSize.x - trimOffset.x - trimSize.x;
    }
    const float2 canvasPixels = displayedTrimOffset + geometryUv * trimSize;

    HikariSpriteShaderInput result = (HikariSpriteShaderInput)0;
    result.canvasUV = canvasPixels / max(canvasSize, 1.0e-8f.xx);
    result.canvasSizePixels = canvasSize;
    result.trimOffsetPixels = trimOffset;
    result.trimSizePixels = trimSize;
    result.sourceRgb = sourceRgb;
    result.sourceAlpha = sourceAlpha;
    result.timeSeconds = gWorldSpriteLogicalTimeSeconds;
    result.rotatedClockwise = rotatedClockwise;
    result.flipX = flipX;
    result._engineParameterBlockIndex = input.parameterBlockIndex;
    result._engineTextureDescriptorIndex = input.textureDescriptorIndex;
    result._engineAtlasUvRect = input.uvRect;
    return result;
}

float4 HikariSpriteSampleSource(HikariSpriteShaderInput input, float2 canvasUV, bool pointSampling)
{
    if (!all(isfinite(canvasUV)) || any(canvasUV < 0.0f.xx) || any(canvasUV > 1.0f.xx))
    {
        return 0.0f.xxxx;
    }
    float2 sourceCanvasPixel = canvasUV * input.canvasSizePixels;
    if (input.flipX)
    {
        sourceCanvasPixel.x = input.canvasSizePixels.x - sourceCanvasPixel.x;
    }
    const float2 trimPixel = sourceCanvasPixel - input.trimOffsetPixels;
    if (any(trimPixel < 0.0f.xx) || any(trimPixel >= input.trimSizePixels))
    {
        return 0.0f.xxxx;
    }
    const float2 sourceUv = trimPixel / max(input.trimSizePixels, 1.0e-8f.xx);
    const float2 packedUv = HikariMapPackedSpriteUv(sourceUv, input.rotatedClockwise, false);
    const float2 atlasUv = input._engineAtlasUvRect.xy + packedUv * input._engineAtlasUvRect.zw;
    return SampleWorldSpriteAlphaAware(input._engineTextureDescriptorIndex, atlasUv,
                                       input._engineAtlasUvRect, pointSampling);
}

float4 HikariSpriteSampleSource(HikariSpriteShaderInput input, float2 canvasUV)
{
    return HikariSpriteSampleSource(input, canvasUV, false);
}
#endif

float3 WorldSpriteResolveProjectColor(WorldSpriteVertex input, float3 sourceRgb, float sourceAlpha)
{
#if defined(HIKARI_WORLD_SPRITE_BUILTIN)
    return sourceRgb * input.tintOpacity.rgb;
#else
    const HikariSpriteShaderInput shaderInput = BuildHikariSpriteShaderInput(input, sourceRgb, sourceAlpha);
    const float3 projectColor = HikariProjectSpriteColor(shaderInput);
    const float3 safeColor = all(isfinite(projectColor)) ? saturate(projectColor) : saturate(sourceRgb);
    return safeColor * input.tintOpacity.rgb;
#endif
}

// 色・深度・表面・影で同じ texture/atlas/alpha coverage を使用する。
float WorldSpriteCoverage(WorldSpriteVertex input, out float3 sourceColor, out float sourceAlpha)
{
    const float4 sampled = SampleWorldSpriteAlphaAware(input.textureDescriptorIndex,
        input.uv, input.uvRect, input.surface.w > 0.5f);
    sourceAlpha = sampled.a;
    sourceColor = sampled.rgb;
    const float alpha = saturate(sourceAlpha * input.tintOpacity.a);
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

float3 WorldSpriteResolveShadingNormal(WorldSpriteVertex input, bool frontFace)
{
    if (input.normalTextureDescriptorIndex == 0xffffffffu) {
        return frontFace ? input.normal : -input.normal;
    }
    const uint normalResourceIndex = NonUniformResourceIndex(input.normalTextureDescriptorIndex);
    float3 encodedNormal;
    if (input.surface.w > 0.5f) {
        uint width;
        uint height;
        gWorldSpriteTextures[normalResourceIndex].GetDimensions(width, height);
        const uint2 dimensions = max(uint2(width, height), uint2(1u, 1u));
        const int2 firstPixel = int2(round(input.uvRect.xy * dimensions));
        const int2 lastPixel = min(int2(round((input.uvRect.xy + input.uvRect.zw) * dimensions)) - 1,
                                   int2(dimensions) - 1);
        encodedNormal = gWorldSpriteTextures[normalResourceIndex].Load(
            int3(clamp(int2(input.uv * dimensions), firstPixel, lastPixel), 0)).rgb;
    } else {
        encodedNormal = gWorldSpriteTextures[normalResourceIndex].Sample(gWorldSpriteSampler, input.uv).rgb;
    }
    return HikariResolveWorldSpriteNormal(encodedNormal * 2.0f - 1.0f, input.tangent, input.normal, frontFace,
        (input.spriteFlags & 8u) != 0u);
}

float3 WorldSpriteApplyLighting(WorldSpriteVertex input, bool frontFace, float3 color)
{
    if (input.surface.x > 0.5f && gWorldSpriteReserved0 != 0u) {
        const float3 geometricNormal = frontFace ? input.normal : -input.normal;
        const float3 shadingNormal = WorldSpriteResolveShadingNormal(input, frontFace);
        color = HikariShadeSimpleSurface(color,0.0f,input.surface.y,1.0f.xxx,input.surface.z,1.0f,
            shadingNormal,geometricNormal,input.worldPosition,input.position.xy,(input.spriteFlags >> 1u) & 1u);
    }
    return color;
}

float4 WorldSpritePS(WorldSpriteVertex input, bool frontFace : SV_IsFrontFace) : SV_TARGET
{
    float3 sourceColor;
    float sourceAlpha;
    const float alpha = WorldSpriteCoverage(input, sourceColor, sourceAlpha);
    float3 color = WorldSpriteResolveProjectColor(input, sourceColor, sourceAlpha);
    color = WorldSpriteApplyLighting(input, frontFace, color);
    return float4(color * alpha, alpha);
}

float4 SpriteColorPS(WorldSpriteVertex input, bool frontFace : SV_IsFrontFace) : SV_TARGET
{
    float3 sourceColor;
    float sourceAlpha;
    const float alpha = WorldSpriteCoverage(input, sourceColor, sourceAlpha);
    float3 color = WorldSpriteResolveProjectColor(input, sourceColor, sourceAlpha);
    color = WorldSpriteApplyLighting(input, frontFace, color);
    return float4(color * alpha, alpha);
}

struct WorldSpriteDepthOfFieldOutput
{
    float4 nearColor : SV_TARGET0;
    float4 farColor : SV_TARGET1;
    float4 sharpColor : SV_TARGET2;
    float4 nearMetadata : SV_TARGET3;
    float4 farMetadata : SV_TARGET4;
    float4 sharpMetadata : SV_TARGET5;
};

WorldSpriteDepthOfFieldOutput WorldSpriteDepthOfFieldPS(WorldSpriteVertex input,
                                                        bool frontFace : SV_IsFrontFace)
{
    WorldSpriteDepthOfFieldOutput output = (WorldSpriteDepthOfFieldOutput)0;
    float3 sourceColor;
    float sourceAlpha;
    const float alpha = WorldSpriteCoverage(input, sourceColor, sourceAlpha);
    float3 color = WorldSpriteResolveProjectColor(input, sourceColor, sourceAlpha);
    color = WorldSpriteApplyLighting(input, frontFace, color);

    const float4 focusBand = gWorldSpritePassData[0];
    const float4 blurAndSize = gWorldSpritePassData[1];
    const float viewDepth = HikariCameraViewDepth(input.worldPosition);
    const float2 coc = HikariDepthOfFieldCoc(viewDepth, focusBand, blurAndSize.xy);
    const float nearCoc = coc.x;
    const float farCoc = coc.y;
    // 一つのcoverageを複数laneへ分割すると、後段のpremultiplied overでalphaが二重に減る。
    // ぼけ半径そのもので連続性を持たせ、各pixelは一つのlaneにだけ出力する。
    if (nearCoc >= 0.5f)
    {
        output.nearColor = float4(color * alpha, alpha);
        output.nearMetadata = float4(viewDepth * alpha, nearCoc * alpha, 0.0f, alpha);
    }
    else if (farCoc >= 0.5f)
    {
        output.farColor = float4(color * alpha, alpha);
        output.farMetadata = float4(viewDepth * alpha, farCoc * alpha, 0.0f, alpha);
    }
    else
    {
        output.sharpColor = float4(color * alpha, alpha);
        output.sharpMetadata = float4(viewDepth * alpha, 0.0f, 0.0f, alpha);
    }
    return output;
}

WorldSpriteDepthOfFieldOutput SpriteDepthOfFieldPS(WorldSpriteVertex input,
                                                   bool frontFace : SV_IsFrontFace)
{
    return WorldSpriteDepthOfFieldPS(input, frontFace);
}

void WorldSpriteDepthPS(WorldSpriteVertex input)
{
    if (gWorldSpritePassKind == 3u && (input.spriteFlags & 1u) == 0u) discard;
    float3 ignoredColor;
    float ignoredAlpha;
    WorldSpriteCoverage(input, ignoredColor, ignoredAlpha);
}

#include "../Include/Surface/HIKARI_SceneSurfaceContract.hlsli"
float4 WorldSpriteSurfacePS(WorldSpriteVertex input, bool frontFace : SV_IsFrontFace) : SV_TARGET0
{
    float3 ignoredColor;
    float ignoredAlpha;
    WorldSpriteCoverage(input, ignoredColor, ignoredAlpha);
    return HikariPackSceneSurface(WorldSpriteResolveShadingNormal(input, frontFace), input.surface.y, 0.0f);
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

#endif // HIKARI_PROJECT_SPRITE_INCLUDED
