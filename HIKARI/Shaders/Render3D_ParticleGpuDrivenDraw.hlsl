#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"
#include "Include/Material/HIKARI_MaterialExpression.hlsli"

static const uint HIKARI_PARTICLE_ROTATED_BILLBOARD = 0u;
static const uint HIKARI_PARTICLE_FIXED_Y_AXIS = 1u;
static const uint HIKARI_PARTICLE_DIRECTIONAL_BILLBOARD = 2u;
static const uint HIKARI_PARTICLE_FIXED = 3u;
static const uint HIKARI_PARTICLE_MAX_SEGMENTS = 64u;
static const uint HIKARI_PARTICLE_MATERIAL_ALPHA_MASK = 1u << 1u;

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
SamplerState gParticleSampler : register(s0);

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
    output.color = particle.color;
    output.worldPosition = worldPosition;
    output.worldNormal = normal;
    output.worldTangent = right;
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

float4 ParticlePS(ParticleVertex input) : SV_TARGET
{
    HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(gMaterialDataIndex);
    float4 materialColor = materialData.baseColor;
    float opacity = 1.0f;
    float3 emissive = materialData.emissiveFactor.rgb *
        materialData.emissiveFactor.w;
    float alphaClipThreshold = 0.001f;
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
            const uint flipbookGrid = asuint(gRenderShapeParamsB.z);
            const uint columns = max(flipbookGrid & 0xffu, 1u);
            const uint rows = max((flipbookGrid >> 8u) & 0xffu, 1u);
            materialColor *= SampleParticleAtlasTexture(
                materialData.baseColorTextureDescriptorIndex,
                columns > 1u || rows > 1u
                    ? input.localUv
                    : baseColorUv,
                input.flipbookFrame,
                columns,
                rows,
                1.0f.xxxx);
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
    float alpha = input.color.a * opacity;
    if (!hasMaterialExpression)
    {
        alpha *= materialColor.a;
    }
    clip(alpha - alphaClipThreshold);
    const float3 straightColor =
        input.color.rgb * materialColor.rgb + emissive;
    return float4(straightColor * alpha, alpha);
}
