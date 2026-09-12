#ifndef HIKARI_SURFACE_TANGENT_FRAME_INCLUDED
#define HIKARI_SURFACE_TANGENT_FRAME_INCLUDED

float4 HikariTransformSurfaceTangent(float4 tangent, float3x3 world)
{
    // 接線は面上の方向ベクトルなので world で変換する。
    // 法線専用の逆転置行列を使うと非一様スケールで UV の方向が歪む。
    const float3 transformed = mul(world, tangent.xyz);
    const float lengthSquared = dot(transformed, transformed);
    const float orientation = determinant(world);
    if (!isfinite(lengthSquared) || lengthSquared <= 1.0e-12f ||
        !isfinite(orientation) || !isfinite(tangent.w))
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    const float handedness = (tangent.w < 0.0f ? -1.0f : 1.0f) *
        (orientation < 0.0f ? -1.0f : 1.0f);
    return float4(transformed * rsqrt(lengthSquared), handedness);
}

float3 HikariTransformTangentNormalToWorld(
    float3 normalTS,
    float3 normalWS,
    float4 tangentWS)
{
    const float normalLengthSquared = dot(normalWS, normalWS);
    const float3 n = isfinite(normalLengthSquared) && normalLengthSquared > 1.0e-12f
        ? normalWS * rsqrt(normalLengthSquared) : float3(0.0f, 1.0f, 0.0f);
    float3 t = tangentWS.xyz;
    float tangentLengthSquared = dot(t, t);
    if (!isfinite(tangentLengthSquared) || tangentLengthSquared <= 1.0e-12f)
    {
        return n;
    }
    t *= rsqrt(tangentLengthSquared);
    t -= n * dot(n, t);
    tangentLengthSquared = dot(t, t);
    if (!isfinite(tangentLengthSquared) || tangentLengthSquared <= 1.0e-10f)
    {
        return n;
    }
    t *= rsqrt(tangentLengthSquared);
    const float3 b = cross(n, t) * (tangentWS.w < 0.0f ? -1.0f : 1.0f);
    const float3 transformed = normalTS.x * t + normalTS.y * b + normalTS.z * n;
    const float transformedLengthSquared = dot(transformed, transformed);
    return isfinite(transformedLengthSquared) && transformedLengthSquared > 1.0e-12f
        ? transformed * rsqrt(transformedLengthSquared) : n;
}

#endif
