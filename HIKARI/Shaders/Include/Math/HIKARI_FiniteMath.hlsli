#ifndef HIKARI_FINITE_MATH_INCLUDED
#define HIKARI_FINITE_MATH_INCLUDED

// GPU 由来の非有限値を feature shader ごとに個別実装しないための共通契約。
// 比較上限は IEEE 754 float の最大値より少し小さくし、Inf も確実に拒否する。
static const float HIKARI_FINITE_FLOAT_LIMIT = 3.4028230e+38f;

bool HikariIsFiniteFloat(float value)
{
    return value == value && abs(value) < HIKARI_FINITE_FLOAT_LIMIT;
}

bool HikariIsFiniteFloat2(float2 value)
{
    return all(value == value) &&
        all(abs(value) < HIKARI_FINITE_FLOAT_LIMIT.xx);
}

bool HikariIsFiniteFloat3(float3 value)
{
    return all(value == value) &&
        all(abs(value) < HIKARI_FINITE_FLOAT_LIMIT.xxx);
}

bool HikariIsFiniteFloat4(float4 value)
{
    return all(value == value) &&
        all(abs(value) < HIKARI_FINITE_FLOAT_LIMIT.xxxx);
}

float HikariFiniteOr(float value, float fallbackValue)
{
    return HikariIsFiniteFloat(value) ? value : fallbackValue;
}

float2 HikariFiniteOr(float2 value, float2 fallbackValue)
{
    return HikariIsFiniteFloat2(value) ? value : fallbackValue;
}

float3 HikariFiniteOr(float3 value, float3 fallbackValue)
{
    return HikariIsFiniteFloat3(value) ? value : fallbackValue;
}

float4 HikariFiniteOr(float4 value, float4 fallbackValue)
{
    return HikariIsFiniteFloat4(value) ? value : fallbackValue;
}

float3 HikariSafeNormalize(float3 value, float3 fallbackValue)
{
    const float lengthSquared = dot(value, value);
    if (HikariIsFiniteFloat(lengthSquared) && lengthSquared > 1.0e-12f)
    {
        return value * rsqrt(lengthSquared);
    }

    const float fallbackLengthSquared = dot(fallbackValue, fallbackValue);
    if (HikariIsFiniteFloat(fallbackLengthSquared) &&
        fallbackLengthSquared > 1.0e-12f)
    {
        return fallbackValue * rsqrt(fallbackLengthSquared);
    }
    return float3(0.0f, 1.0f, 0.0f);
}

#endif
