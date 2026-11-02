#include "../Include/Lighting/HIKARI_LocalLightContract.hlsli"
#include "../Include/Lighting/HIKARI_LightingTransportMaterial.hlsli"
#include "../Include/Sky/HIKARI_SkyRadiance.hlsli"
#include "../Include/Sky/HIKARI_EquirectangularSampling.hlsli"
#include "../Include/HIKARI_DirectionEncoding.hlsli"

struct TransportTriangle
{
    float4 position0;
    float4 position1;
    float4 position2;
    float4 normal0;
    float4 normal1;
    float4 normal2;
    float4 tangent[3];
    float4 materialUv[3];
    uint materialIndex;
    uint flags; // bit0 両面
    uint2 padding;
};

struct TransportTexel
{
    float4 position;
    float4 normal;
};

struct TransportLight
{
    float4 positionRange;
    float4 directionOuterCone;
    float4 radianceInnerCone;
    uint type;
    float angularFalloff;
    uint reserved;
    float sourceRadius;
};

cbuffer BakeConstants : register(b0)
{
    uint gWidth;
    uint gHeight;
    uint gSampleIndex;
    uint gBounceCount;
    uint gTriangleCount;
    uint gLightCount;
    uint gSkyMode;
    uint gSkyTextureIndex;
    uint gSampleGridWidth;
    uint gSampleGridHeight;
    // 加速構造の geometry 0 が不透明、geometry 1 が切り抜き。三角形はこの順に並ぶ。
    uint gOpaqueTriangleCount;
    // 0: 光照贴图の texel。1: 探針（gWidth は探針一つの 16x16 の八面体の方向、gHeight は探針の数。texel の位置が探針）。
    uint gProbeTarget;
    float4 gSkyParams; // x 天空光の倍率、y 空の回転、z 探針の距離の上限
    float4 gSkyTintExposure;
};

cbuffer AtmosphereCB : register(b1)
{
    float4 gAtmospherePlanetParams;
    float4 gAtmosphereRayleigh;
    float4 gAtmosphereMie;
    float4 gAtmosphereOzone;
    float4 gAtmosphereOzoneParams;
    float4 gAtmosphereSunDirection;
    float4 gAtmosphereSunColorRadiance;
    float4 gAtmosphereGroundColor;
};
#include "../Include/Atmosphere/HIKARI_AtmosphereScattering.hlsli"

RaytracingAccelerationStructure gScene : register(t0);
StructuredBuffer<TransportTriangle> gTriangles : register(t1);
StructuredBuffer<TransportTexel> gTexels : register(t2);
StructuredBuffer<TransportLight> gLights : register(t3);
TextureCube gEnvironmentCube : register(t4);
// texel ごとの積算。radiance.rgb は裏面に当たらなかった標本の放射輝度の和、w は全標本数。statistics.x は最初の光線が
// 裏面に当たった標本数（多い texel は形状に埋まっているので CPU が無効にして周りから埋める）、y は表の標本の輝度の二乗和
// （降噪が texel ごとの雑音の大きさに使う）、z は最初の光線が空へ抜けた標本数（余弦で重みづけた空の見える割合。
// 実行時の鏡面の遮蔽に使う）。探針では y と w が表の標本の最初に当たるまでの距離の和と二乗和（遮蔽の判定に使う）。
struct LightmapAccumulation
{
    float4 radiance;
    float4 statistics;
};
RWStructuredBuffer<LightmapAccumulation> gOutput : register(u0);
SamplerState gMaterialSampler : register(s0);
SamplerState gEnvironmentSampler : register(s1);

static const float HIKARI_PI = 3.14159265359f;
static const float HIKARI_FLOAT_MAX = 3.402823e+38f;

bool IsFiniteFloat(float value)
{
    return value == value && abs(value) <= HIKARI_FLOAT_MAX;
}

bool IsFiniteFloat3(float3 value)
{
    return
        all(value == value) &&
        all(abs(value) <= HIKARI_FLOAT_MAX);
}

bool IsValidRadiance(float3 value)
{
    return
        IsFiniteFloat3(value) &&
        all(value >= 0.0f.xxx);
}

bool TryNormalizeVector(
    float3 value,
    out float3 normalizedValue)
{
    // 面積を持つ幾何法線も渡される。固定の長さ閾値は微小 Triangle の
    // 有効な hit を拒否するため、最大成分で尺度を除いてから正規化する。
    const float scale = max(abs(value.x), max(abs(value.y), abs(value.z)));
    if (!IsFiniteFloat3(value) || scale <= 0.0f)
    {
        normalizedValue = 0.0f.xxx;
        return false;
    }
    const float3 scaledValue = value / scale;
    normalizedValue = scaledValue * rsqrt(dot(scaledValue, scaledValue));
    return IsFiniteFloat3(normalizedValue);
}

uint HashPcg(uint value)
{
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) *
        277803737u;
    return (word >> 22u) ^ word;
}

float Random01(inout uint state)
{
    state = HashPcg(state);
    return (float)(state & 0x00ffffffu) / 16777216.0f;
}

float3 EvaluateSky(float3 direction)
{
    const float3 skyDirection = HikariRotateSkyYaw(direction, gSkyParams.y);
    float3 sky = 0.0f.xxx;
    if (gSkyMode == 2u) sky = gEnvironmentCube.SampleLevel(gEnvironmentSampler, skyDirection, 0).rgb;
    else if (gSkyMode == 3u) sky = HikariSampleMaterialTexture(gSkyTextureIndex, gEnvironmentSampler,
        HikariDirectionToEquirectangularUv(skyDirection), 0.0f.xxxx).rgb;
    // 大気は世界の太陽方向に従う。直接光の sun disk は二重積分しない。
    else if (gSkyMode == 4u) sky = HikariAtmosphereEvaluateSkyRadiance(
        float3(0, HikariAtmospherePlanetRadius() + 1.0f, 0), direction,
        gAtmosphereGroundColor.rgb, gAtmosphereOzoneParams.w, gAtmosphereOzoneParams.y);
    return HikariApplySkyRadianceScale(sky, gSkyTintExposure.rgb, gSkyTintExposure.w) * max(gSkyParams.x, 0.0f);
}

bool BuildBasis(
    float3 normal,
    out float3 tangent,
    out float3 bitangent)
{
    float3 helper =
        abs(normal.z) < 0.999f
        ? float3(0.0f, 0.0f, 1.0f)
        : float3(1.0f, 0.0f, 0.0f);
    if (!TryNormalizeVector(
            cross(helper, normal),
            tangent))
    {
        bitangent = 0.0f.xxx;
        return false;
    }
    bitangent = cross(normal, tangent);
    return IsFiniteFloat3(bitangent);
}

bool SampleCosineHemisphere(
    float3 normal,
    inout uint randomState,
    uint texelIndex,
    uint bounce,
    out float3 direction)
{
    float r0 = Random01(randomState);
    float r1 = Random01(randomState);
    if (bounce == 0u)
    {
        // 主半球を等面積の領域に分け、全 sample で各領域を一度ずつ訪れる。
        // 画素ごとの巡回順と領域内 jitter は固定の格子模様を避ける。
        const uint sampleCount = gSampleGridWidth * gSampleGridHeight;
        const uint stratum = (gSampleIndex + HashPcg(texelIndex) % sampleCount) % sampleCount;
        r0 = (float(stratum % gSampleGridWidth) + r0) / float(gSampleGridWidth);
        r1 = (float(stratum / gSampleGridWidth) + r1) / float(gSampleGridHeight);
    }
    float radius = sqrt(r0);
    float angle = 2.0f * HIKARI_PI * r1;
    float3 tangent;
    float3 bitangent;
    if (!BuildBasis(normal, tangent, bitangent))
    {
        direction = 0.0f.xxx;
        return false;
    }
    return TryNormalizeVector(
        tangent * (radius * cos(angle)) +
            bitangent * (radius * sin(angle)) +
            normal * sqrt(max(0.0f, 1.0f - r0)),
        direction);
}

uint TransportTriangleIndex(uint geometryIndex, uint primitiveIndex)
{
    return primitiveIndex + (geometryIndex != 0u ? gOpaqueTriangleCount : 0u);
}

// 切り抜きの三角形（geometry 1 だけが any-hit の候補になる）を交差位置で alpha test する。
bool CandidateCovers(uint geometryIndex, uint primitiveIndex, float2 barycentrics)
{
    const uint triangleIndex = TransportTriangleIndex(geometryIndex, primitiveIndex);
    if (triangleIndex >= gTriangleCount)
    {
        return false;
    }
    const TransportTriangle candidate = gTriangles[triangleIndex];
    const float3 weights = float3(1.0f - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    const float4 uv = candidate.materialUv[0] * weights.x + candidate.materialUv[1] * weights.y +
        candidate.materialUv[2] * weights.z;
    const float4 tangent = candidate.tangent[0] * weights.x + candidate.tangent[1] * weights.y +
        candidate.tangent[2] * weights.z;
    const float3 position = candidate.position0.xyz * weights.x + candidate.position1.xyz * weights.y +
        candidate.position2.xyz * weights.z;
    float3 normal;
    if (!TryNormalizeVector(candidate.normal0.xyz * weights.x + candidate.normal1.xyz * weights.y +
            candidate.normal2.xyz * weights.z, normal))
    {
        normal = float3(0.0f, 1.0f, 0.0f);
    }
    return HikariLightingTransportCovers(candidate.materialIndex, uv.xy, uv.zw, position, normal, tangent,
        gMaterialSampler);
}

bool TraceAny(float3 origin, float3 direction, float maximumDistance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001f;
    ray.TMax = maximumDistance;
    RayQuery<
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(gScene, RAY_FLAG_NONE, 0xffu, ray);
    while (query.Proceed())
    {
        if (CandidateCovers(query.CandidateGeometryIndex(), query.CandidatePrimitiveIndex(),
                query.CandidateTriangleBarycentrics()))
        {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

bool EvaluateDirect(
    float3 position,
    float3 normal,
    float3 geometricNormal,
    inout uint randomState,
    out float3 result)
{
    result = 0.0f.xxx;
    if (!IsFiniteFloat3(position) ||
        !IsFiniteFloat3(normal))
    {
        return false;
    }
    [loop]
    for (uint lightIndex = 0u;
        lightIndex < gLightCount;
        ++lightIndex)
    {
        TransportLight light = gLights[lightIndex];
        float3 directionToLight = 0.0f.xxx;
        float distanceToLight = 1.0e7f;
        float attenuation = 1.0f;
        if (light.type == 0u)
        {
            if (!TryNormalizeVector(
                    -light.directionOuterCone.xyz,
                    directionToLight))
            {
                return false;
            }
        }
        else
        {
            float3 offset = light.positionRange.xyz - position;
            float distanceSquared = dot(offset, offset);
            if (!IsFiniteFloat3(offset) ||
                !IsFiniteFloat(distanceSquared) ||
                !IsFiniteFloat(light.positionRange.w) ||
                light.positionRange.w <= 0.0f)
            {
                return false;
            }
            distanceSquared = max(distanceSquared, 0.0001f);
            distanceToLight = sqrt(distanceSquared);
            if (distanceToLight >= light.positionRange.w)
            {
                continue;
            }
            directionToLight = offset / distanceToLight;
            // realtime と共通の逆二乗の契約で評価する。
            attenuation = HikariLocalLightRangeAttenuation(
                distanceToLight, light.positionRange.w, light.sourceRadius);
            if (light.sourceRadius > 0.0f && distanceToLight > light.sourceRadius)
            {
                // 球の見かけの円盤を立体角で一様サンプルする。I/d^2 から
                // 放射輝度/pdf への補正も行い、半径だけで照度を増幅しない。
                const float halfAngleSineSquared = HikariLocalLightSourceHalfAngleSineSquared(
                    distanceToLight, light.sourceRadius);
                const float coneCos = 1.0f - 2.0f * halfAngleSineSquared;
                const float2 emitterSample = float2(Random01(randomState), Random01(randomState));
                const float sampleCos = 1.0f - 2.0f * halfAngleSineSquared * emitterSample.x;
                directionToLight = HikariSampleEmitterCone(directionToLight,halfAngleSineSquared,emitterSample);
                attenuation *= 2.0f / (1.0f + coneCos);
                const float projectedDistance = distanceToLight * sampleCos;
                distanceToLight = projectedDistance - sqrt(max(0.0f,
                    light.sourceRadius * light.sourceRadius -
                    distanceSquared * (1.0f - sampleCos * sampleCos)));
            }
            if (light.type == 2u)
            {
                float3 spotDirection;
                if (!TryNormalizeVector(
                        light.directionOuterCone.xyz,
                        spotDirection))
                {
                    return false;
                }
                float cone =
                    dot(
                        spotDirection,
                        -directionToLight);
                attenuation *= HikariLocalLightConeAttenuation(cone,
                    light.radianceInnerCone.w, light.directionOuterCone.w,
                    light.angularFalloff);
            }
        }
        if (!IsValidRadiance(
                light.radianceInnerCone.rgb) ||
            !IsFiniteFloat(attenuation))
        {
            return false;
        }
        float ndotl = saturate(dot(normal, directionToLight));
        if (ndotl <= 0.0f || dot(geometricNormal, directionToLight) <= 0.0f ||
            TraceAny(
                position + geometricNormal * 0.002f,
                directionToLight,
                max(distanceToLight - 0.003f, 0.001f)))
        {
            continue;
        }
        result +=
            light.radianceInnerCone.rgb *
            attenuation *
            ndotl /
            HIKARI_PI;
        if (!IsValidRadiance(result))
        {
            return false;
        }
    }
    return true;
}

bool TraceSurface(
    float3 origin,
    float3 direction,
    out float3 position,
    out float3 normal,
    out float3 geometricNormal,
    out float3 diffuse,
    out float3 emissive,
    out bool surfaceValid,
    out bool backFace)
{
    position = 0.0f.xxx;
    normal = 0.0f.xxx;
    geometricNormal = 0.0f.xxx;
    diffuse = 0.0f.xxx;
    emissive = 0.0f.xxx;
    surfaceValid = true;
    backFace = false;

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001f;
    ray.TMax = 1.0e7f;
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(
        gScene,
        RAY_FLAG_NONE,
        0xffu,
        ray);
    while (query.Proceed())
    {
        if (CandidateCovers(query.CandidateGeometryIndex(), query.CandidatePrimitiveIndex(),
                query.CandidateTriangleBarycentrics()))
        {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        return false;
    }
    uint primitiveIndex = TransportTriangleIndex(query.CommittedGeometryIndex(), query.CommittedPrimitiveIndex());
    if (primitiveIndex >= gTriangleCount)
    {
        surfaceValid = false;
        return false;
    }
    TransportTriangle surfaceTriangle = gTriangles[primitiveIndex];
    float2 barycentrics =
        query.CommittedTriangleBarycentrics();
    float3 barycentricWeights = float3(
        1.0f - barycentrics.x - barycentrics.y,
        barycentrics.x,
        barycentrics.y);
    float distance = query.CommittedRayT();
    position = origin + direction * distance;
    float3 interpolatedNormal =
        surfaceTriangle.normal0.xyz * barycentricWeights.x +
        surfaceTriangle.normal1.xyz * barycentricWeights.y +
        surfaceTriangle.normal2.xyz * barycentricWeights.z;
    const float3 edge01 = surfaceTriangle.position1.xyz - surfaceTriangle.position0.xyz;
    const float3 edge02 = surfaceTriangle.position2.xyz - surfaceTriangle.position0.xyz;
    if (!TryNormalizeVector(cross(edge01, edge02), geometricNormal))
    {
        surfaceValid = false;
        return false;
    }
    // 面の表は頂点法線の側（巻き順が逆の形状もある）。光線が表から入っていなければ裏面に当たった。両面の面
    // （一枚板の壁、葉）には裏がない。
    const float3 vertexNormalSum = surfaceTriangle.normal0.xyz + surfaceTriangle.normal1.xyz + surfaceTriangle.normal2.xyz;
    backFace = (surfaceTriangle.flags & 1u) == 0u &&
        dot(geometricNormal, direction) * (dot(geometricNormal, vertexNormalSum) < 0.0f ? -1.0f : 1.0f) > 0.0f;
    if (dot(geometricNormal, direction) > 0.0f) geometricNormal = -geometricNormal;
    if (!TryNormalizeVector(
            interpolatedNormal,
            normal))
    {
        normal = geometricNormal;
    }
    if (dot(normal, direction) > 0.0f)
    {
        normal = -normal;
    }
    const float4 uv = surfaceTriangle.materialUv[0] * barycentricWeights.x +
        surfaceTriangle.materialUv[1] * barycentricWeights.y + surfaceTriangle.materialUv[2] * barycentricWeights.z;
    const float4 tangent = surfaceTriangle.tangent[0] * barycentricWeights.x +
        surfaceTriangle.tangent[1] * barycentricWeights.y + surfaceTriangle.tangent[2] * barycentricWeights.z;
    const HikariLightingTransportMaterial material = HikariEvaluateLightingTransportMaterial(
        surfaceTriangle.materialIndex, uv.xy, uv.zw, position, normal, tangent, gMaterialSampler);
    if (!IsFiniteFloat3(position) ||
        !IsValidRadiance(material.diffuse) ||
        !IsValidRadiance(material.emissive) ||
        !TryNormalizeVector(material.normal, normal))
    {
        surfaceValid = false;
        return false;
    }
    diffuse = material.diffuse;
    emissive = material.emissive;
    return true;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gWidth ||
        dispatchThreadId.y >= gHeight)
    {
        return;
    }
    uint texelIndex =
        dispatchThreadId.y * gWidth +
        dispatchThreadId.x;
    TransportTexel texel = gTexels[texelIndex];
    if (texel.position.w <= 0.0f ||
        texel.normal.w <= 0.0f)
    {
        return;
    }

    uint randomState = HashPcg(
        texelIndex ^
        ((gSampleIndex + 1u) * 0x9e3779b9u));
    float3 position = texel.position.xyz;
    float3 normal;
    if (!IsFiniteFloat3(position) ||
        !TryNormalizeVector(
            texel.normal.xyz,
            normal))
    {
        return;
    }

    // Surface Lightmaps store indirect diffuse transport only. Direct
    // lighting remains live so moving lights and shadow maps are not counted
    // twice at runtime.
    // 探針は最初の光線を自分の八面体の方向の中で一様に選び、その向きから届く放射輝度を積む（光照贴图と同じ反弹と直射）。
    const bool probe = gProbeTarget != 0u;
    float firstDistance = gSkyParams.z;
    float3 radiance = 0.0f.xxx;
    float3 throughput = 1.0f.xxx;
    bool sampleValid = true;
    bool backFaceSample = false;
    bool skySample = false;
    float3 geometricNormal = normal;

    [loop]
    for (uint bounce = 0u; bounce < gBounceCount; ++bounce)
    {
        float3 direction;
        const bool probeRay = probe && bounce == 0u;
        if (probeRay)
        {
            const float2 jitter = float2(Random01(randomState), Random01(randomState));
            direction = HikariDecodeOctahedralDirection(
                (float2(dispatchThreadId.x % 16u, dispatchThreadId.x / 16u) + jitter) / 16.0f);
        }
        else if (!SampleCosineHemisphere(
                normal,
                randomState,
                texelIndex,
                bounce,
                direction))
        {
            sampleValid = false;
            break;
        }
        float3 hitPosition;
        float3 hitNormal;
        float3 hitGeometricNormal;
        if (!probeRay && dot(direction, geometricNormal) <= 0.0f) break;
        float3 hitDiffuse;
        float3 hitEmissive;
        bool surfaceValid;
        bool hitBackFace;
        const float3 origin = probeRay ? position : position + geometricNormal * 0.002f;
        const bool hit = TraceSurface(
                origin,
                direction,
                hitPosition,
                hitNormal,
                hitGeometricNormal,
                hitDiffuse,
                hitEmissive,
                surfaceValid,
                hitBackFace);
        if (!surfaceValid)
        {
            sampleValid = false;
            break;
        }
        if (probeRay && hit)
        {
            firstDistance = min(length(hitPosition - origin), gSkyParams.z);
        }
        // 裏面に当たった光線は形状の中を通った（texel が壁や床に埋まっている、交差の隙間から外へ抜けた）。裏面を表として
        // 照らすと壁の向こうの日光と空が室内の texel へ漏れる。最初の光線ならこの標本を裏面として数えて光を足さず、
        // 二度目以降は遮られたとみなす。
        if (hit && hitBackFace)
        {
            backFaceSample = bounce == 0u;
            break;
        }
        if (!hit)
        {
            skySample = bounce == 0u;
            const float3 skyRadiance =
                EvaluateSky(direction);
            if (!IsValidRadiance(skyRadiance))
            {
                sampleValid = false;
                break;
            }
            radiance += throughput * skyRadiance;
            break;
        }

        radiance += throughput * hitEmissive;
        throughput *= hitDiffuse;
        float3 directRadiance;
        if (!IsValidRadiance(throughput) ||
            !IsValidRadiance(radiance) ||
            !EvaluateDirect(
                hitPosition,
                hitNormal,
                hitGeometricNormal,
                randomState,
                directRadiance))
        {
            sampleValid = false;
            break;
        }
        radiance += throughput * directRadiance;
        position = hitPosition;
        normal = hitNormal;
        geometricNormal = hitGeometricNormal;

        if (bounce >= 1u)
        {
            float continuation =
                clamp(
                    max(
                        throughput.x,
                        max(throughput.y, throughput.z)),
                    0.1f,
                    0.95f);
            if (Random01(randomState) > continuation)
            {
                break;
            }
            throughput /= continuation;
        }
    }

    if (!sampleValid ||
        !IsValidRadiance(radiance))
    {
        return;
    }

    LightmapAccumulation accumulation = gOutput[texelIndex];
    const float3 nextRadiance = accumulation.radiance.rgb + radiance;
    if (!IsValidRadiance(accumulation.radiance.rgb) ||
        !IsFiniteFloat(accumulation.radiance.w) ||
        accumulation.radiance.w < 0.0f ||
        !IsValidRadiance(nextRadiance))
    {
        return;
    }
    const float luminance = dot(radiance, float3(0.2126f, 0.7152f, 0.0722f));
    accumulation.radiance = float4(nextRadiance, accumulation.radiance.w + 1.0f);
    accumulation.statistics.x += backFaceSample ? 1.0f : 0.0f;
    accumulation.statistics.y += backFaceSample ? 0.0f : probe ? firstDistance : luminance * luminance;
    accumulation.statistics.z += skySample ? 1.0f : 0.0f;
    accumulation.statistics.w += backFaceSample || !probe ? 0.0f : firstDistance * firstDistance;
    gOutput[texelIndex] = accumulation;
}
