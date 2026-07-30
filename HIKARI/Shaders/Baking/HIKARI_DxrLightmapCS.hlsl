struct TransportTriangle
{
    float4 position0;
    float4 position1;
    float4 position2;
    float4 normal0;
    float4 normal1;
    float4 normal2;
    float4 diffuse;
    float4 emissive;
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
    uint3 padding;
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

static const float HIKARI_PI = 3.14159265359f;

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

void BuildBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
    float3 helper =
        abs(normal.z) < 0.999f
        ? float3(0.0f, 0.0f, 1.0f)
        : float3(1.0f, 0.0f, 0.0f);
    tangent = normalize(cross(helper, normal));
    bitangent = cross(normal, tangent);
}

float3 SampleCosineHemisphere(
    float3 normal,
    inout uint randomState)
{
    float r0 = Random01(randomState);
    float r1 = Random01(randomState);
    float radius = sqrt(r0);
    float angle = 2.0f * HIKARI_PI * r1;
    float3 tangent;
    float3 bitangent;
    BuildBasis(normal, tangent, bitangent);
    return normalize(
        tangent * (radius * cos(angle)) +
        bitangent * (radius * sin(angle)) +
        normal * sqrt(max(0.0f, 1.0f - r0)));
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

float3 EvaluateDirect(float3 position, float3 normal)
{
    float3 result = 0.0f.xxx;
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
            directionToLight =
                normalize(-light.directionOuterCone.xyz);
        }
        else
        {
            float3 offset = light.positionRange.xyz - position;
            float distanceSquared = max(dot(offset, offset), 0.0001f);
            distanceToLight = sqrt(distanceSquared);
            if (distanceToLight >= light.positionRange.w)
            {
                continue;
            }
            directionToLight = offset / distanceToLight;
            float rangeWeight =
                saturate(1.0f -
                    distanceToLight /
                        max(light.positionRange.w, 0.001f));
            attenuation =
                rangeWeight * rangeWeight /
                max(distanceSquared, 0.01f);
            if (light.type == 2u)
            {
                float cone =
                    dot(
                        normalize(light.directionOuterCone.xyz),
                        -directionToLight);
                float coneWeight = saturate(
                    (cone - light.directionOuterCone.w) /
                    max(
                        light.radianceInnerCone.w -
                            light.directionOuterCone.w,
                        0.0001f));
                attenuation *= coneWeight * coneWeight;
            }
        }
        float ndotl = saturate(dot(normal, directionToLight));
        if (ndotl <= 0.0f ||
            TraceAny(
                position + normal * 0.002f,
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
    }
    return result;
}

bool TraceSurface(
    float3 origin,
    float3 direction,
    out float3 position,
    out float3 normal,
    out float3 diffuse,
    out float3 emissive)
{
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
    normal = normalize(
        surfaceTriangle.normal0.xyz * barycentricWeights.x +
        surfaceTriangle.normal1.xyz * barycentricWeights.y +
        surfaceTriangle.normal2.xyz * barycentricWeights.z);
    if (dot(normal, direction) > 0.0f)
    {
        normal = -normal;
    }
    diffuse = saturate(surfaceTriangle.diffuse.rgb);
    emissive = max(surfaceTriangle.emissive.rgb, 0.0f.xxx);
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
    float3 normal = normalize(texel.normal.xyz);
    float3 radiance = EvaluateDirect(position, normal);
    float3 throughput = 1.0f.xxx;

    [loop]
    for (uint bounce = 0u; bounce < gBounceCount; ++bounce)
    {
        float3 direction =
            SampleCosineHemisphere(normal, randomState);
        float3 hitPosition;
        float3 hitNormal;
        float3 hitDiffuse;
        float3 hitEmissive;
        if (!TraceSurface(
                position + normal * 0.002f,
                direction,
                hitPosition,
                hitNormal,
                hitDiffuse,
                hitEmissive))
        {
            radiance += throughput * EvaluateSky(direction);
            break;
        }

        throughput *= hitDiffuse;
        radiance += throughput * hitEmissive;
        radiance +=
            throughput *
            EvaluateDirect(hitPosition, hitNormal);
        position = hitPosition;
        normal = hitNormal;

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

    gOutput[texelIndex] += float4(
        min(radiance, 65504.0f.xxx),
        1.0f);
}
