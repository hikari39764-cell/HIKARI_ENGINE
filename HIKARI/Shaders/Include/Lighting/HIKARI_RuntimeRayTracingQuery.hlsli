#ifndef HIKARI_RUNTIME_RAY_TRACING_QUERY_INCLUDED
#define HIKARI_RUNTIME_RAY_TRACING_QUERY_INCLUDED

#include "../Contracts/HIKARI_ShaderResourceBindings.hlsli"
#include "../Cluster/HIKARI_ClusterGpuData.hlsli"
#define HIKARI_MESH_MATERIAL_DATA_EXTERNAL_INDEX_CB 1
#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#ifndef HIKARI_MATERIAL_EXPLICIT_LOD
#define HIKARI_MATERIAL_EXPLICIT_LOD 0.0f
#endif
#include "../Material/HIKARI_MaterialBaseColorInputs.hlsli"
#include "../Material/HIKARI_MaterialExpressionSurface.hlsli"
#include "../Color/HIKARI_Luminance.hlsli"

struct HikariRayTracingHitRecord
{
    uint geometryDescriptor;
    uint metadataDescriptor;
    uint materialIndex;
    uint flags;
    uint firstPrimitive;
    uint firstCluster;
    uint clusterCount;
    uint primitiveCount;
    uint deformedVertexBase;  // 変形した頂点の pool 内の byte 位置（flags bit2 の時）
    uint deformedVertexCount;
    uint2 padding;
};
RaytracingAccelerationStructure gRuntimeRayScene : register(t0, space8);
StructuredBuffer<HikariRayTracingHitRecord> gRuntimeRayHits : register(t1, space8);
// 変形した面（骨格の蒙皮）の world 空間の頂点：[位置 float3 × N][法線 float3 × N][接線 float4 × N]。
ByteAddressBuffer gRuntimeRayDeformed : register(t2, space8);
static const uint HIKARI_RAY_HIT_DEFORMED = 4u;
// 透過する面（ガラスなど半透明）。光線を止めず、影の光線だけ透過率を掛けて先へ進む。
static const uint HIKARI_RAY_HIT_TRANSMISSIVE = 8u;

void HikariLoadDeformedRayVertex(HikariRayTracingHitRecord record, uint vertex, inout HikariClusterVertex value)
{
    const uint count = record.deformedVertexCount;
    if (vertex >= count)
        return;
    const uint base = record.deformedVertexBase;
    value.position.xyz = asfloat(gRuntimeRayDeformed.Load3(base + vertex * 12u));
    value.normal.xyz = asfloat(gRuntimeRayDeformed.Load3(base + count * 12u + vertex * 12u));
    value.tangent = asfloat(gRuntimeRayDeformed.Load4(base + count * 24u + vertex * 16u));
}
ByteAddressBuffer gRuntimeRayGeometry[HIKARI_SHADER_CLUSTER_GEOMETRY_SRV_COUNT] : register(t0, space1);

struct HikariRaySurface
{
    float3 position;
    float3 geometricNormal;
    float3 normal;
    float4 tangent;
    float2 uv0;
    float2 uv1;
    uint materialIndex;
    float distance;
    float4 baseColor;
    HikariMaterialPbrInputs pbr;
    float3 emissive;
    uint backFace;
    HikariMaterialTextureFootprint footprint;
};

// xy = 原点での直径 / 2*tan(開口全角/2)。world 単位で距離に沿って広がる。
// 交差面へ射影した円の主軸を UV 微分へ変換し、非一様 scale と掠射角も扱う。
HikariMaterialTextureFootprint HikariRayTriangleFootprint(
    float diameter, float3 direction, float3 edge1, float3 edge2,
    float4 uvEdge1, float4 uvEdge2)
{
    HikariMaterialTextureFootprint footprint = (HikariMaterialTextureFootprint)0;
    footprint.explicitGradients = true;
    const float3 crossEdges = cross(edge1, edge2);
    const float areaSquared = dot(crossEdges, crossEdges);
    if (diameter <= 0.0f || areaSquared <= 1.0e-20f || !isfinite(areaSquared)) return footprint;
    const float3 n = crossEdges * rsqrt(areaSquared);
    const float cosine = dot(n, direction);
    const float3 tangent = direction - n * cosine;
    const float tangentLengthSquared = dot(tangent, tangent);
    const float3 major = tangentLengthSquared > 1.0e-10f ? tangent * rsqrt(tangentLengthSquared)
        : normalize(cross(n, abs(n.z) < 0.9f ? float3(0,0,1) : float3(0,1,0)));
    const float3 positionDx = major * (diameter / max(abs(cosine), 0.001f));
    const float3 positionDy = cross(n, major) * diameter;
    const float3 barycentric1 = cross(edge2, crossEdges) / areaSquared;
    const float3 barycentric2 = cross(crossEdges, edge1) / areaSquared;
    footprint.uvDx = dot(barycentric1, positionDx) * uvEdge1 + dot(barycentric2, positionDx) * uvEdge2;
    footprint.uvDy = dot(barycentric1, positionDy) * uvEdge1 + dot(barycentric2, positionDy) * uvEdge2;
    if (!all(isfinite(footprint.uvDx)) || !all(isfinite(footprint.uvDy)))
    { footprint.uvDx = 0.0f.xxxx; footprint.uvDy = 0.0f.xxxx; }
    return footprint;
}

// Raster と同じ HCMESH decode / UV / material-expression API を使用する。
// BLAS 分割点は meshlet 境界と一致する必要がない。
bool HikariDecodeRayCandidate(inout RayQuery<RAY_FLAG_NONE> query,
    uint hitRecordCount, float2 rayCone, out HikariRaySurface surface, out uint flags)
{
    surface = (HikariRaySurface)0;
    flags = 0u;
    const uint instance = query.CandidateInstanceID();
    const uint geometryIndex = query.CandidateGeometryIndex();
    if (instance >= hitRecordCount || geometryIndex >= hitRecordCount-instance) return false;
    const HikariRayTracingHitRecord record = gRuntimeRayHits[instance+geometryIndex];
    const uint geometry = record.geometryDescriptor-HIKARI_SHADER_CLUSTER_GEOMETRY_SRV_BEGIN;
    const uint metadata = record.metadataDescriptor-HIKARI_SHADER_CLUSTER_GEOMETRY_SRV_BEGIN;
    if (record.materialIndex == 0xffffffffu || geometry >= HIKARI_SHADER_CLUSTER_GEOMETRY_SRV_COUNT ||
        metadata >= HIKARI_SHADER_CLUSTER_GEOMETRY_SRV_COUNT || query.CandidatePrimitiveIndex() >= record.primitiveCount)
        return false;
    const HikariClusterGeometryHeader header = HikariLoadClusterGeometryHeader(gRuntimeRayGeometry[NonUniformResourceIndex(metadata)]);
    if (!HikariIsValidClusterGeometryHeader(header) || record.firstCluster >= header.clusterCount ||
        record.clusterCount > header.clusterCount-record.firstCluster) return false;
    const uint primitive = record.firstPrimitive + query.CandidatePrimitiveIndex();
    if (primitive >= header.meshletPrimitiveCount) return false;
    uint lower = record.firstCluster, upper = lower + record.clusterCount;
    // firstPrimitive は全 LOD0 cluster で昇順。
    while (lower+1u < upper)
    {
        const uint middle = lower + (upper-lower)/2u;
        const HikariMeshCluster cluster = HikariLoadMeshCluster(gRuntimeRayGeometry[NonUniformResourceIndex(metadata)], header, middle);
        if (cluster.firstPrimitive <= primitive) lower = middle;
        else upper = middle;
    }
    const HikariMeshCluster cluster = HikariLoadMeshCluster(gRuntimeRayGeometry[NonUniformResourceIndex(metadata)], header, lower);
    if (primitive < cluster.firstPrimitive || primitive-cluster.firstPrimitive >= cluster.triangleCount) return false;
    const uint3 local = HikariLoadMeshletPrimitiveIndices(gRuntimeRayGeometry[NonUniformResourceIndex(geometry)], header, primitive);
    if (any(local >= cluster.vertexCount) || cluster.firstVertex >= header.vertexCount ||
        any(local >= header.vertexCount-cluster.firstVertex)) return false;
    const uint3 vertices = cluster.firstVertex + local;
    HikariClusterVertex a = HikariLoadClusterVertex(gRuntimeRayGeometry[NonUniformResourceIndex(geometry)], header, vertices.x);
    HikariClusterVertex b = HikariLoadClusterVertex(gRuntimeRayGeometry[NonUniformResourceIndex(geometry)], header, vertices.y);
    HikariClusterVertex c = HikariLoadClusterVertex(gRuntimeRayGeometry[NonUniformResourceIndex(geometry)], header, vertices.z);
    // 変形した面は位置・法線・接線を world 空間の変形結果で置き換える（UV は元のまま、instance の変換は単位）。
    if ((record.flags & HIKARI_RAY_HIT_DEFORMED) != 0u)
    {
        HikariLoadDeformedRayVertex(record, vertices.x, a);
        HikariLoadDeformedRayVertex(record, vertices.y, b);
        HikariLoadDeformedRayVertex(record, vertices.z, c);
    }
    const float2 bary = query.CandidateTriangleBarycentrics();
    const float3 weights = float3(1.0f-bary.x-bary.y, bary.x, bary.y);
    const float3x4 objectToWorld = query.CandidateObjectToWorld3x4();
    const float3x3 normalToWorld = transpose((float3x3)query.CandidateWorldToObject3x4());
    const float3 normalOS = a.normal.xyz*weights.x + b.normal.xyz*weights.y + c.normal.xyz*weights.z;
    const float3 faceOS = cross(b.position.xyz-a.position.xyz, c.position.xyz-a.position.xyz);
    float3 face = HikariNormalizeOrDefault(mul(normalToWorld, faceOS), float3(0,1,0));
    float3 normal = HikariNormalizeOrDefault(mul(normalToWorld, normalOS), face);
    if (dot(face, normal) < 0) face = -face;
    const float4 tangentOS = a.tangent*weights.x + b.tangent*weights.y + c.tangent*weights.z;
    surface.tangent = float4(mul((float3x3)objectToWorld, tangentOS.xyz),
        (tangentOS.w < 0 ? -1.0f : 1.0f) * (determinant((float3x3)objectToWorld) < 0 ? -1.0f : 1.0f));
    surface.position = mul(objectToWorld, float4(a.position.xyz*weights.x + b.position.xyz*weights.y + c.position.xyz*weights.z, 1));
    surface.distance = query.CandidateTriangleRayT();
    surface.geometricNormal = face;
    surface.normal = normal;
    surface.uv0 = a.uv01.xy*weights.x + b.uv01.xy*weights.y + c.uv01.xy*weights.z;
    surface.uv1 = a.uv01.zw*weights.x + b.uv01.zw*weights.y + c.uv01.zw*weights.z;
    surface.materialIndex = record.materialIndex;
    surface.footprint = HikariRayTriangleFootprint(max(0.0f, rayCone.x + rayCone.y * surface.distance),
        query.WorldRayDirection(), mul((float3x3)objectToWorld, b.position.xyz-a.position.xyz),
        mul((float3x3)objectToWorld, c.position.xyz-a.position.xyz), b.uv01-a.uv01, c.uv01-a.uv01);
    flags = record.flags;
    return all(isfinite(surface.position)) && all(isfinite(surface.normal));
}

static const uint HIKARI_RAY_SHADOW = 1u;
static const uint HIKARI_RAY_REPORT_BACKFACES = 2u;
// GI の連通性など、影を落とす設定とは独立した形状の遮断だけを調べる。
static const uint HIKARI_RAY_OCCLUSION = 4u;
// 最も近い三角形の位置・距離・向き・裏面だけを返し、材質を読まない（alpha test もせず切り抜きは不透明として当たる）。
// おおよその距離だけが要る ray に使う。材質を読む命令は呼び出しごとに展開されるので、大きな shader では
// これだけで重さが変わる（動的 GI の更新の相機 ray をこれにして約 0.35 ms 軽くなった）。
static const uint HIKARI_RAY_GEOMETRY_ONLY = 8u;

// 半透明の面が光を通す割合。被覆の外（1 - alpha）はそのまま、被覆の中は透過の分だけ底色で色づいて通る。
float HikariRayTransmittance(HikariMeshMaterialData material, HikariRaySurface surface, SamplerState materialSampler)
{
    const float4 base = HikariResolveMaterialBaseColor(material, surface.uv0, surface.uv1, materialSampler, true);
    const float3 through = (1.0f - base.a) + base.a * saturate(material.surfaceLayers.z) * base.rgb;
    return saturate(HikariLuminance(through));
}

// transmittance は影の光線が通り抜けた半透明の面の透過率の積（不透明に当たれば戻り値が true）。
bool HikariTraceRuntimeSceneCore(RayDesc ray, uint hitRecordCount, uint materialCount,
    float4 time, SamplerState materialSampler, uint queryFlags, out HikariRaySurface hit,
    float2 rayCone, inout float transmittance)
{
    hit = (HikariRaySurface)0;
    const bool shadowRay = (queryFlags & HIKARI_RAY_SHADOW) != 0u;
    const bool occlusionOnly = shadowRay || (queryFlags & HIKARI_RAY_OCCLUSION) != 0u;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(gRuntimeRayScene, occlusionOnly ? RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH : RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed())
    {
        if (query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
        HikariRaySurface candidate;
        uint flags;
        if (!HikariDecodeRayCandidate(query, hitRecordCount, occlusionOnly ? 0.0f.xx : rayCone, candidate, flags) ||
            candidate.materialIndex >= materialCount || (shadowRay && (flags & 2u) == 0u)) continue;
        const HikariMeshMaterialData material = HikariGetMeshMaterialData(candidate.materialIndex);
        const bool doubleSided = (flags & 1u) != 0u || (material.instanceFlags & HIKARI_PROGRAM_FLAG_DOUBLE_SIDED) != 0u;
        const bool backFace = dot(candidate.geometricNormal, ray.Direction) > 0.0f;
        // 影の光線は裏面でも遮る（カスケードの影と同じ）。始点が接地した物の張り出しの内側に入っても、
        // 中から抜ける時に裏面を飛ばすと影の中に光の筋が出る。
        if (!doubleSided && backFace && !shadowRay && (queryFlags & HIKARI_RAY_REPORT_BACKFACES) == 0u) continue;
        candidate.backFace = backFace ? 1u : 0u;
        if (backFace) { candidate.geometricNormal = -candidate.geometricNormal; candidate.normal = -candidate.normal; }
        // alpha test は stochastic ray 自身の交差位置で評価する。低 mip の平均 alpha を
        // threshold と比較すると細い葉や穴が消える。完全な shading は最終 hit だけ。
        if ((material.materialFlags & HIKARI_MATERIAL_FLAG_ALPHA_MASK) != 0u &&
            (queryFlags & HIKARI_RAY_GEOMETRY_ONLY) == 0u)
        {
            float4 coverage = HikariResolveMaterialBaseColor(material, candidate.uv0, candidate.uv1, materialSampler, true);
            float cutoff = material.pbrParams.w;
            const HikariMaterialExpressionContext coverageContext = HikariBuildMaterialExpressionContext(
                candidate.uv0, candidate.uv1, time, candidate.position, candidate.normal,
                candidate.tangent, ray.Origin, 0.0f.xxxx, 0.0f.xx);
            const HikariMaterialExpressionResult coverageExpression = HikariEvaluateMaterialExpression(material, coverageContext, materialSampler);
            HikariApplyMaterialExpressionCoverage(coverageExpression, coverage, cutoff);
            if (coverage.a < cutoff) continue;
        }
        if ((flags & HIKARI_RAY_HIT_TRANSMISSIVE) != 0u)
        {
            if (shadowRay)
                transmittance *= HikariRayTransmittance(material, candidate, materialSampler);
            continue;
        }
        query.CommitNonOpaqueTriangleHit();
        hit = candidate;
    }
    const bool found = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    if (found && !occlusionOnly && (queryFlags & HIKARI_RAY_GEOMETRY_ONLY) == 0u)
    {
        const HikariMeshMaterialData material = HikariGetMeshMaterialData(hit.materialIndex);
        const float3 baseNormal = hit.normal;
        hit.baseColor = HikariResolveMaterialBaseColor(material, hit.uv0, hit.uv1, materialSampler, true, hit.footprint);
        const HikariMaterialExpressionContext expressionContext = HikariBuildMaterialExpressionContext(
            hit.uv0, hit.uv1, time, hit.position, hit.normal, hit.tangent, ray.Origin, 0.0f.xxxx, 0.0f.xx, hit.footprint);
        HikariMaterialExpressionResult expression = HikariEvaluateMaterialExpression(material, expressionContext, materialSampler);
        expression.baseColor = HikariResolveMaterialSourceColor(material, expression.baseColor);
        float cutoff = material.pbrParams.w;
        HikariApplyMaterialExpressionCoverage(expression, hit.baseColor, cutoff);
        hit.pbr = HikariResolveMaterialPbrInputs(material, hit.uv0, hit.uv1, materialSampler, hit.footprint);
        HikariApplyMaterialExpressionPbr(expression, hit.pbr);
        hit.normal = HikariResolveSurfaceShadingNormal(material, baseNormal, hit.tangent,
            hit.uv0, hit.uv1, materialSampler, hit.footprint);
        hit.normal = HikariResolveMaterialExpressionNormal(expression, material.normalScale, baseNormal, hit.tangent, hit.normal);
        hit.emissive = HikariResolveMaterialEmissive(material, hit.uv0, hit.uv1, materialSampler, hit.footprint);
        if ((expression.outputMask & HIKARI_EXPRESSION_OUTPUT_EMISSIVE) != 0u) hit.emissive = expression.emissive;
    }
    return found;
}
bool HikariTraceRuntimeScene(RayDesc ray, uint hitRecordCount, uint materialCount,
    float4 time, SamplerState materialSampler, uint queryFlags, out HikariRaySurface hit,
    float2 rayCone = 0.0f.xx)
{
    float transmittance = 1.0f;
    return HikariTraceRuntimeSceneCore(ray, hitRecordCount, materialCount, time, materialSampler, queryFlags, hit,
        rayCone, transmittance);
}

// 太陽などへの影の光線の通る割合（不透明に当たれば 0、半透明は透過率の積）。
float HikariTraceRuntimeShadowTransmittance(RayDesc ray, uint hitRecordCount, uint materialCount, float4 time,
    SamplerState materialSampler)
{
    float transmittance = 1.0f;
    HikariRaySurface ignored;
    return HikariTraceRuntimeSceneCore(ray, hitRecordCount, materialCount, time, materialSampler, HIKARI_RAY_SHADOW,
        ignored, 0.0f.xx, transmittance) ? 0.0f : transmittance;
}
#endif
