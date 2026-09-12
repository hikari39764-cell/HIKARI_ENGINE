#include "../Include/Lighting/HIKARI_LocalLightContract.hlsli"
#include "../Include/Lighting/HIKARI_LightingTransportMaterial.hlsli"

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
    uint3 padding;
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
    uint distanceFalloff;
    uint padding;
};

cbuffer BakeConstants : register(b0)
{
    uint gWidth;
    uint gHeight;
    uint gSampleIndex;
    uint gBounceCount;
    uint gTriangleCount;
    uint gLightCount;
    uint2 gPadding;
    float4 gAmbientSkyStrength;
    float4 gSkyZenith;
    float4 gSkyHorizon;
    float4 gSkyGround;
};

RaytracingAccelerationStructure gScene : register(t0);
StructuredBuffer<TransportTriangle> gTriangles : register(t1);
StructuredBuffer<TransportTexel> gTexels : register(t2);
StructuredBuffer<TransportLight> gLights : register(t3);
RWStructuredBuffer<float4> gOutput : register(u0);
SamplerState gMaterialSampler : register(s0);

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
    const float lengthSquared = dot(value, value);
    if (!IsFiniteFloat3(value) ||
        !IsFiniteFloat(lengthSquared) ||
        lengthSquared <= 1.0e-12f)
    {
        normalizedValue = 0.0f.xxx;
        return false;
    }
    normalizedValue = value * rsqrt(lengthSquared);
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
    float horizonWeight =
        pow(saturate(1.0f - abs(direction.y)), 1.5f);
    float3 hemisphere =
        direction.y >= 0.0f
        ? lerp(gSkyHorizon.rgb, gSkyZenith.rgb, direction.y)
        : lerp(gSkyHorizon.rgb, gSkyGround.rgb, -direction.y);
    float3 sky = lerp(hemisphere, gSkyHorizon.rgb, horizonWeight);
    return lerp(
        gAmbientSkyStrength.rgb,
        sky,
        saturate(gAmbientSkyStrength.w));
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
    out float3 direction)
{
    float r0 = Random01(randomState);
    float r1 = Random01(randomState);
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
    }
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

bool EvaluateDirect(
    float3 position,
    float3 normal,
    float3 geometricNormal,
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
            // 保存された衰減モードを realtime と共通の契約で評価する。
            attenuation = HikariLocalLightRangeAttenuation(
                distanceToLight, light.positionRange.w, light.distanceFalloff);
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
    out bool surfaceValid)
{
    position = 0.0f.xxx;
    normal = 0.0f.xxx;
    geometricNormal = 0.0f.xxx;
    diffuse = 0.0f.xxx;
    emissive = 0.0f.xxx;
    surfaceValid = true;

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
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        return false;
    }
    uint primitiveIndex = query.CommittedPrimitiveIndex();
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
    float3 radiance = 0.0f.xxx;
    float3 throughput = 1.0f.xxx;
    bool sampleValid = true;
    float3 geometricNormal = normal;

    [loop]
    for (uint bounce = 0u; bounce < gBounceCount; ++bounce)
    {
        float3 direction;
        if (!SampleCosineHemisphere(
                normal,
                randomState,
                direction))
        {
            sampleValid = false;
            break;
        }
        float3 hitPosition;
        float3 hitNormal;
        float3 hitGeometricNormal;
        if (dot(direction, geometricNormal) <= 0.0f) break;
        float3 hitDiffuse;
        float3 hitEmissive;
        bool surfaceValid;
        const bool hit = TraceSurface(
                position + geometricNormal * 0.002f,
                direction,
                hitPosition,
                hitNormal,
                hitGeometricNormal,
                hitDiffuse,
                hitEmissive,
                surfaceValid);
        if (!surfaceValid)
        {
            sampleValid = false;
            break;
        }
        if (!hit)
        {
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

    const float4 accumulation =
        gOutput[texelIndex];
    const float3 nextRadiance =
        accumulation.rgb + radiance;
    if (!IsValidRadiance(accumulation.rgb) ||
        !IsFiniteFloat(accumulation.w) ||
        accumulation.w < 0.0f ||
        !IsValidRadiance(nextRadiance))
    {
        return;
    }
    gOutput[texelIndex] =
        float4(
            nextRadiance,
            accumulation.w + 1.0f);
}
