#ifndef HIKARI_SKY_RADIANCE_INCLUDED
#define HIKARI_SKY_RADIANCE_INCLUDED

float3 HikariRotateSkyYaw(float3 direction, float yaw)
{
    float sine, cosine;
    sincos(yaw, sine, cosine);
    return float3(direction.x * cosine - direction.z * sine, direction.y,
        direction.x * sine + direction.z * cosine);
}

// 背景、反射、光輸送は同じ authored horizon を通る連続な放射輝度を使う。
float3 HikariEvaluateGradientSky(float3 direction, float3 zenith, float3 horizon,
    float3 ground, float horizonPower)
{
    const float elevation = saturate(abs(direction.y));
    const float3 target = direction.y >= 0.0f ? zenith : ground;
    const float3 color = lerp(horizon, target, smoothstep(0.0f, 1.0f, elevation));
    const float horizonBand = pow(saturate(1.0f - elevation), max(0.01f, horizonPower));
    return lerp(color, horizon, horizonBand * 0.12f);
}

float3 HikariApplySkyRadianceScale(float3 radiance, float3 tint, float exposure)
{
    return max(0.0f.xxx, radiance) * max(0.0f.xxx, tint) * max(0.0f, exposure);
}

// 既存の ambientFromSky の blend / intensity 契約を全経路で保持する。
float3 HikariBlendAmbientWithSky(float3 ambient, float3 sky, float strength)
{
    return lerp(ambient, sky * max(0.0f, strength), saturate(strength));
}

#endif
