#ifndef HIKARI_PARTICLE_EMITTER_EVALUATION
#define HIKARI_PARTICLE_EMITTER_EVALUATION
// 粒子の発射器の値（HIKARI_ParticleEmitterLayout.hlsli の並び）の評価：乱数、分布、曲線、緩動、渐变と、年齢から粒子の
// 位置・回転・大きさ・色を出す EvaluateParticlePresentation。読み込む側が先に ByteAddressBuffer gEmitterData（発射器の値）、
// 発射器の並びの定数、HIKARI_PARTICLE_RANDOM_* と HIKARI_ParticleData.hlsli・HIKARI_BillboardMath.hlsli を用意する。

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

float RandomForProperty(
    uint seed,
    uint randomGroup,
    uint propertySalt)
{
    uint stream = randomGroup != 0u ? randomGroup : propertySalt;
    uint state = Hash(seed ^ ((stream + 1u) * 2246822519u));
    return (float)(state & 0x00ffffffu) / 16777216.0f;
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
    const bool localSimulation = (asuint(LoadEmitterValue(emitterIndex,14u).w)&1u)!=0u;
    float4 worldScale = localSimulation ? LoadEmitterValue(emitterIndex,2u) : particle.birthScale;
    float4 worldRotation = localSimulation ? LoadEmitterValue(emitterIndex,1u) : particle.birthRotation;
    particle.positionAge.xyz = localSimulation ? LoadEmitterValue(emitterIndex,0u).xyz +
        RotateByQuaternion((particle.physicsPositionSeed.xyz+localPosition)*worldScale.xyz,worldRotation) :
        particle.physicsPositionSeed.xyz + RotateByQuaternion(localPosition*worldScale.xyz,worldRotation);
    particle.scaleRotation = float4(
        abs(localScale.xy * worldScale.xy) *
            max(LoadEmitterValue(emitterIndex, 10u).y, 0.0f),
        localRotation.z,
        abs(localScale.z * worldScale.z) * max(LoadEmitterValue(emitterIndex, 10u).y, 0.0f));
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

#endif
