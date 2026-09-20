// Particle geometry and AS/MS dispatch; material evaluation stays in the draw shader.
#include "HIKARI_ParticlePrimitive.hlsli"

// 形状UV → 材質UV の契約。既定(Parameter モード)は恒等変換で、既存資産の
// 見た目を変えない。WorldLength モードと軸交換・回転・タイルだけを適用する。
float2 ParticleApplyUvContract(float2 parameterUv, float2 worldExtent)
{
    float2 uv = parameterUv;
    float2 extent = worldExtent;
    if ((gRenderUvMode & 2u) != 0u)
    {
        uv = uv.yx;
        extent = extent.yx;
    }
    if ((gRenderUvMode & 1u) != 0u)
        uv.x = uv.x * extent.x / max(gRenderUvParams.x, 1.0e-4f);
    uv *= gRenderUvParams.yz;
    if (abs(gRenderUvParams.w) > 1.0e-6f)
    {
        const float sine = sin(gRenderUvParams.w);
        const float cosine = cos(gRenderUvParams.w);
        const float2 centered = uv - 0.5f.xx;
        uv = float2(centered.x * cosine - centered.y * sine,
            centered.x * sine + centered.y * cosine) + 0.5f.xx;
    }
    return uv;
}

// 形状パラメータ s (0..1) に沿った幅の倍率。既定の (1,1) は形状を変えない。
float ParticleShapeTaper(float s)
{
    return lerp(gDepthParams.z, gDepthParams.w, saturate(s));
}
float ParticleShapeBend()
{
    return gShapeBend;
}
float ParticleShapeTwist()
{
    return gShapeTwist;
}
// per-draw の形状パラメータを読み込む。各ジオメトリ入口で最初に呼ぶ。
void ParticleLoadShapeState()
{
    gShapeState = gShapeStates[gParticleReserved];
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
    // パラメータUVの U/V が対応する物理長。WorldLength 契約の基準。
    float2 uvWorldExtent = 1.0f.xx;
    float3 worldPosition = particle.positionAge.xyz;
    if (gRenderShapeMode == 9u)
    {
        const float3 corners[4] = {particle.velocityLifetime.xyz, particle.physicsPositionSeed.xyz,
            particle.birthRotation.xyz, particle.birthScale.xyz};
        const float2 uvs[4] = {float2(0,0),float2(0,1),float2(1,1),float2(1,0)};
        const uint grid=ParticleSpatialGrid(gRenderShapeMode);
        uv = grid>0u ? ParticleSpatialGridUv(vertexIndex,grid) : uvs[vertexIndex];
        worldPosition=lerp(lerp(corners[0],corners[3],uv.x),lerp(corners[1],corners[2],uv.x),uv.y);
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
        const uint grid=ParticleSpatialGrid(gRenderShapeMode);
        const float2 corner = grid>0u ? ParticleSpatialGridUv(vertexIndex,grid)-.5f : corners[vertexIndex];
        uv = float2(corner.x+.5f,.5f-corner.y);
        uvWorldExtent = float2(abs(particle.scaleRotation.x), abs(particle.scaleRotation.y));
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
        // 長さ軸を持つプリミティブは、断面の幅を長さ方向へテーパーできる。
        if (gRenderShapeMode == 3u || gRenderShapeMode == 5u ||
            gRenderShapeMode == 6u || gRenderShapeMode == 8u)
        {
            // 変形軸は face UV ではなく形状の Y。法線は変形の逆転置、
            // 接線は同じヤコビアンで運び、Fresnel/照明を輪郭と一致させる。
            const float axisT = saturate(localPosition.y + 0.5f);
            const float widthScale = max(ParticleShapeTaper(axisT), 1.0e-5f);
            const float widthSlope = gDepthParams.w - gDepthParams.z;
            // 円形断面のねじりは曲面自体を変えない。頂点列を回すと低分割で
            // 断面が潰れるため、等価な周方向 UV の移動として評価する。
            const bool circular = gRenderShapeMode == 5u || gRenderShapeMode == 6u;
            const float geometryTwist = circular ? 0.0f : ParticleShapeTwist();
            if (circular) uv.x -= ParticleShapeTwist() * axisT * (1.0f / 6.28318530718f);
            const float angle = geometryTwist * axisT;
            const float2x2 turn = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));
            const float2 turned = mul(turn, localPosition.xz);
            const float2 crossSection = turned * widthScale;
            const float2 slope = turned * widthSlope +
                geometryTwist * float2(-crossSection.y, crossSection.x) +
                float2(2.0f * ParticleShapeBend() * axisT, 0.0f);
            const float2 normalXZ = mul(turn, localNormal.xz) / widthScale;
            localNormal = float3(normalXZ.x, localNormal.y - dot(slope, normalXZ), normalXZ.y);
            const float2 tangentXZ = mul(turn, localTangent.xz) * widthScale + slope * localTangent.y;
            localTangent = float3(tangentXZ.x, localTangent.y, tangentXZ.y);
            localPosition.xz = crossSection + float2(ParticleShapeBend() * axisT * axisT, 0);
        }
        const float3 scale = max(abs(particle.scaleRotation.xyw), 1.0e-5f.xxx);
        const float4 orientation = NormalizeQuaternion(particle.orientation);
        worldPosition += RotateByQuaternion(localPosition * scale, orientation);
        normal = normalize(RotateByQuaternion(localNormal / scale, orientation));
        right = normalize(RotateByQuaternion(localTangent * scale, orientation));
        // プリミティブの基準は回転後の形状軸。アンカーYは軸/緯度方向を取る。
        up = NormalizeOr(-cross(normal, right), up);
        uvWorldExtent = ParticlePrimitiveUvExtent(gRenderShapeMode, scale);
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
                    particle.scaleRotation.y * ParticleShapeTaper(s),
                0.0f);
            // ツイストは断面を長さ方向へ回し、ベンドは中心線を幅方向へ
            // 放物線状にずらす。どちらも 0 なら従来の帯のまま。
            const float twist = ParticleShapeTwist();
            const float3 twistedAxis = twist == 0.0f
                ? widthDirection
                : NormalizeOr(
                    widthDirection * cos(twist * s) +
                        normal * sin(twist * s),
                    widthDirection);
            const float bend = ParticleShapeBend();
            worldPosition += tangent * ((s - 0.5f) * ribbonLength) +
                twistedAxis * side * halfWidth +
                widthDirection * (bend * s * s * ribbonLength);
            right = tangent;
            // 帯の基準は「長さ方向 = X、幅方向 = Y」。
            up = widthDirection;
            uvWorldExtent = float2(ribbonLength,
                max(gRenderShapeParamsA.x * particle.scaleRotation.y, 0.0f));
        }
        else
        {
            const float angle = lerp(
                gRenderShapeParamsA.w,
                gRenderShapeParamsB.x,
                s);
            // リングは帯幅を弧方向に収窄できる。既定倍率 1 では従来どおり。
            const float meanRadius =
                0.5f * (gRenderShapeParamsA.y + gRenderShapeParamsA.z);
            const float halfBand =
                0.5f * (gRenderShapeParamsA.y - gRenderShapeParamsA.z) *
                ParticleShapeTaper(s);
            const float radius = side < 0.0f
                ? meanRadius - halfBand
                : meanRadius + halfBand;
            worldPosition += right * cos(angle) * radius *
                    particle.scaleRotation.x +
                up * sin(angle) * radius *
                    particle.scaleRotation.y;
            // 弧長は中径と角度幅で近似する。半径方向は内外径の幅。
            const float meanScale = 0.5f *
                (abs(particle.scaleRotation.x) + abs(particle.scaleRotation.y));
            uvWorldExtent = float2(
                abs(gRenderShapeParamsB.x - gRenderShapeParamsA.w) * meanRadius * meanScale,
                abs(gRenderShapeParamsA.y - gRenderShapeParamsA.z) * meanScale *
                    0.5f * (gDepthParams.z + gDepthParams.w));
        }
    }

    // アンカー: 形状中心ではなく指定点を変換原点にする。0 のときは恒等。
    if (gRenderShapeMode != 9u && dot(gRenderAnchor.xyz, gRenderAnchor.xyz) > 1.0e-12f)
    {
        const float3 anchorScale = gRenderShapeMode >= 3u
            ? max(abs(particle.scaleRotation.xyw), 1.0e-5f.xxx)
            : float3(particle.scaleRotation.x, particle.scaleRotation.y, 1.0f);
        worldPosition -= gRenderShapeMode >= 3u
            ? RotateByQuaternion(gRenderAnchor.xyz * anchorScale, NormalizeQuaternion(particle.orientation))
            : right * (gRenderAnchor.x * anchorScale.x) +
                up * (gRenderAnchor.y * anchorScale.y) +
                normal * (gRenderAnchor.z * anchorScale.z);
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
    // フリップブックはセル内UV、それ以外は契約適用後のUVを使う。材質へは
    // 契約適用後の materialUv を渡し、アトラスセル解決と独立させる。
    const float2 materialUv = ParticleApplyUvContract(uv, uvWorldExtent);
    output.uv = columns > 1u || rows > 1u
        ? (float2(frame % columns, frame / columns) + uv) /
            float2(columns, rows)
        : materialUv;
    output.localUv = uv;
    output.materialUv = materialUv;
    output.flipbookFrame = frame;
    ParticleSpatialVertex(worldPosition,normal,right);
    output.position = mul(gViewProjection, float4(worldPosition, 1.0f));
    if (gDepthMode == HIKARI_PARTICLE_DEPTH_CONTACT_SURFACE)
    {
        output.position.z = max(
            0.0f,
            output.position.z -
                max(gDepthParams.y, 0.0f) * output.position.w);
    }
    output.color = particle.color;
    if (gRenderShapeMode == 9u) {
        const uint grid=ParticleSpatialGrid(gRenderShapeMode);
        const float t=grid>0u?ParticleSpatialGridUv(vertexIndex,grid).x:(vertexIndex<2u?0:1);
        output.color.rgb = lerp(particle.orientation.xyz,particle.color.rgb,t);
        output.color.a *= lerp(particle.physicsPositionSeed.w,particle.birthScale.w,t);
    }
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
    float3 position = particle.positionAge.xyz;
    const float3 scale = abs(particle.scaleRotation.xyw);
    const float taper = max(abs(gDepthParams.z), abs(gDepthParams.w));
    float3 extent = 0.5f.xxx;
    if (gRenderShapeMode == 5u || gRenderShapeMode == 6u)
        extent.xz = max(asfloat(gShapeParam0), max(asfloat(gShapeParam1), asfloat(gShapeParam2))).xx;
    if (gRenderShapeMode == 10u)
        extent = float3(gShapeParam3 + gShapeParam4, gShapeParam4, gShapeParam3 + gShapeParam4);
    if (gRenderShapeMode == 3u || gRenderShapeMode == 5u || gRenderShapeMode == 6u || gRenderShapeMode == 8u)
    {
        // 回転断面は x/z の長い方で包む。
        extent.xz = (length(extent.xz) * taper).xx;
        extent.x += abs(ParticleShapeBend());
    }
    float radius = length(extent * scale);
    if (gRenderShapeMode == 1u)
        radius = 0.5f * length(float2(scale.x, gRenderShapeParamsA.x * scale.y * taper)) + abs(ParticleShapeBend()) * scale.x;
    if (gRenderShapeMode == 2u)
        radius = (0.5f * (gRenderShapeParamsA.y + gRenderShapeParamsA.z) +
            0.5f * abs(gRenderShapeParamsA.y - gRenderShapeParamsA.z) * taper) * max(scale.x, scale.y);
    radius += length(gRenderAnchor.xyz * (gRenderShapeMode >= 3u ? scale : float3(scale.xy, 1)));
    if (gRenderShapeMode == 9u)
    {
        radius = max(length(particle.velocityLifetime.xyz - position), length(particle.physicsPositionSeed.xyz - position));
        radius = max(radius, max(length(particle.birthRotation.xyz - position), length(particle.birthScale.xyz - position)));
    }
    ParticleSpatialBounds(position,radius);
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
    ParticleLoadShapeState();
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
    const uint groups = gRenderShapeMode == 0u && gShapeState.spatialCount==0u ? (gParticlePayload.count+7u)/8u : gParticlePayload.count;
    DispatchMesh(groups,1u,1u,gParticlePayload);
}

[outputtopology("triangle")]
[numthreads(32,1,1)]
void ParticleSpriteMS(uint3 groupId : SV_GroupID, uint lane : SV_GroupThreadID,
    in payload ParticlePayload payload,
    out vertices ParticleVertex verticesOut[32], out indices uint3 trianglesOut[16])
{
    ParticleLoadShapeState();
    const uint count = min(8u, payload.count - groupId.x*8u);
    SetMeshOutputCounts(count*4u,count*2u);
    const uint slot = lane/4u;
    if (slot < count)
    {
        const Particle particle = gDrawParticles[payload.indices[groupId.x*8u+slot]];
        float3 right, up, normal;
        ResolveBillboardBasis(particle,right,up,normal);
    ParticleSpatialBasis(particle,right,up,normal);
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
    // D3D12 のメッシュシェーダは 1 グループ 256 頂点 / 256 図元が上限。
    // 球は 17×9 の格子（153 頂点 / 256 図元）まで使える。
    out vertices ParticleVertex verticesOut[256],
    out indices uint3 trianglesOut[256])
{
    ParticleLoadShapeState();
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
        ParticleSpatialBasis(gMeshParticle,gMeshRight,gMeshUp,gMeshNormal);
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
        ParticleSpatialBasis(gMeshParticle,gMeshRight,gMeshUp,gMeshNormal);
    }
    for (uint primitiveIndex=groupThreadId;primitiveIndex<primitiveCount;primitiveIndex+=64u)
        trianglesOut[primitiveIndex] =
            ParticlePrimitiveTriangle(gRenderShapeMode, primitiveIndex, segmentCount);
}

ParticleVertex ParticleVS(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    ParticleLoadShapeState();
    const Particle particle = gDrawParticles[gAliveParticleIndices[gAliveIndexBase+instanceIndex]];
    float3 right,up,normal;
    ResolveBillboardBasis(particle,right,up,normal);
    ParticleSpatialBasis(particle,right,up,normal);
    // 頂点シェーダ側はメッシュシェーダと同じ分割式でなければ添字が合わない。
    const uint segmentCount = gRenderShapeMode == 0u
        ? 1u
        : clamp(gSegmentCount, gRenderShapeMode == 1u ? 2u : 3u,
            HIKARI_PARTICLE_MAX_SEGMENTS);
    const uint3 indices =
        ParticlePrimitiveTriangle(gRenderShapeMode, vertexIndex/3u, segmentCount);
    return BuildParticleVertex(particle,indices[vertexIndex%3u],segmentCount,right,up,normal);
}
