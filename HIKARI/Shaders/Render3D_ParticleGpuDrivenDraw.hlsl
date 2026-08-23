#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"
#include "Include/Material/HIKARI_MaterialExpression.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"

static const uint HIKARI_PARTICLE_ROTATED_BILLBOARD = 0u;
static const uint HIKARI_PARTICLE_FIXED_Y_AXIS = 1u;
static const uint HIKARI_PARTICLE_DIRECTIONAL_BILLBOARD = 2u;
static const uint HIKARI_PARTICLE_FIXED = 3u;
static const uint HIKARI_PARTICLE_MAX_SEGMENTS = 64u;
static const uint HIKARI_PARTICLE_MATERIAL_ALPHA_MASK = 1u << 1u;
static const uint HIKARI_PARTICLE_DEPTH_WORLD_OCCLUDED = 0u;
static const uint HIKARI_PARTICLE_DEPTH_SOFT_INTERSECTION = 1u;
static const uint HIKARI_PARTICLE_DEPTH_CONTACT_SURFACE = 2u;
static const uint HIKARI_PARTICLE_DEPTH_FOREGROUND_ACCENT = 3u;
static const uint HIKARI_PARTICLE_STYLE_DEFAULT = 0u;
static const uint HIKARI_PARTICLE_STYLE_COMBAT_CUT = 1u;
static const uint HIKARI_PARTICLE_STYLE_COMBAT_IMPACT = 2u;
static const uint HIKARI_PARTICLE_STYLE_COMBAT_GROUND = 3u;
static const uint HIKARI_PARTICLE_STYLE_COMIC_SMOKE = 4u;
static const uint HIKARI_PARTICLE_STYLE_PROJECTILE_NEEDLE = 5u;

struct Particle
{
    float4 positionAge;
    float4 velocityLifetime;
    float4 physicsPositionSeed;
    float4 scaleRotation;
    float4 orientation;
    float4 color;
};

StructuredBuffer<Particle> gDrawParticles : register(t1);
StructuredBuffer<uint> gAliveParticleIndices : register(t2);
Texture2D<float> gParticleSceneDepth : register(t3);
SamplerState gParticleSampler : register(s0);
SamplerState gParticleDepthSampler : register(s1);

cbuffer CameraConstants : register(b0)
{
    float4x4 gViewProjection;
    float4x4 gInverseViewProjection;
    float4 gCameraPosition;
    float4 gTimeParams;
    float4 gSimulationTimeParams;
    float4 gScreenParams;
};

cbuffer ParticleDrawConstants : register(b1)
{
    uint gAliveIndexBase;
    uint gBillboardMode;
    uint gRenderShapeMode;
    uint gSegmentCount;
    float4 gRenderShapeParamsA;
    float4 gRenderShapeParamsB;
    uint gDepthMode;
    uint gShaderStyle;
    uint gSceneDepthAvailable;
    uint gParticleReserved;
    float4 gDepthAndStyleParams;
    float4 gShaderParams;
    float4 gShaderOutlineColor;
    float4 gShaderCoreColor;
};

struct ParticleVertex
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float3 worldPosition : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float3 worldTangent : TEXCOORD3;
    float2 localUv : TEXCOORD4;
    nointerpolation uint flipbookFrame : TEXCOORD5;
    nointerpolation float normalizedAge : TEXCOORD6;
    nointerpolation float randomValue : TEXCOORD7;
};

float4 SampleParticleAtlasTexture(
    uint descriptorIndex,
    float2 localUv,
    uint frame,
    uint columns,
    uint rows,
    float4 fallbackValue)
{
    if (!HikariHasMaterialTexture(descriptorIndex))
    {
        return fallbackValue;
    }

    const uint resourceIndex = NonUniformResourceIndex(descriptorIndex);
    uint textureWidth = 1u;
    uint textureHeight = 1u;
    uint mipCount = 1u;
    gMaterialTexturePool[resourceIndex].GetDimensions(
        0u,
        textureWidth,
        textureHeight,
        mipCount);
    const float2 textureSize = max(
        float2(textureWidth, textureHeight),
        1.0f.xx);
    const float2 grid = float2(max(columns, 1u), max(rows, 1u));
    const float2 frameSize = textureSize / grid;
    const float2 atlasDx = ddx(localUv) / grid;
    const float2 atlasDy = ddy(localUv) / grid;
    const float footprint = max(
        dot(atlasDx * textureSize, atlasDx * textureSize),
        dot(atlasDy * textureSize, atlasDy * textureSize));
    const float requestedLod = max(0.5f * log2(max(footprint, 1.0f)), 0.0f);
    const float maximumCellLod = max(
        floor(log2(max(min(frameSize.x, frameSize.y), 1.0f))) - 1.0f,
        0.0f);
    const float lod = min(
        requestedLod,
        min(maximumCellLod, float(max(mipCount, 1u) - 1u)));
    const float2 mipInset = 0.5f * exp2(lod) / textureSize;
    const float2 cell = float2(frame % columns, frame / columns);
    const float2 cellMinimum = cell / grid + mipInset;
    const float2 cellMaximum = (cell + 1.0f.xx) / grid - mipInset;
    const float2 safeUv = lerp(
        cellMinimum,
        max(cellMinimum, cellMaximum),
        saturate(localUv));
    return gMaterialTexturePool[resourceIndex].SampleLevel(
        gParticleSampler,
        safeUv,
        lod);
}

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

void ResolveBillboardBasis(
    Particle particle,
    out float3 right,
    out float3 up,
    out float3 normal)
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
    const float3 cameraRight = NormalizeOr(
        rightH.xyz - centerH.xyz,
        float3(1.0f, 0.0f, 0.0f));
    const float3 cameraUp = NormalizeOr(
        upH.xyz - centerH.xyz,
        float3(0.0f, 1.0f, 0.0f));
    const float3 cameraForward = NormalizeOr(
        cross(cameraRight, cameraUp),
        float3(0.0f, 0.0f, 1.0f));
    const float3 toCamera = NormalizeOr(
        gCameraPosition.xyz - particle.positionAge.xyz,
        -cameraForward);

    right = cameraRight;
    up = cameraUp;
    normal = toCamera;
    if (gBillboardMode == HIKARI_PARTICLE_FIXED)
    {
        const float4 orientation = NormalizeQuaternion(particle.orientation);
        right = NormalizeOr(
            RotateByQuaternion(float3(1.0f, 0.0f, 0.0f), orientation),
            cameraRight);
        up = NormalizeOr(
            RotateByQuaternion(float3(0.0f, 1.0f, 0.0f), orientation),
            cameraUp);
        normal = NormalizeOr(-cross(right, up), toCamera);
    }
    else if (gBillboardMode == HIKARI_PARTICLE_FIXED_Y_AXIS)
    {
        up = float3(0.0f, 1.0f, 0.0f);
        normal = NormalizeOr(
            float3(toCamera.x, 0.0f, toCamera.z),
            float3(0.0f, 0.0f, -1.0f));
        right = NormalizeOr(cross(normal, up), cameraRight);
    }
    else if (gBillboardMode == HIKARI_PARTICLE_DIRECTIONAL_BILLBOARD)
    {
        const float3 projectedDirection = particle.velocityLifetime.xyz -
            normal * dot(particle.velocityLifetime.xyz, normal);
        up = NormalizeOr(projectedDirection, cameraUp);
        right = NormalizeOr(cross(normal, up), cameraRight);
    }
    else
    {
        const float sine = sin(particle.scaleRotation.z);
        const float cosine = cos(particle.scaleRotation.z);
        right = NormalizeOr(
            cameraRight * cosine + cameraUp * sine,
            cameraRight);
        up = NormalizeOr(
            cameraRight * -sine + cameraUp * cosine,
            cameraUp);
        normal = NormalizeOr(-cross(right, up), normal);
    }
}

ParticleVertex BuildParticleVertex(
    Particle particle,
    uint vertexIndex,
    uint segmentCount,
    float3 baseRight,
    float3 baseUp,
    float3 baseNormal)
{
    ParticleVertex output = (ParticleVertex)0;
    float3 right = baseRight;
    float3 up = baseUp;
    float3 normal = baseNormal;

    float2 uv = 0.0f;
    float3 worldPosition = particle.positionAge.xyz;
    if (gRenderShapeMode == 0u)
    {
        static const float2 corners[4] = {
            float2(-0.5f, -0.5f),
            float2(-0.5f,  0.5f),
            float2( 0.5f,  0.5f),
            float2( 0.5f, -0.5f)
        };
        static const float2 uvs[4] = {
            float2(0.0f, 1.0f),
            float2(0.0f, 0.0f),
            float2(1.0f, 0.0f),
            float2(1.0f, 1.0f)
        };
        const float2 corner = corners[vertexIndex];
        uv = uvs[vertexIndex];
        worldPosition += right * corner.x * particle.scaleRotation.x +
            up * corner.y * particle.scaleRotation.y;
    }
    else
    {
        const uint pairIndex = vertexIndex >> 1u;
        const float side = (vertexIndex & 1u) == 0u ? -1.0f : 1.0f;
        const float s = (float)pairIndex / (float)segmentCount;
        uv = float2(s, side * 0.5f + 0.5f);
        if (gRenderShapeMode == 1u)
        {
            const float4 orientation = NormalizeQuaternion(
                particle.orientation);
            const float3 tangent = NormalizeOr(
                RotateByQuaternion(
                    float3(1.0f, 0.0f, 0.0f),
                    orientation),
                right);
            const float3 widthDirection = NormalizeOr(
                cross(normal, tangent),
                up);
            const float ribbonLength = max(particle.scaleRotation.x, 0.0f);
            const float halfWidth = max(
                gRenderShapeParamsA.x * 0.5f *
                    particle.scaleRotation.y,
                0.0f);
            worldPosition += tangent * ((s - 0.5f) * ribbonLength) +
                widthDirection * side * halfWidth;
            right = tangent;
        }
        else
        {
            const float angle = lerp(
                gRenderShapeParamsA.w,
                gRenderShapeParamsB.x,
                s);
            const float radius = side < 0.0f
                ? gRenderShapeParamsA.z
                : gRenderShapeParamsA.y;
            worldPosition += right * cos(angle) * radius *
                    particle.scaleRotation.x +
                up * sin(angle) * radius *
                    particle.scaleRotation.y;
        }
    }

    const uint flipbookGrid = asuint(gRenderShapeParamsB.z);
    const uint flipbookPlayback = asuint(gRenderShapeParamsB.w);
    const uint columns = max(flipbookGrid & 0xffu, 1u);
    const uint rows = max((flipbookGrid >> 8u) & 0xffu, 1u);
    const uint frameCount = clamp(
        flipbookPlayback & 0xffffu,
        1u,
        columns * rows);
    const uint flags = flipbookPlayback >> 16u;
    uint frame = (uint)floor(
        max(gRenderShapeParamsB.y, 0.0f) *
        max(particle.positionAge.w, 0.0f));
    if ((flags & 2u) != 0u)
    {
        frame += asuint(particle.physicsPositionSeed.w) % frameCount;
    }
    if ((flags & 1u) != 0u)
    {
        frame %= frameCount;
    }
    else
    {
        frame = min(frame, frameCount - 1u);
    }
    output.uv = (float2(frame % columns, frame / columns) + uv) /
        float2(columns, rows);
    output.localUv = uv;
    output.flipbookFrame = frame;
    output.position = mul(gViewProjection, float4(worldPosition, 1.0f));
    if (gDepthMode == HIKARI_PARTICLE_DEPTH_CONTACT_SURFACE)
    {
        output.position.z = max(
            0.0f,
            output.position.z -
                max(gDepthAndStyleParams.y, 0.0f) * output.position.w);
    }
    output.color = particle.color;
    output.worldPosition = worldPosition;
    output.worldNormal = normal;
    output.worldTangent = right;
    output.normalizedAge = saturate(
        max(particle.positionAge.w, 0.0f) /
        max(particle.velocityLifetime.w, 1.0e-5f));
    output.randomValue = frac(
        float(asuint(particle.physicsPositionSeed.w)) *
        0.00000011920928955078125f);
    return output;
}

groupshared Particle gMeshParticle;
groupshared float3 gMeshRight;
groupshared float3 gMeshUp;
groupshared float3 gMeshNormal;

[outputtopology("triangle")]
[numthreads(4, 1, 1)]
void ParticleSpriteMS(
    uint3 groupId : SV_GroupID,
    uint groupThreadId : SV_GroupThreadID,
    out vertices ParticleVertex verticesOut[4],
    out indices uint3 trianglesOut[2])
{
    if (groupThreadId == 0u)
    {
        const uint particleIndex =
            gAliveParticleIndices[gAliveIndexBase + groupId.x];
        gMeshParticle = gDrawParticles[particleIndex];
        ResolveBillboardBasis(
            gMeshParticle,
            gMeshRight,
            gMeshUp,
            gMeshNormal);
    }
    GroupMemoryBarrierWithGroupSync();
    SetMeshOutputCounts(4u, 2u);
    verticesOut[groupThreadId] = BuildParticleVertex(
        gMeshParticle,
        groupThreadId,
        1u,
        gMeshRight,
        gMeshUp,
        gMeshNormal);
    if (groupThreadId < 2u)
    {
        trianglesOut[groupThreadId] = groupThreadId == 0u
            ? uint3(0u, 1u, 2u)
            : uint3(0u, 2u, 3u);
    }
}

[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void ParticleStripMS(
    uint3 groupId : SV_GroupID,
    uint groupThreadId : SV_GroupThreadID,
    out vertices ParticleVertex verticesOut[130],
    out indices uint3 trianglesOut[128])
{
    if (groupThreadId == 0u)
    {
        const uint particleIndex =
            gAliveParticleIndices[gAliveIndexBase + groupId.x];
        gMeshParticle = gDrawParticles[particleIndex];
        ResolveBillboardBasis(
            gMeshParticle,
            gMeshRight,
            gMeshUp,
            gMeshNormal);
    }
    GroupMemoryBarrierWithGroupSync();
    const Particle particle = gMeshParticle;
    const uint segmentCount = gRenderShapeMode == 0u
        ? 1u
        : clamp(gSegmentCount, gRenderShapeMode == 1u ? 2u : 3u,
            HIKARI_PARTICLE_MAX_SEGMENTS);
    const uint vertexCount = gRenderShapeMode == 0u
        ? 4u
        : (segmentCount + 1u) * 2u;
    const uint primitiveCount = gRenderShapeMode == 0u
        ? 2u
        : segmentCount * 2u;
    SetMeshOutputCounts(vertexCount, primitiveCount);

    for (uint vertexIndex = groupThreadId;
         vertexIndex < vertexCount;
         vertexIndex += 64u)
    {
        verticesOut[vertexIndex] = BuildParticleVertex(
            particle,
            vertexIndex,
            segmentCount,
            gMeshRight,
            gMeshUp,
            gMeshNormal);
    }
    for (uint primitiveIndex = groupThreadId;
         primitiveIndex < primitiveCount;
         primitiveIndex += 64u)
    {
        if (gRenderShapeMode == 0u)
        {
            trianglesOut[primitiveIndex] = primitiveIndex == 0u
                ? uint3(0u, 1u, 2u)
                : uint3(0u, 2u, 3u);
        }
        else
        {
            const uint segment = primitiveIndex >> 1u;
            const uint baseVertex = segment * 2u;
            trianglesOut[primitiveIndex] = (primitiveIndex & 1u) == 0u
                ? uint3(baseVertex, baseVertex + 1u, baseVertex + 3u)
                : uint3(baseVertex, baseVertex + 3u, baseVertex + 2u);
        }
    }
}

float HikariParticleHash21(float2 value)
{
    const float3 p = frac(float3(value.xyx) * 0.1031f);
    const float3 q = p + dot(p, p.yzx + 33.33f);
    return frac((q.x + q.y) * q.z);
}

float HikariParticleValueNoise(float2 value)
{
    const float2 cell = floor(value);
    const float2 local = frac(value);
    const float2 blend = local * local * (3.0f - 2.0f * local);
    const float lower = lerp(
        HikariParticleHash21(cell),
        HikariParticleHash21(cell + float2(1.0f, 0.0f)),
        blend.x);
    const float upper = lerp(
        HikariParticleHash21(cell + float2(0.0f, 1.0f)),
        HikariParticleHash21(cell + 1.0f.xx),
        blend.x);
    return lerp(lower, upper, blend.y);
}

float2 HikariParticleRotate2D(float2 value, float angle)
{
    const float sine = sin(angle);
    const float cosine = cos(angle);
    return float2(
        value.x * cosine + value.y * sine,
        value.y * cosine - value.x * sine);
}

float HikariParticleTaperedStrokeDistance(
    float2 position,
    float angle,
    float start,
    float lengthValue,
    float rootHalfWidth,
    float tipHalfWidth)
{
    const float2 local = HikariParticleRotate2D(position, angle);
    const float progress = saturate(
        (local.x - start) / max(lengthValue, 1.0e-4f));
    const float halfWidth = lerp(rootHalfWidth, tipHalfWidth, progress);
    return max(
        abs(local.y) - halfWidth,
        max(start - local.x, local.x - (start + lengthValue)));
}

float HikariParticleStyleDistance(
    float2 position,
    uint style,
    float randomValue,
    float normalizedAge)
{
    if (style == HIKARI_PARTICLE_STYLE_COMBAT_CUT)
    {
        const float curve =
            sin((position.x + 1.0f) * 2.4f + randomValue * 3.1f) *
            0.11f * (1.0f - saturate(abs(position.x)));
        const float halfWidth = 0.055f +
            0.245f * pow(saturate(1.0f - abs(position.x)), 0.72f);
        return max(
            abs(position.y - curve) - halfWidth,
            abs(position.x) - 0.98f);
    }
    if (style == HIKARI_PARTICLE_STYLE_COMBAT_IMPACT)
    {
        const float radius = length(position);
        const float angle = atan2(position.y, position.x);
        const float ray = pow(
            abs(cos(angle * 4.0f + randomValue * 1.7f)),
            9.0f);
        const float silhouette = 0.37f + 0.34f * ray;
        return radius - silhouette;
    }
    if (style == HIKARI_PARTICLE_STYLE_COMBAT_GROUND)
    {
        const float radius = length(position);
        const float angle = atan2(position.y, position.x);
        const float ringRadius = 0.61f +
            sin(angle * 6.0f + randomValue * 6.28318531f) * 0.018f;
        const float ring = abs(radius - ringRadius) - 0.065f;
        const float phase = randomValue * 6.28318531f;
        const float crackA = HikariParticleTaperedStrokeDistance(
            position,
            phase,
            0.14f,
            0.72f,
            0.048f,
            0.006f);
        const float crackB = HikariParticleTaperedStrokeDistance(
            position,
            phase + 2.19f,
            0.19f,
            0.58f,
            0.038f,
            0.005f);
        const float crackC = HikariParticleTaperedStrokeDistance(
            position,
            phase + 4.34f,
            0.22f,
            0.48f,
            0.032f,
            0.004f);
        return min(ring, min(crackA, min(crackB, crackC)));
    }
    if (style == HIKARI_PARTICLE_STYLE_COMIC_SMOKE)
    {
        const float swell = lerp(0.88f, 1.08f, normalizedAge);
        const float d0 = length(
            (position - float2(-0.28f, 0.04f)) /
            float2(0.58f, 0.54f)) - swell;
        const float d1 = length(
            (position - float2(0.30f, 0.08f)) /
            float2(0.55f, 0.50f)) - swell;
        const float d2 = length(
            (position - float2(0.02f, -0.27f)) /
            float2(0.70f, 0.48f)) - swell;
        const float d3 = length(
            (position - float2(0.0f, 0.30f)) /
            float2(0.48f, 0.43f)) - swell;
        return min(min(d0, d1), min(d2, d3));
    }
    if (style == HIKARI_PARTICLE_STYLE_PROJECTILE_NEEDLE)
    {
        const float taper = 0.025f +
            0.14f * pow(saturate(1.0f - abs(position.x)), 0.65f);
        return max(
            abs(position.y) - taper,
            abs(position.x) - 0.99f);
    }
    return -1.0f;
}

void HikariEvaluateStylizedParticle(
    ParticleVertex input,
    out float coverage,
    out float coreCoverage,
    out float breakupKeep)
{
    const float2 position = input.localUv * 2.0f - 1.0f;
    const float distance = HikariParticleStyleDistance(
        position,
        gShaderStyle,
        input.randomValue,
        input.normalizedAge);
    const float edgeHardness = saturate(gDepthAndStyleParams.w);
    const float antiAlias = max(fwidth(distance), 6.0e-4f) *
        lerp(2.85f, 1.35f, edgeHardness);
    const float outlineWidth = saturate(gDepthAndStyleParams.z);
    const float outerCoverage = 1.0f - smoothstep(
        -antiAlias,
        antiAlias,
        distance - outlineWidth);
    coreCoverage = 1.0f - smoothstep(
        -antiAlias,
        antiAlias,
        distance + outlineWidth * 0.18f);

    const float breakupScale = max(gShaderParams.y, 0.1f);
    const float breakupStrength = saturate(gShaderParams.z);
    const float2 noisePosition = float2(
        position.x * 0.80f + position.y * 0.60f,
        position.y * 0.80f - position.x * 0.60f) * breakupScale +
        float2(input.randomValue * 17.0f, input.randomValue * 29.0f);
    const float breakupNoise =
        HikariParticleValueNoise(noisePosition) * 0.68f +
        HikariParticleValueNoise(
            noisePosition * 2.07f + float2(11.3f, 7.1f)) * 0.32f;
    const float breakupFilterWidth = max(fwidth(breakupNoise) * 1.35f, 0.025f);
    const float organicBreakup = smoothstep(
        0.34f - breakupFilterWidth,
        0.56f + breakupFilterWidth,
        breakupNoise);
    const float breakupEdgeBand = smoothstep(
        -max(0.18f, outlineWidth * 2.5f),
        antiAlias * 2.0f,
        distance);
    const float breakupAmount = saturate(
        breakupStrength *
        smoothstep(0.52f, 1.0f, input.normalizedAge) *
        breakupEdgeBand);
    breakupKeep = lerp(1.0f, organicBreakup, breakupAmount);
    coverage = outerCoverage * breakupKeep;
    coreCoverage *= breakupKeep;
}

float HikariResolveParticleDepthFade(ParticleVertex input)
{
    if (gDepthMode != HIKARI_PARTICLE_DEPTH_SOFT_INTERSECTION ||
        gSceneDepthAvailable == 0u)
    {
        return 1.0f;
    }
    const float2 uv = HikariScreenUvFromSvPosition(
        input.position,
        gScreenParams.zw);
    if (!HikariIsScreenUvInsideTexelGuard(uv, gScreenParams.zw))
    {
        return 1.0f;
    }
    const float sceneDepth = gParticleSceneDepth.SampleLevel(
        gParticleDepthSampler,
        uv,
        0.0f);
    float3 sceneWorldPosition = 0.0f.xxx;
    if (!HikariReconstructWorldPosition(
            uv,
            sceneDepth,
            gInverseViewProjection,
            sceneWorldPosition))
    {
        return 1.0f;
    }
    const float separation = distance(
        sceneWorldPosition,
        input.worldPosition);
    return saturate(
        separation / max(gDepthAndStyleParams.x, 1.0e-3f));
}

float4 ParticlePS(ParticleVertex input) : SV_TARGET
{
    HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(gMaterialDataIndex);
    float4 materialColor = materialData.baseColor;
    float opacity = 1.0f;
    float3 emissive = materialData.emissiveFactor.rgb *
        materialData.emissiveFactor.w;
    float alphaClipThreshold = 0.001f;
    const uint flipbookGrid = asuint(gRenderShapeParamsB.z);
    const uint columns = max(flipbookGrid & 0xffu, 1u);
    const uint rows = max((flipbookGrid >> 8u) & 0xffu, 1u);
    bool hasAuthoredStylizedMask = false;
    float authoredMaskCenter = 1.0f;
    float authoredMaskOuter = 1.0f;
    const bool hasMaterialExpression =
        materialData.materialProgramIndex !=
        HIKARI_STANDARD_PBR_MATERIAL_PROGRAM;
    if (hasMaterialExpression)
    {
        HikariMaterialExpressionContext context;
        context.uv0 = input.uv;
        context.uv1 = input.uv;
        context.time = gTimeParams;
        context.worldPosition = input.worldPosition;
        context.worldNormal = normalize(input.worldNormal);
        context.worldTangent = float4(
            normalize(input.worldTangent), 1.0f);
        context.viewDirection = normalize(
            gCameraPosition.xyz - input.worldPosition);
        context.cameraPosition = gCameraPosition.xyz;
        context.screenPosition = float4(
            input.position.xy * gScreenParams.zw,
            input.position.z,
            input.position.w);
        const HikariMaterialExpressionResult expression =
            HikariEvaluateMaterialExpression(
                materialData,
                context,
                gParticleSampler);
        if ((expression.outputMask &
             HIKARI_EXPRESSION_OUTPUT_BASE_COLOR) != 0u)
        {
            materialColor = expression.baseColor;
        }
        if ((expression.outputMask &
             HIKARI_EXPRESSION_OUTPUT_OPACITY) != 0u)
        {
            opacity = expression.opacity;
        }
        if ((expression.outputMask &
             HIKARI_EXPRESSION_OUTPUT_EMISSIVE) != 0u)
        {
            emissive = expression.emissive;
        }
        if ((expression.outputMask &
             HIKARI_EXPRESSION_OUTPUT_ALPHA_CLIP) != 0u)
        {
            alphaClipThreshold = max(
                expression.alphaClipThreshold,
                0.0f);
        }
    }
    else
    {
        const float2 baseColorUv = HikariResolveMaterialUv(
            materialData,
            HIKARI_MATERIAL_UV_BASE_COLOR,
            input.uv,
            input.uv);
        if (materialData.hasBaseColorTexture != 0u)
        {
            materialColor *= SampleParticleAtlasTexture(
                materialData.baseColorTextureDescriptorIndex,
                columns > 1u || rows > 1u
                    ? input.localUv
                    : baseColorUv,
                input.flipbookFrame,
                columns,
                rows,
                1.0f.xxxx);
            if (gShaderStyle != HIKARI_PARTICLE_STYLE_DEFAULT &&
                columns == 1u && rows == 1u)
            {
                // Production combat masks own the silhouette. Dilate their
                // alpha in screen-pixel units to create a stable dark-blue
                // outline instead of intersecting the authored arc with the
                // old analytic lens/card shape.
                hasAuthoredStylizedMask = true;
                authoredMaskCenter = saturate(materialColor.a);
                const float outlinePixels = clamp(
                    0.60f + saturate(gDepthAndStyleParams.z) * 18.0f,
                    0.65f,
                    1.80f);
                const float2 du = ddx(baseColorUv) * outlinePixels;
                const float2 dv = ddy(baseColorUv) * outlinePixels;
                const float2 diagonalA = (du + dv) * 0.70710678f;
                const float2 diagonalB = (du - dv) * 0.70710678f;
                const float baseAlpha = saturate(materialData.baseColor.a);
                authoredMaskOuter = authoredMaskCenter;
                authoredMaskOuter = max(authoredMaskOuter,
                    HikariSampleMaterialTexture(
                        materialData.baseColorTextureDescriptorIndex,
                        gParticleSampler,
                        baseColorUv + du,
                        1.0f.xxxx).a * baseAlpha);
                authoredMaskOuter = max(authoredMaskOuter,
                    HikariSampleMaterialTexture(
                        materialData.baseColorTextureDescriptorIndex,
                        gParticleSampler,
                        baseColorUv - du,
                        1.0f.xxxx).a * baseAlpha);
                authoredMaskOuter = max(authoredMaskOuter,
                    HikariSampleMaterialTexture(
                        materialData.baseColorTextureDescriptorIndex,
                        gParticleSampler,
                        baseColorUv + dv,
                        1.0f.xxxx).a * baseAlpha);
                authoredMaskOuter = max(authoredMaskOuter,
                    HikariSampleMaterialTexture(
                        materialData.baseColorTextureDescriptorIndex,
                        gParticleSampler,
                        baseColorUv - dv,
                        1.0f.xxxx).a * baseAlpha);
                authoredMaskOuter = max(authoredMaskOuter,
                    HikariSampleMaterialTexture(
                        materialData.baseColorTextureDescriptorIndex,
                        gParticleSampler,
                        baseColorUv + diagonalA,
                        1.0f.xxxx).a * baseAlpha);
                authoredMaskOuter = max(authoredMaskOuter,
                    HikariSampleMaterialTexture(
                        materialData.baseColorTextureDescriptorIndex,
                        gParticleSampler,
                        baseColorUv - diagonalA,
                        1.0f.xxxx).a * baseAlpha);
                authoredMaskOuter = max(authoredMaskOuter,
                    HikariSampleMaterialTexture(
                        materialData.baseColorTextureDescriptorIndex,
                        gParticleSampler,
                        baseColorUv + diagonalB,
                        1.0f.xxxx).a * baseAlpha);
                authoredMaskOuter = max(authoredMaskOuter,
                    HikariSampleMaterialTexture(
                        materialData.baseColorTextureDescriptorIndex,
                        gParticleSampler,
                        baseColorUv - diagonalB,
                        1.0f.xxxx).a * baseAlpha);
            }
        }
        if (materialData.hasEmissiveTexture != 0u)
        {
            const float2 emissiveUv = HikariResolveMaterialUv(
                materialData,
                HIKARI_MATERIAL_UV_EMISSIVE,
                input.uv,
                input.uv);
            emissive *= HikariSampleMaterialTexture(
                materialData.emissiveTextureDescriptorIndex,
                gParticleSampler,
                emissiveUv,
                1.0f.xxxx).rgb;
        }
        if ((materialData.materialFlags &
             HIKARI_PARTICLE_MATERIAL_ALPHA_MASK) != 0u)
        {
            alphaClipThreshold = max(materialData.pbrParams.w, 0.0f);
        }
    }
    const float rawParticleAlpha = max(input.color.a, 0.0f);
    const float shaderOverdrive = saturate(
        (rawParticleAlpha - 1.0f) * 0.25f);
    float alpha = input.color.a * opacity;
    if (!hasMaterialExpression)
    {
        alpha *= materialColor.a;
    }
    if (gShaderStyle != HIKARI_PARTICLE_STYLE_DEFAULT)
    {
        // Profiled combat effects reserve HDR alpha above one as a transient
        // shader-overdrive carrier. The excess is transported through the
        // existing VFX color multiplier, so neither the particle layout nor
        // the cooked format needs another runtime field. Keep the default
        // material path untouched and bound only stylized visible opacity.
        alpha = min(rawParticleAlpha, 1.0f) * opacity;
        if (!hasMaterialExpression && !hasAuthoredStylizedMask)
        {
            alpha *= materialColor.a;
        }
        float styleCoverage = 0.0f;
        float coreCoverage = 0.0f;
        float breakupKeep = 1.0f;
        if (hasAuthoredStylizedMask)
        {
            styleCoverage = smoothstep(
                0.015f,
                0.92f,
                authoredMaskOuter);
            coreCoverage = smoothstep(
                0.20f,
                0.78f,
                authoredMaskCenter);
        }
        else
        {
            HikariEvaluateStylizedParticle(
                input,
                styleCoverage,
                coreCoverage,
                breakupKeep);
        }
        const float coreRatio = saturate(
            coreCoverage / max(styleCoverage, 1.0e-4f));
        const float4 styleColor = lerp(
            gShaderOutlineColor,
            gShaderCoreColor,
            coreRatio);
        alpha *= styleCoverage * styleColor.a;
        alpha *= HikariResolveParticleDepthFade(input);
        clip(styleCoverage - 1.0e-4f);
        clip(alpha - max(alphaClipThreshold, 0.001f));
        const float intensity = lerp(
            0.82f,
            1.0f,
            saturate(max(input.color.r, max(input.color.g, input.color.b))));
        const float3 stylizedColor = styleColor.rgb * intensity;
        const float3 stylizedEmissive = gShaderCoreColor.rgb *
            coreCoverage * max(gShaderParams.x, 0.0f);
        const float phase = frac(
            dot(input.localUv, float2(2.85f, -1.95f)) +
            input.normalizedAge * 1.70f + input.randomValue * 3.10f);
        const float movingCut = 1.0f - smoothstep(
            0.065f,
            0.205f,
            abs(phase - 0.50f));
        const float whiteCore = pow(coreRatio, 0.42f);
        const float3 overdriveColor = lerp(
            float3(0.04f, 0.58f, 1.22f),
            1.0f.xxx,
            whiteCore);
        const float interiorEnergy = coreCoverage *
            smoothstep(0.16f, 0.82f, coreRatio);
        const float overdriveEnergy = shaderOverdrive *
            (0.36f + 0.44f * movingCut) *
            interiorEnergy;
        const float3 straightStylized = stylizedColor +
            stylizedEmissive * (1.0f +
                0.85f * shaderOverdrive * interiorEnergy) +
            overdriveColor * overdriveEnergy;
        return float4(straightStylized * alpha, alpha);
    }
    alpha *= HikariResolveParticleDepthFade(input);
    clip(alpha - alphaClipThreshold);
    const float3 straightColor =
        input.color.rgb * materialColor.rgb + emissive;
    return float4(straightColor * alpha, alpha);
}
