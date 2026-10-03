#ifndef HIKARI_BILLBOARD_MATH_INCLUDED
#define HIKARI_BILLBOARD_MATH_INCLUDED

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

float2 HikariMapPackedSpriteUv(float2 sourceUv, bool rotatedClockwise, bool flipX)
{
    if (flipX)
    {
        sourceUv.x = 1.0f - sourceUv.x;
    }
    return rotatedClockwise ? float2(1.0f - sourceUv.y, sourceUv.x) : sourceUv;
}

float2 HikariUnmapPackedSpriteUv(float2 packedUv, bool rotatedClockwise, bool flipX)
{
    float2 sourceUv = rotatedClockwise ? float2(packedUv.y, 1.0f - packedUv.x) : packedUv;
    if (flipX)
    {
        sourceUv.x = 1.0f - sourceUv.x;
    }
    return sourceUv;
}

float3 HikariResolveWorldSpriteNormal(
    float3 tangentNormal, float3 tangent, float3 geometricNormal, bool frontFace, bool flipX)
{
    if (flipX)
    {
        tangentNormal.x = -tangentNormal.x;
    }
    const float3 normal = NormalizeOr(geometricNormal, float3(0.0f, 0.0f, 1.0f));
    float3 surfaceTangent = tangent - normal * dot(normal, tangent);
    surfaceTangent = NormalizeOr(surfaceTangent, float3(1.0f, 0.0f, 0.0f));
    const float3 surfaceBitangent = NormalizeOr(cross(normal, surfaceTangent), float3(0.0f, 1.0f, 0.0f));
    const float3 mappedNormal = tangentNormal.x * surfaceTangent + tangentNormal.y * surfaceBitangent +
        tangentNormal.z * normal;
    const float3 resolvedNormal = NormalizeOr(mappedNormal, normal);
    return frontFace ? resolvedNormal : -resolvedNormal;
}

void HikariResolveBillboardCameraBasis(float4x4 inverseViewProjection, out float3 right, out float3 up, out float3 forward)
{
    float4 centerH = mul(
        inverseViewProjection,
        float4(0.0f, 0.0f, 0.0f, 1.0f));
    float4 rightH = mul(
        inverseViewProjection,
        float4(1.0f, 0.0f, 0.0f, 1.0f));
    float4 upH = mul(
        inverseViewProjection,
        float4(0.0f, 1.0f, 0.0f, 1.0f));
    centerH.xyz /= max(abs(centerH.w), 1.0e-6f);
    rightH.xyz /= max(abs(rightH.w), 1.0e-6f);
    upH.xyz /= max(abs(upH.w), 1.0e-6f);
    right = NormalizeOr(
        rightH.xyz - centerH.xyz,
        float3(1.0f, 0.0f, 0.0f));
    up = NormalizeOr(
        upH.xyz - centerH.xyz,
        float3(0.0f, 1.0f, 0.0f));
    forward = NormalizeOr(
        cross(right, up),
        float3(0.0f, 0.0f, 1.0f));
}

#endif
