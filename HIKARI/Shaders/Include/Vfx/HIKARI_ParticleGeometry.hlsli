// Particle geometry and AS/MS dispatch; material evaluation stays in the draw shader.
#include "HIKARI_ParticlePrimitive.hlsli"
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
    if (gRenderShapeMode == 9u)
    {
        const float3 corners[4] = {particle.velocityLifetime.xyz, particle.physicsPositionSeed.xyz,
            particle.birthRotation.xyz, particle.birthScale.xyz};
        const float2 uvs[4] = {float2(0,0),float2(0,1),float2(1,1),float2(1,0)};
        worldPosition = corners[vertexIndex]; uv = uvs[vertexIndex];
        uv.x = lerp(particle.scaleRotation.z, particle.birthRotation.w, uv.x);
        right = NormalizeOr(corners[3]-corners[0], baseRight);
        normal = NormalizeOr(cross(right, corners[1]-corners[0]), baseNormal);
    }
    else if (gRenderShapeMode == 0u)
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
    else if (gRenderShapeMode >= 3u)
    {
        float3 localPosition;
        float3 localNormal;
        float3 localTangent;
        ParticlePrimitiveVertex(gRenderShapeMode, vertexIndex, segmentCount,
            localPosition, localNormal, localTangent, uv);
        const float3 scale = max(abs(particle.scaleRotation.xyw), 1.0e-5f.xxx);
        const float4 orientation = NormalizeQuaternion(particle.orientation);
        worldPosition += RotateByQuaternion(localPosition * scale, orientation);
        normal = normalize(RotateByQuaternion(localNormal / scale, orientation));
        right = normalize(RotateByQuaternion(localTangent * scale, orientation));
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
                max(gDepthParams.y, 0.0f) * output.position.w);
    }
    output.color = particle.color;
    if (gRenderShapeMode == 9u)
        output.color.a *= vertexIndex < 2u ? particle.physicsPositionSeed.w : particle.birthScale.w;
    output.worldPosition = worldPosition;
    output.worldNormal = normal;
    output.worldTangent = right;
    output.normalizedAge = saturate(
        max(particle.positionAge.w, 0.0f) /
        max(particle.velocityLifetime.w, 1.0e-5f));
    output.randomValue = frac(
        float(asuint(particle.physicsPositionSeed.w)) *
        0.00000011920928955078125f);
    output.ageAndSpeed = float2(max(particle.positionAge.w, 0.0f), length(particle.velocityLifetime.xyz));
    if (gRenderShapeMode == 9u) { output.randomValue = 0; output.ageAndSpeed.y = 0; }
    return output;
}

// AS owns view fine culling; CS only compacts live simulation slots.
struct ParticlePayload
{
    uint count;
    uint indices[32];
};
groupshared ParticlePayload gParticlePayload;
groupshared uint gParticleVisibilityMask;
StructuredBuffer<uint> gParticleAliveCounts : register(t4);

bool ParticleInFrustum(Particle particle)
{
    const float3 position = particle.positionAge.xyz;
    float radius = 0.5f * length(particle.scaleRotation.xyw);
    if (gRenderShapeMode == 1u) radius *= max(1.0f, gRenderShapeParamsA.x);
    if (gRenderShapeMode == 2u) radius *= max(1.0f, 2.0f * gRenderShapeParamsA.y);
    if (!all(isfinite(position)) || !isfinite(radius)) return true;
    const float4 planes[6] = {
        gViewProjection[3]+gViewProjection[0], gViewProjection[3]-gViewProjection[0],
        gViewProjection[3]+gViewProjection[1], gViewProjection[3]-gViewProjection[1],
        gViewProjection[2], gViewProjection[3]-gViewProjection[2]
    };
    [unroll] for (uint i=0u;i<6u;++i)
        if (dot(planes[i],float4(position,1)) < -radius*length(planes[i].xyz)) return false;
    return true;
}

[numthreads(32,1,1)]
void ParticleAS(uint3 groupId : SV_GroupID, uint lane : SV_GroupThreadID)
{
    if (lane == 0u) gParticleVisibilityMask = 0u;
    GroupMemoryBarrierWithGroupSync();
    const uint aliveIndex = groupId.x * 32u + lane;
    uint index = 0u;
    bool visible = false;
    if (aliveIndex < gParticleAliveCounts[gParticleReserved])
    {
        index = gAliveParticleIndices[gAliveIndexBase + aliveIndex];
        visible = ParticleInFrustum(gDrawParticles[index]);
    }
    if (visible) InterlockedOr(gParticleVisibilityMask, 1u << lane);
    GroupMemoryBarrierWithGroupSync();
    if (visible)
        gParticlePayload.indices[countbits(gParticleVisibilityMask & ((1u << lane)-1u))] = index;
    if (lane == 0u) gParticlePayload.count = countbits(gParticleVisibilityMask);
    GroupMemoryBarrierWithGroupSync();
    const uint groups = gRenderShapeMode == 0u ? (gParticlePayload.count+7u)/8u : gParticlePayload.count;
    DispatchMesh(groups,1u,1u,gParticlePayload);
}

[outputtopology("triangle")]
[numthreads(32,1,1)]
void ParticleSpriteMS(uint3 groupId : SV_GroupID, uint lane : SV_GroupThreadID,
    in payload ParticlePayload payload,
    out vertices ParticleVertex verticesOut[32], out indices uint3 trianglesOut[16])
{
    const uint count = min(8u, payload.count - groupId.x*8u);
    SetMeshOutputCounts(count*4u,count*2u);
    const uint slot = lane/4u;
    if (slot < count)
    {
        const Particle particle = gDrawParticles[payload.indices[groupId.x*8u+slot]];
        float3 right, up, normal;
        ResolveBillboardBasis(particle,right,up,normal);
        verticesOut[lane] = BuildParticleVertex(particle,lane%4u,1u,right,up,normal);
    }
    if (lane < count*2u)
    {
        const uint base = (lane/2u)*4u;
        trianglesOut[lane] = (lane&1u)==0u ? uint3(base,base+1u,base+2u) : uint3(base,base+2u,base+3u);
    }
}

groupshared Particle gMeshParticle;
groupshared float3 gMeshRight;
groupshared float3 gMeshUp;
groupshared float3 gMeshNormal;

[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void ParticleShapeMS(
    uint3 groupId : SV_GroupID,
    uint groupThreadId : SV_GroupThreadID,
    in payload ParticlePayload payload,
    out vertices ParticleVertex verticesOut[130],
    out indices uint3 trianglesOut[128])
{
    if (groupThreadId == 0u)
    {
        const uint particleIndex =
            payload.indices[groupId.x];
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
    const uint2 counts = ParticlePrimitiveCounts(gRenderShapeMode, segmentCount);
    const uint vertexCount = counts.x;
    const uint primitiveCount = counts.y;
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
    for (uint primitiveIndex=groupThreadId;primitiveIndex<primitiveCount;primitiveIndex+=64u)
        trianglesOut[primitiveIndex] = ParticlePrimitiveTriangle(gRenderShapeMode, primitiveIndex);
}

ParticleVertex ParticleVS(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    const Particle particle = gDrawParticles[gAliveParticleIndices[gAliveIndexBase+instanceIndex]];
    float3 right,up,normal;
    ResolveBillboardBasis(particle,right,up,normal);
    const uint3 indices = ParticlePrimitiveTriangle(gRenderShapeMode, vertexIndex/3u);
    return BuildParticleVertex(particle,indices[vertexIndex%3u],max(gSegmentCount,1u),right,up,normal);
}
