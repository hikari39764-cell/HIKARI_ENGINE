#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"
#include "Include/Material/HIKARI_MaterialExpression.hlsli"

struct Particle
{
    float4 positionAge;
    float4 velocityLifetime;
    float4 physicsPositionSeed;
    float4 scaleRotation;
    float4 orientation;
    float4 color;
};

// These value-block offsets must stay in lockstep with the C++ emitter
// parameter packing in HIKARI_GpuParticleParameterPacking.cpp. They are
// derived from the same formulas instead of hand-maintained literals so
// the layouts cannot drift apart again.
static const uint HIKARI_PARTICLE_CURVE_KEYS = 8u;
static const uint HIKARI_PARTICLE_COLOR_STOPS = 8u;
static const uint HIKARI_PARTICLE_FLOAT3_DISTRIBUTION_VALUES = 2u;
static const uint HIKARI_PARTICLE_FLOAT4_DISTRIBUTION_VALUES = 3u;
static const uint HIKARI_PARTICLE_ANIMATED_FLOAT3_DISTRIBUTIONS = 6u;
static const uint HIKARI_PARTICLE_ANIMATED_FLOAT3_CURVES = 3u;
static const uint HIKARI_PARTICLE_CURVE_VALUES =
    2u + HIKARI_PARTICLE_CURVE_KEYS;
static const uint HIKARI_PARTICLE_ANIMATED_FLOAT3_VALUES =
    HIKARI_PARTICLE_FLOAT3_DISTRIBUTION_VALUES *
        HIKARI_PARTICLE_ANIMATED_FLOAT3_DISTRIBUTIONS +
    HIKARI_PARTICLE_CURVE_VALUES *
        HIKARI_PARTICLE_ANIMATED_FLOAT3_CURVES;
static const uint HIKARI_PARTICLE_GRADIENT_VALUES =
    1u + HIKARI_PARTICLE_COLOR_STOPS * 2u;
static const uint HIKARI_PARTICLE_EMITTER_HEADER_COUNT = 15u;
static const uint HIKARI_PARTICLE_POSITION_VALUES =
    HIKARI_PARTICLE_EMITTER_HEADER_COUNT;
static const uint HIKARI_PARTICLE_ROTATION_VALUES =
    HIKARI_PARTICLE_POSITION_VALUES +
    HIKARI_PARTICLE_ANIMATED_FLOAT3_VALUES;
static const uint HIKARI_PARTICLE_SCALE_VALUES =
    HIKARI_PARTICLE_ROTATION_VALUES +
    HIKARI_PARTICLE_ANIMATED_FLOAT3_VALUES;
static const uint HIKARI_PARTICLE_COLOR_DISTRIBUTIONS =
    HIKARI_PARTICLE_SCALE_VALUES +
    HIKARI_PARTICLE_ANIMATED_FLOAT3_VALUES;
static const uint HIKARI_PARTICLE_COLOR_CURVES =
    HIKARI_PARTICLE_COLOR_DISTRIBUTIONS +
    HIKARI_PARTICLE_FLOAT4_DISTRIBUTION_VALUES * 4u;
static const uint HIKARI_PARTICLE_COLOR_GRADIENT =
    HIKARI_PARTICLE_COLOR_CURVES +
    HIKARI_PARTICLE_CURVE_VALUES * 4u;
static const uint HIKARI_PARTICLE_EMITTER_VALUE_COUNT =
    HIKARI_PARTICLE_COLOR_GRADIENT +
    HIKARI_PARTICLE_GRADIENT_VALUES;
static const uint HIKARI_PARTICLE_RANDOM_LIFETIME = 1u;
static const uint HIKARI_PARTICLE_RANDOM_SHAPE_RADIUS = 2u;
static const uint HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_Y = 3u;
static const uint HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_ANGLE = 4u;
static const uint HIKARI_PARTICLE_RANDOM_SHAPE_OFFSET_RADIUS = 5u;
static const uint HIKARI_PARTICLE_RANDOM_RADIAL_SPEED = 6u;
static const uint HIKARI_PARTICLE_RANDOM_POSITION = 16u;
static const uint HIKARI_PARTICLE_RANDOM_ROTATION = 32u;
static const uint HIKARI_PARTICLE_RANDOM_SCALE = 48u;
static const uint HIKARI_PARTICLE_RANDOM_COLOR = 64u;
static const uint HIKARI_PARTICLE_ROTATED_BILLBOARD = 0u;
static const uint HIKARI_PARTICLE_FIXED_Y_AXIS = 1u;
static const uint HIKARI_PARTICLE_DIRECTIONAL_BILLBOARD = 2u;
static const uint HIKARI_PARTICLE_FIXED = 3u;

ByteAddressBuffer gEmitterData : register(t0);
RWStructuredBuffer<Particle> gParticles : register(u0);

cbuffer SimulationConstants : register(b0)
{
    uint gParticleOffset;
    uint gParticleCapacity;
    uint gEmitterIndex;
    uint gSpawnCursor;
    uint gSpawnCount;
    uint gRandomSeed;
    float gDeltaSeconds;
    uint gSimulationFlags;
};

uint Hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float4 LoadEmitterValue(uint emitterIndex, uint valueIndex)
{
    const uint byteOffset =
        (emitterIndex * HIKARI_PARTICLE_EMITTER_VALUE_COUNT + valueIndex) *
        16u;
    return asfloat(gEmitterData.Load4(byteOffset));
}

float3 RotateByQuaternion(float3 value, float4 rotation)
{
    float3 q = rotation.xyz;
    return value + 2.0f * cross(q, cross(q, value) + rotation.w * value);
}

float4 NormalizeQuaternion(float4 value)
{
    float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8f
        ? value * rsqrt(lengthSquared)
        : float4(0.0f, 0.0f, 0.0f, 1.0f);
}

float4 MultiplyQuaternion(float4 left, float4 right)
{
    return float4(
        left.w * right.xyz + right.w * left.xyz +
            cross(left.xyz, right.xyz),
        left.w * right.w - dot(left.xyz, right.xyz));
}

float4 QuaternionFromEulerXYZ(float3 rotation)
{
    float3 halfRotation = rotation * 0.5f;
    float3 cosine = cos(halfRotation);
    float3 sine = sin(halfRotation);
    return NormalizeQuaternion(float4(
        sine.x * cosine.y * cosine.z -
            cosine.x * sine.y * sine.z,
        cosine.x * sine.y * cosine.z +
            sine.x * cosine.y * sine.z,
        cosine.x * cosine.y * sine.z -
            sine.x * sine.y * cosine.z,
        cosine.x * cosine.y * cosine.z +
            sine.x * sine.y * sine.z));
}

float3 NormalizeOr(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8f
        ? value * rsqrt(lengthSquared)
        : fallback;
}

float3 PerpendicularTo(float3 axis)
{
    float3 reference = abs(axis.y) < 0.999f
        ? float3(0.0f, 1.0f, 0.0f)
        : float3(1.0f, 0.0f, 0.0f);
    return NormalizeOr(cross(axis, reference), float3(1.0f, 0.0f, 0.0f));
}

float RandomForProperty(
    uint seed,
    uint randomGroup,
    uint propertySalt)
{
    uint stream = randomGroup != 0u ? randomGroup : propertySalt;
    uint state = Hash(seed ^ ((stream + 1u) * 2246822519u));
    return (float)(state & 0x00ffffffu) / 16777216.0f;
}

float EvaluateFloatDistribution(
    uint emitterIndex,
    uint valueIndex,
    uint seed,
    uint propertySalt)
{
    float4 distribution = LoadEmitterValue(emitterIndex, valueIndex);
    uint mode = asuint(distribution.z);
    float amount = mode == 1u
        ? RandomForProperty(
            seed, asuint(distribution.w), propertySalt)
        : 0.0f;
    return lerp(distribution.x, distribution.y, amount);
}

float3 EvaluateFloat3Distribution(
    uint emitterIndex,
    uint valueIndex,
    uint seed,
    uint propertySalt)
{
    float4 minimum = LoadEmitterValue(emitterIndex, valueIndex);
    float4 maximum = LoadEmitterValue(emitterIndex, valueIndex + 1u);
    float amount = asuint(minimum.w) == 1u
        ? RandomForProperty(
            seed, asuint(maximum.w), propertySalt)
        : 0.0f;
    return lerp(minimum.xyz, maximum.xyz, amount);
}

float4 EvaluateFloat4Distribution(
    uint emitterIndex,
    uint valueIndex,
    uint seed,
    uint propertySalt)
{
    float4 minimum = LoadEmitterValue(emitterIndex, valueIndex);
    float4 maximum = LoadEmitterValue(emitterIndex, valueIndex + 1u);
    float4 metadata = LoadEmitterValue(emitterIndex, valueIndex + 2u);
    float amount = asuint(metadata.x) == 1u
        ? RandomForProperty(
            seed, asuint(metadata.y), propertySalt)
        : 0.0f;
    return lerp(minimum, maximum, amount);
}

float EvaluateCurve(
    uint emitterIndex,
    uint valueIndex,
    float normalizedAge,
    uint seed,
    uint propertySalt)
{
    float4 metadata = LoadEmitterValue(emitterIndex, valueIndex);
    uint keyCount = min(asuint(metadata.y), 8u);
    float4 offsetRange = LoadEmitterValue(emitterIndex, valueIndex + 1u);
    float randomOffset = asuint(metadata.z) == 1u
        ? lerp(
            offsetRange.x,
            offsetRange.y,
            RandomForProperty(
                seed, asuint(metadata.w), propertySalt))
        : offsetRange.x;
    if (keyCount == 0u)
    {
        return randomOffset;
    }
    float4 first = LoadEmitterValue(emitterIndex, valueIndex + 2u);
    if (keyCount == 1u || normalizedAge <= first.x)
    {
        return first.y + randomOffset;
    }

    uint interpolation = asuint(metadata.x);
    [unroll]
    for (uint keyIndex = 1u; keyIndex < 8u; ++keyIndex)
    {
        if (keyIndex >= keyCount)
        {
            break;
        }
        float4 previous = LoadEmitterValue(
            emitterIndex, valueIndex + 2u + keyIndex - 1u);
        float4 next = LoadEmitterValue(
            emitterIndex, valueIndex + 2u + keyIndex);
        if (normalizedAge <= next.x)
        {
            float duration = max(next.x - previous.x, 0.00001f);
            float amount = saturate((normalizedAge - previous.x) / duration);
            if (interpolation == 2u)
            {
                amount = 0.0f;
            }
            else if (interpolation == 1u)
            {
                float amount2 = amount * amount;
                float amount3 = amount2 * amount;
                float value =
                    (2.0f * amount3 - 3.0f * amount2 + 1.0f) *
                        previous.y +
                    (amount3 - 2.0f * amount2 + amount) *
                        previous.w * duration +
                    (-2.0f * amount3 + 3.0f * amount2) * next.y +
                    (amount3 - amount2) * next.z * duration;
                return value + randomOffset;
            }
            return lerp(previous.y, next.y, amount) + randomOffset;
        }
    }
    return LoadEmitterValue(
        emitterIndex,
        valueIndex + 2u + keyCount - 1u).y + randomOffset;
}

float ApplyEasing(float amount, uint easingFunction)
{
    amount = saturate(amount);
    if (easingFunction == 1u)
    {
        return amount * amount;
    }
    if (easingFunction == 2u)
    {
        float inverse = 1.0f - amount;
        return 1.0f - inverse * inverse;
    }
    if (easingFunction == 3u)
    {
        return amount < 0.5f
            ? 2.0f * amount * amount
            : 1.0f - pow(-2.0f * amount + 2.0f, 2.0f) * 0.5f;
    }
    return amount;
}

float3 EvaluateEasingFloat3(
    uint emitterIndex,
    uint valueIndex,
    float normalizedAge,
    uint seed,
    uint propertySalt,
    uint easingFunction,
    uint hasMiddle)
{
    float3 start = EvaluateFloat3Distribution(
        emitterIndex, valueIndex, seed, propertySalt + 3u);
    float3 middle = EvaluateFloat3Distribution(
        emitterIndex, valueIndex + 2u, seed, propertySalt + 4u);
    float3 end = EvaluateFloat3Distribution(
        emitterIndex, valueIndex + 4u, seed, propertySalt + 5u);
    if (hasMiddle == 0u)
    {
        return lerp(
            start,
            end,
            ApplyEasing(normalizedAge, easingFunction));
    }
    if (normalizedAge < 0.5f)
    {
        return lerp(
            start,
            middle,
            ApplyEasing(normalizedAge * 2.0f, easingFunction));
    }
    return lerp(
        middle,
        end,
        ApplyEasing(
            (normalizedAge - 0.5f) * 2.0f,
            easingFunction));
}

float3 EvaluateAnimatedFloat3(
    uint emitterIndex,
    uint valueIndex,
    uint mode,
    float age,
    float normalizedAge,
    uint seed,
    uint propertySalt,
    uint easingFunction,
    uint hasMiddle)
{
    if (mode == 1u)
    {
        float3 initial = EvaluateFloat3Distribution(
            emitterIndex, valueIndex, seed, propertySalt + 0u);
        float3 velocity = EvaluateFloat3Distribution(
            emitterIndex, valueIndex + 2u, seed, propertySalt + 1u);
        float3 acceleration = EvaluateFloat3Distribution(
            emitterIndex, valueIndex + 4u, seed, propertySalt + 2u);
        return initial + velocity * age + acceleration * age * age * 0.5f;
    }
    if (mode == 2u)
    {
        return EvaluateEasingFloat3(
            emitterIndex,
            valueIndex + 6u,
            normalizedAge,
            seed,
            propertySalt,
            easingFunction,
            hasMiddle);
    }
    if (mode == 3u)
    {
        uint curveIndex = valueIndex + 12u;
        return float3(
            EvaluateCurve(
                emitterIndex,
                curveIndex,
                normalizedAge,
                seed,
                propertySalt + 6u),
            EvaluateCurve(
                emitterIndex,
                curveIndex + HIKARI_PARTICLE_CURVE_VALUES,
                normalizedAge,
                seed,
                propertySalt + 7u),
            EvaluateCurve(
                emitterIndex,
                curveIndex + HIKARI_PARTICLE_CURVE_VALUES * 2u,
                normalizedAge,
                seed,
                propertySalt + 8u));
    }
    return EvaluateFloat3Distribution(
        emitterIndex, valueIndex, seed, propertySalt + 0u);
}

float4 EvaluateGradient(
    uint emitterIndex,
    float normalizedAge)
{
    uint stopCount = min(asuint(LoadEmitterValue(
        emitterIndex, HIKARI_PARTICLE_COLOR_GRADIENT).x), 8u);
    if (stopCount == 0u)
    {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    float firstTime = LoadEmitterValue(
        emitterIndex, HIKARI_PARTICLE_COLOR_GRADIENT + 1u).x;
    float4 firstColor = LoadEmitterValue(
        emitterIndex, HIKARI_PARTICLE_COLOR_GRADIENT + 2u);
    if (stopCount == 1u || normalizedAge <= firstTime)
    {
        return firstColor;
    }

    [unroll]
    for (uint stopIndex = 1u; stopIndex < 8u; ++stopIndex)
    {
        if (stopIndex >= stopCount)
        {
            break;
        }
        float previousTime = LoadEmitterValue(
            emitterIndex,
            HIKARI_PARTICLE_COLOR_GRADIENT + 1u +
                (stopIndex - 1u) * 2u).x;
        float4 previousColor = LoadEmitterValue(
            emitterIndex,
            HIKARI_PARTICLE_COLOR_GRADIENT + 2u +
                (stopIndex - 1u) * 2u);
        float nextTime = LoadEmitterValue(
            emitterIndex,
            HIKARI_PARTICLE_COLOR_GRADIENT + 1u +
                stopIndex * 2u).x;
        float4 nextColor = LoadEmitterValue(
            emitterIndex,
            HIKARI_PARTICLE_COLOR_GRADIENT + 2u +
                stopIndex * 2u);
        if (normalizedAge <= nextTime)
        {
            float amount = saturate(
                (normalizedAge - previousTime) /
                max(nextTime - previousTime, 0.00001f));
            return lerp(previousColor, nextColor, amount);
        }
    }
    return LoadEmitterValue(
        emitterIndex,
        HIKARI_PARTICLE_COLOR_GRADIENT + 2u +
            (stopCount - 1u) * 2u);
}

float4 EvaluateAnimatedColor(
    uint emitterIndex,
    uint mode,
    float normalizedAge,
    uint seed,
    uint propertySalt,
    uint easingFunction,
    uint hasMiddle)
{
    if (mode == 2u)
    {
        float4 start = EvaluateFloat4Distribution(
            emitterIndex,
            HIKARI_PARTICLE_COLOR_DISTRIBUTIONS + 3u,
            seed,
            propertySalt + 1u);
        float4 middle = EvaluateFloat4Distribution(
            emitterIndex,
            HIKARI_PARTICLE_COLOR_DISTRIBUTIONS + 6u,
            seed,
            propertySalt + 2u);
        float4 end = EvaluateFloat4Distribution(
            emitterIndex,
            HIKARI_PARTICLE_COLOR_DISTRIBUTIONS + 9u,
            seed,
            propertySalt + 3u);
        if (hasMiddle == 0u)
        {
            return lerp(
                start,
                end,
                ApplyEasing(normalizedAge, easingFunction));
        }
        if (normalizedAge < 0.5f)
        {
            return lerp(
                start,
                middle,
                ApplyEasing(normalizedAge * 2.0f, easingFunction));
        }
        return lerp(
            middle,
            end,
            ApplyEasing(
                (normalizedAge - 0.5f) * 2.0f,
                easingFunction));
    }
    if (mode == 3u)
    {
        return float4(
            EvaluateCurve(
                emitterIndex,
                HIKARI_PARTICLE_COLOR_CURVES,
                normalizedAge,
                seed,
                propertySalt + 4u),
            EvaluateCurve(
                emitterIndex,
                HIKARI_PARTICLE_COLOR_CURVES +
                    HIKARI_PARTICLE_CURVE_VALUES,
                normalizedAge,
                seed,
                propertySalt + 5u),
            EvaluateCurve(
                emitterIndex,
                HIKARI_PARTICLE_COLOR_CURVES +
                    HIKARI_PARTICLE_CURVE_VALUES * 2u,
                normalizedAge,
                seed,
                propertySalt + 6u),
            EvaluateCurve(
                emitterIndex,
                HIKARI_PARTICLE_COLOR_CURVES +
                    HIKARI_PARTICLE_CURVE_VALUES * 3u,
                normalizedAge,
                seed,
                propertySalt + 7u));
    }
    if (mode == 4u)
    {
        return EvaluateGradient(emitterIndex, normalizedAge);
    }
    return EvaluateFloat4Distribution(
        emitterIndex,
        HIKARI_PARTICLE_COLOR_DISTRIBUTIONS,
        seed,
        propertySalt + 0u);
}

void EvaluateParticlePresentation(
    uint emitterIndex,
    inout Particle particle)
{
    float normalizedAge = saturate(
        particle.positionAge.w / max(particle.velocityLifetime.w, 0.0001f));
    uint seed = asuint(particle.physicsPositionSeed.w);
    float4 animationModes = LoadEmitterValue(emitterIndex, 7u);
    float4 easingA = LoadEmitterValue(emitterIndex, 8u);
    float4 easingB = LoadEmitterValue(emitterIndex, 9u);
    float3 localPosition = EvaluateAnimatedFloat3(
        emitterIndex,
        HIKARI_PARTICLE_POSITION_VALUES,
        asuint(animationModes.x),
        particle.positionAge.w,
        normalizedAge,
        seed,
        HIKARI_PARTICLE_RANDOM_POSITION,
        asuint(easingA.x),
        asuint(easingA.y));
    float3 localRotation = EvaluateAnimatedFloat3(
        emitterIndex,
        HIKARI_PARTICLE_ROTATION_VALUES,
        asuint(animationModes.y),
        particle.positionAge.w,
        normalizedAge,
        seed,
        HIKARI_PARTICLE_RANDOM_ROTATION,
        asuint(easingA.z),
        asuint(easingA.w));
    float3 localScale = EvaluateAnimatedFloat3(
        emitterIndex,
        HIKARI_PARTICLE_SCALE_VALUES,
        asuint(animationModes.z),
        particle.positionAge.w,
        normalizedAge,
        seed,
        HIKARI_PARTICLE_RANDOM_SCALE,
        asuint(easingB.x),
        asuint(easingB.y));
    float4 worldScale = LoadEmitterValue(emitterIndex, 2u);
    float4 worldRotation = LoadEmitterValue(emitterIndex, 1u);
    particle.positionAge.xyz = particle.physicsPositionSeed.xyz +
        RotateByQuaternion(localPosition * worldScale.xyz, worldRotation);
    particle.scaleRotation = float4(
        abs(localScale.xy * worldScale.xy) *
            max(LoadEmitterValue(emitterIndex, 10u).y, 0.0f),
        localRotation.z,
        0.0f);
    particle.orientation = NormalizeQuaternion(MultiplyQuaternion(
        NormalizeQuaternion(worldRotation),
        QuaternionFromEulerXYZ(localRotation)));
    particle.color = EvaluateAnimatedColor(
        emitterIndex,
        asuint(animationModes.w),
        normalizedAge,
        seed,
        HIKARI_PARTICLE_RANDOM_COLOR,
        asuint(easingB.z),
        asuint(easingB.w)) * LoadEmitterValue(emitterIndex, 6u);
}

[numthreads(64, 1, 1)]
void ResetCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gParticleCapacity)
    {
        return;
    }
    Particle particle = (Particle)0;
    gParticles[gParticleOffset + dispatchThreadId.x] = particle;
}

[numthreads(64, 1, 1)]
void UpdateCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gParticleCapacity)
    {
        return;
    }
    uint particleIndex = gParticleOffset + dispatchThreadId.x;
    Particle particle = gParticles[particleIndex];
    if (particle.velocityLifetime.w <= 0.0f)
    {
        return;
    }

    particle.positionAge.w += max(gDeltaSeconds, 0.0f);
    if (particle.positionAge.w >= particle.velocityLifetime.w)
    {
        particle.velocityLifetime.w = 0.0f;
        particle.color.a = 0.0f;
        gParticles[particleIndex] = particle;
        return;
    }

    float4 forces = LoadEmitterValue(gEmitterIndex, 5u);
    float3 gravity = forces.xyz *
        LoadEmitterValue(gEmitterIndex, 10u).z;
    float drag = max(forces.w, 0.0f);
    particle.velocityLifetime.xyz += gravity * gDeltaSeconds;
    particle.velocityLifetime.xyz *= exp(-drag * gDeltaSeconds);
    particle.physicsPositionSeed.xyz +=
        particle.velocityLifetime.xyz * gDeltaSeconds;
    EvaluateParticlePresentation(gEmitterIndex, particle);
    gParticles[particleIndex] = particle;
}

[numthreads(64, 1, 1)]
void SpawnCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gSpawnCount || gParticleCapacity == 0u)
    {
        return;
    }

    if (asuint(LoadEmitterValue(gEmitterIndex, 10u).w) == 0u)
    {
        return;
    }
    uint particleSlot =
        (gSpawnCursor + dispatchThreadId.x) % gParticleCapacity;
    uint particleSeed = Hash(
        gRandomSeed ^ (particleSlot * 747796405u));
    float3 localPosition = 0.0f;
    float3 localDirection = float3(0.0f, 1.0f, 0.0f);
    float4 shapeParameters = LoadEmitterValue(gEmitterIndex, 4u);
    float4 emitterOrigin = LoadEmitterValue(gEmitterIndex, 0u);
    float4 emitterScale = LoadEmitterValue(gEmitterIndex, 2u);
    uint shape = asuint(shapeParameters.x);
    float shapeRadius = lerp(
        emitterOrigin.w,
        emitterScale.w,
        asuint(shapeParameters.z) == 1u
            ? RandomForProperty(
                particleSeed,
                asuint(shapeParameters.w),
                HIKARI_PARTICLE_RANDOM_SHAPE_RADIUS)
            : 0.0f);
    shapeRadius = max(shapeRadius, 0.0f);
    if (shape == 1u)
    {
        float y = lerp(
            -1.0f,
            1.0f,
            RandomForProperty(
                particleSeed,
                0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_Y));
        float angle = RandomForProperty(
            particleSeed,
            0u,
            HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_ANGLE) *
            6.28318530718f;
        float radius = pow(RandomForProperty(
            particleSeed,
            0u,
            HIKARI_PARTICLE_RANDOM_SHAPE_OFFSET_RADIUS),
            1.0f / 3.0f) *
            shapeRadius;
        float radial = sqrt(max(0.0f, 1.0f - y * y));
        localDirection = float3(
            radial * cos(angle),
            y,
            radial * sin(angle));
        localPosition = localDirection * radius;
    }
    else if (shape == 2u)
    {
        float halfAngle = clamp(
            shapeParameters.y * 0.5f,
            0.0f,
            3.14159265359f);
        float y = lerp(
            cos(halfAngle),
            1.0f,
            RandomForProperty(
                particleSeed,
                0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_Y));
        float angle = RandomForProperty(
            particleSeed,
            0u,
            HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_ANGLE) *
            6.28318530718f;
        float radial = sqrt(max(0.0f, 1.0f - y * y));
        localDirection = float3(
            radial * cos(angle),
            y,
            radial * sin(angle));
        float distance = pow(RandomForProperty(
            particleSeed,
            0u,
            HIKARI_PARTICLE_RANDOM_SHAPE_OFFSET_RADIUS),
            1.0f / 3.0f) * shapeRadius;
        localPosition = localDirection * distance;
    }

    else if (shape == 3u)
    {
        float4 shapeBox = LoadEmitterValue(gEmitterIndex, 12u);
        localPosition = float3(
            lerp(-shapeBox.x, shapeBox.x, RandomForProperty(
                particleSeed, 0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_Y)),
            lerp(-shapeBox.y, shapeBox.y, RandomForProperty(
                particleSeed, 0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_ANGLE)),
            lerp(-shapeBox.z, shapeBox.z, RandomForProperty(
                particleSeed, 0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_OFFSET_RADIUS)));
        localDirection = float3(0.0f, 1.0f, 0.0f);
    }
    else if (shape == 4u)
    {
        float4 shapeBox = LoadEmitterValue(gEmitterIndex, 12u);
        float angle = RandomForProperty(
            particleSeed, 0u,
            HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_ANGLE) *
            6.28318530718f;
        float distance = sqrt(RandomForProperty(
            particleSeed, 0u,
            HIKARI_PARTICLE_RANDOM_SHAPE_OFFSET_RADIUS)) * shapeRadius;
        float y = lerp(
            -shapeBox.w * 0.5f,
            shapeBox.w * 0.5f,
            RandomForProperty(
                particleSeed, 0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_Y));
        localPosition = float3(
            cos(angle) * distance, y, sin(angle) * distance);
        localDirection = float3(0.0f, 1.0f, 0.0f);
    }
    else if (shape == 5u)
    {
        float4 shapeCircle = LoadEmitterValue(gEmitterIndex, 13u);
        float angle = RandomForProperty(
            particleSeed, 0u,
            HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_ANGLE) *
            6.28318530718f;
        float innerRadius = clamp(shapeCircle.x, 0.0f, shapeRadius);
        float distance = shapeRadius;
        if (asuint(shapeCircle.y) == 0u && shapeRadius > innerRadius)
        {
            float u = RandomForProperty(
                particleSeed, 0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_OFFSET_RADIUS);
            distance = sqrt(lerp(
                innerRadius * innerRadius,
                shapeRadius * shapeRadius,
                u));
        }
        localPosition = float3(
            cos(angle) * distance, 0.0f, sin(angle) * distance);
        localDirection = float3(0.0f, 1.0f, 0.0f);
    }
    else if (shape == 6u)
    {
        float4 shapeCircle = LoadEmitterValue(gEmitterIndex, 13u);
        float halfLength = shapeCircle.z * 0.5f;
        localPosition = float3(
            lerp(-halfLength, halfLength, RandomForProperty(
                particleSeed, 0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_Y)),
            0.0f,
            0.0f);
        localDirection = float3(0.0f, 1.0f, 0.0f);
    }

    float4 rotation = LoadEmitterValue(gEmitterIndex, 1u);
    localPosition *= emitterScale.xyz;
    float3 worldPosition = emitterOrigin.xyz +
        RotateByQuaternion(localPosition, rotation);
    float3 worldDirection = normalize(
        RotateByQuaternion(localDirection, rotation));
    float4 lifetimeAndSpeed = LoadEmitterValue(gEmitterIndex, 3u);
    float4 distributionMetadata = LoadEmitterValue(gEmitterIndex, 11u);
    float lifetime = lerp(
        lifetimeAndSpeed.x,
        lifetimeAndSpeed.y,
        asuint(distributionMetadata.x) == 1u
            ? RandomForProperty(
                particleSeed,
                asuint(distributionMetadata.y),
                HIKARI_PARTICLE_RANDOM_LIFETIME)
            : 0.0f);
    float speed = lerp(
        lifetimeAndSpeed.z,
        lifetimeAndSpeed.w,
        asuint(distributionMetadata.z) == 1u
            ? RandomForProperty(
                particleSeed,
                asuint(distributionMetadata.w),
                HIKARI_PARTICLE_RANDOM_RADIAL_SPEED)
            : 0.0f) * LoadEmitterValue(gEmitterIndex, 10u).x;

    Particle particle = (Particle)0;
    particle.positionAge = float4(worldPosition, 0.0f);
    particle.velocityLifetime = float4(worldDirection * speed, max(lifetime, 0.0001f));
    particle.physicsPositionSeed = float4(
        worldPosition,
        asfloat(particleSeed));
    EvaluateParticlePresentation(gEmitterIndex, particle);
    uint particleIndex = gParticleOffset +
        particleSlot;
    gParticles[particleIndex] = particle;
}

StructuredBuffer<Particle> gDrawParticles : register(t1);
SamplerState gParticleSampler : register(s0);

cbuffer DrawConstants : register(b0)
{
    float4x4 gViewProjection;
    float4 gCameraRight;
    float4 gCameraUp;
    float4 gCameraPosition;
    float4 gTimeParams;
    float4 gScreenParams;
    uint gBillboardMode;
    uint gRenderShapeMode;
    uint gRenderShapeParam0;
    uint gRenderShapeParam1;
    float4 gRenderShapeParamsA;
    float4 gRenderShapeParamsB;
};

struct ParticleVsOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float alive : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
    float3 worldNormal : TEXCOORD3;
    float3 worldTangent : TEXCOORD4;
};

ParticleVsOutput ParticleVS(
    uint vertexId : SV_VertexID,
    uint instanceId : SV_InstanceID)
{
    static const float2 corners[6] = {
        float2(-0.5f, -0.5f),
        float2(-0.5f,  0.5f),
        float2( 0.5f,  0.5f),
        float2(-0.5f, -0.5f),
        float2( 0.5f,  0.5f),
        float2( 0.5f, -0.5f)
    };
    static const float2 uvs[6] = {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f)
    };

    Particle particle = gDrawParticles[instanceId];
    float2 corner = corners[vertexId % 6u];
    float2 uvCorner = uvs[vertexId % 6u];
    const float segmentCorner = corner.x + 0.5f;
    const float segmentSide = corner.y * 2.0f;
    bool isStripShape = false;
    float stripS = 0.0f;
    float stripSide = 0.0f;
    float stripHalfWidth = 0.0f;
    if (gRenderShapeMode == 1u)
    {
        uint segmentCount = max(gRenderShapeParam0, 2u);
        uint vertsPerStrip = segmentCount * 6u;
        uint quadIndex = (vertexId % vertsPerStrip) / 6u;
        stripS = ((float)quadIndex + segmentCorner) /
            (float)segmentCount;
        stripSide = segmentSide;
        stripHalfWidth = gRenderShapeParamsA.x * 0.5f;
        isStripShape = true;
    }
    else if (gRenderShapeMode == 2u)
    {
        uint segmentCount = max(gRenderShapeParam1, 3u);
        uint vertsPerRing = segmentCount * 6u;
        uint quadIndex = (vertexId % vertsPerRing) / 6u;
        float startAngle = gRenderShapeParamsA.w;
        float endAngle = gRenderShapeParamsB.x;
        float angleSpan = endAngle - startAngle;
        stripS = ((float)quadIndex + segmentCorner) /
            (float)segmentCount;
        float angle = startAngle + angleSpan * stripS;
        float outerRadius = gRenderShapeParamsA.y;
        float innerRadius = gRenderShapeParamsA.z;
        stripSide = segmentSide;
        float radius = stripSide < 0.0f ? innerRadius : outerRadius;
        corner = float2(cos(angle) * radius, sin(angle) * radius);
        uvCorner = float2(stripS, stripSide * 0.5f + 0.5f);
        stripS = angle;
    }
    float3 cameraRight = NormalizeOr(
        gCameraRight.xyz,
        float3(1.0f, 0.0f, 0.0f));
    float3 cameraUp = NormalizeOr(
        gCameraUp.xyz,
        float3(0.0f, 1.0f, 0.0f));
    float3 cameraNormal = NormalizeOr(
        cross(cameraRight, cameraUp),
        float3(0.0f, 0.0f, 1.0f));
    float3 toCamera = NormalizeOr(
        gCameraPosition.xyz - particle.positionAge.xyz,
        -cameraNormal);
    float3 cameraFacingNormal = NormalizeOr(
        toCamera,
        -cameraNormal);

    float3 right = cameraRight;
    float3 up = cameraUp;
    float3 normal = NormalizeOr(
        -cross(right, up),
        cameraFacingNormal);
    if (gBillboardMode == HIKARI_PARTICLE_FIXED)
    {
        float4 orientation = NormalizeQuaternion(particle.orientation);
        right = NormalizeOr(
            RotateByQuaternion(float3(1.0f, 0.0f, 0.0f), orientation),
            cameraRight);
        up = NormalizeOr(
            RotateByQuaternion(float3(0.0f, 1.0f, 0.0f), orientation),
            cameraUp);
        normal = NormalizeOr(
            RotateByQuaternion(float3(0.0f, 0.0f, -1.0f), orientation),
            cameraFacingNormal);
    }
    else if (gBillboardMode == HIKARI_PARTICLE_FIXED_Y_AXIS)
    {
        up = float3(0.0f, 1.0f, 0.0f);
        float3 fallbackHorizontalNormal = NormalizeOr(
            float3(
                cameraFacingNormal.x,
                0.0f,
                cameraFacingNormal.z),
            float3(0.0f, 0.0f, -1.0f));
        normal = NormalizeOr(
            float3(toCamera.x, 0.0f, toCamera.z),
            fallbackHorizontalNormal);
        right = NormalizeOr(cross(normal, up), cameraRight);
        normal = NormalizeOr(-cross(right, up), normal);
    }
    else if (gBillboardMode ==
        HIKARI_PARTICLE_DIRECTIONAL_BILLBOARD)
    {
        float3 projectedDirection = particle.velocityLifetime.xyz -
            cameraFacingNormal * dot(
                particle.velocityLifetime.xyz,
                cameraFacingNormal);
        float3 projectedCameraUp = cameraUp -
            cameraFacingNormal * dot(cameraUp, cameraFacingNormal);
        up = NormalizeOr(
            projectedDirection,
            NormalizeOr(
                projectedCameraUp,
                float3(0.0f, 1.0f, 0.0f)));
        right = NormalizeOr(
            cross(cameraFacingNormal, up),
            cameraRight);
        normal = NormalizeOr(
            -cross(right, up),
            cameraFacingNormal);
    }
    else
    {
        float sine = sin(particle.scaleRotation.z);
        float cosine = cos(particle.scaleRotation.z);
        float3 unrotatedRight = right;
        float3 unrotatedUp = up;
        right = NormalizeOr(
            unrotatedRight * cosine + unrotatedUp * sine,
            cameraRight);
        up = NormalizeOr(
            unrotatedRight * -sine + unrotatedUp * cosine,
            cameraUp);
        normal = NormalizeOr(
            -cross(right, up),
            cameraFacingNormal);
    }
    float3 worldPosition = particle.positionAge.xyz +
        right * corner.x * particle.scaleRotation.x +
        up * corner.y * particle.scaleRotation.y;
    if (isStripShape)
    {
        float4 stripOrientation = NormalizeQuaternion(
            particle.orientation);
        float3 stripTangent = NormalizeOr(RotateByQuaternion(
            float3(1.0f, 0.0f, 0.0f), stripOrientation), cameraRight);
        float3 stripNormal = NormalizeOr(RotateByQuaternion(
            float3(0.0f, 0.0f, -1.0f), stripOrientation),
            cameraFacingNormal);
        float3 toCameraStrip = NormalizeOr(
            gCameraPosition.xyz - particle.positionAge.xyz,
            cameraFacingNormal);
        float3 stripWidthDir = NormalizeOr(
            cross(toCameraStrip, stripTangent), cameraUp);
        float ribbonLength = max(particle.scaleRotation.x, 0.0f);
        float ribbonWidthScale = max(particle.scaleRotation.y, 0.0f);
        worldPosition = particle.positionAge.xyz +
            stripTangent * ((stripS - 0.5f) * ribbonLength) +
            stripWidthDir * (stripSide * stripHalfWidth *
                ribbonWidthScale);
        normal = stripNormal;
        right = stripTangent;
        uvCorner = float2(stripS, stripSide * 0.5f + 0.5f);
    }

    ParticleVsOutput output;
    output.position = mul(gViewProjection, float4(worldPosition, 1.0f));
    uint flipbookGrid = asuint(gRenderShapeParamsB.z);
    uint flipbookPlayback = asuint(gRenderShapeParamsB.w);
    uint flipbookColumns = max(flipbookGrid & 0xffu, 1u);
    uint flipbookRows = max((flipbookGrid >> 8u) & 0xffu, 1u);
    uint flipbookCellCount = flipbookColumns * flipbookRows;
    uint flipbookFrameCount = clamp(
        flipbookPlayback & 0xffffu, 1u, flipbookCellCount);
    uint flipbookFlags = flipbookPlayback >> 16u;
    uint flipbookFrame = (uint)floor(
        max(gRenderShapeParamsB.y, 0.0f) *
        max(particle.positionAge.w, 0.0f));
    if ((flipbookFlags & 2u) != 0u)
    {
        flipbookFrame += Hash(asuint(
            particle.physicsPositionSeed.w)) % flipbookFrameCount;
    }
    flipbookFrame = (flipbookFlags & 1u) != 0u
        ? flipbookFrame % flipbookFrameCount
        : min(flipbookFrame, flipbookFrameCount - 1u);
    uint flipbookColumn = flipbookFrame % flipbookColumns;
    uint flipbookRow = flipbookFrame / flipbookColumns;
    output.uv = (float2(flipbookColumn, flipbookRow) + uvCorner) /
        float2(flipbookColumns, flipbookRows);
    output.color = particle.color;
    output.alive = particle.velocityLifetime.w > 0.0f ? 1.0f : 0.0f;
    output.worldPosition = worldPosition;
    output.worldNormal = normal;
    output.worldTangent = right;
    return output;
}

float4 ParticlePS(ParticleVsOutput input) : SV_TARGET
{
    clip(input.alive - 0.5f);
    HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(gMaterialDataIndex);
    float4 materialColor = materialData.baseColor;
    float opacity = 1.0f;
    float3 emissive = materialData.emissiveFactor.rgb *
        materialData.emissiveFactor.w;
    float alphaClipThreshold = 0.001f;
    const bool hasParticleExpression =
        materialData.materialProgramIndex !=
        HIKARI_STANDARD_PBR_MATERIAL_PROGRAM;
    if (hasParticleExpression)
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
                materialData, context, gParticleSampler);
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
                expression.alphaClipThreshold, 0.0f);
        }
    }
    float4 result = float4(
        input.color.rgb * materialColor.rgb,
        input.color.a * opacity);
    if (!hasParticleExpression)
    {
        result.a *= materialColor.a;
    }
    result.rgb += emissive;
    clip(result.a - alphaClipThreshold);
    return result;
}
