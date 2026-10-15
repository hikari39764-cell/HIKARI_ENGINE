#ifndef HIKARI_SKY_VIEW_RAY_HLSLI
#define HIKARI_SKY_VIEW_RAY_HLSLI

// 画面の NDC から空を見る世界の向き（近平面から遠平面へ）。行列が退化した時は +Z。
float3 HikariSkyViewRay(float4x4 inverseViewProjection, float2 ndc)
{
    const float4 worldNear = mul(inverseViewProjection, float4(ndc, 0.0f, 1.0f));
    const float4 worldFar = mul(inverseViewProjection, float4(ndc, 1.0f, 1.0f));
    if (abs(worldNear.w) <= 1.0e-6f || abs(worldFar.w) <= 1.0e-6f)
    {
        return float3(0.0f, 0.0f, 1.0f);
    }
    const float3 direction = worldFar.xyz / worldFar.w - worldNear.xyz / worldNear.w;
    const float lengthSquared = dot(direction, direction);
    if (!all(isfinite(direction)) || lengthSquared <= 1.0e-8f)
    {
        return float3(0.0f, 0.0f, 1.0f);
    }
    return direction * rsqrt(lengthSquared);
}

#endif
