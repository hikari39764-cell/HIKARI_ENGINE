#ifndef HIKARI_SKY_RADIANCE_INCLUDED
#define HIKARI_SKY_RADIANCE_INCLUDED

float3 HikariRotateSkyYaw(float3 direction, float yaw)
{
    float sine, cosine;
    sincos(yaw, sine, cosine);
    return float3(direction.x * cosine - direction.z * sine, direction.y,
        direction.x * sine + direction.z * cosine);
}

// 空の画素値を輝度へ写す（tint × 倍率）。
float3 HikariApplySkyRadianceScale(float3 radiance, float3 tint, float exposure)
{
    return max(0.0f.xxx, radiance) * max(0.0f.xxx, tint) * max(0.0f, exposure);
}

#endif
