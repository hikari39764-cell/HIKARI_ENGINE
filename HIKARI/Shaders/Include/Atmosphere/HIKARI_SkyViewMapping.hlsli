#ifndef HIKARI_SKY_VIEW_MAPPING_INCLUDED
#define HIKARI_SKY_VIEW_MAPPING_INCLUDED

// 空の背景（天空 LUT）の地面を見下ろす最低の高さ [m]。LUT の地平線より下の地面（環境光の下からの照り返しと、
// 背景を惑星の地表にした時の表示）は場面の外に広がる遠い地表の代わりで、実際の地表は起伏があって数 km 先まで
// 見渡せる（平らな地面を数 m の高さから見ると、地平線は剃刀のような線で地面は足元の一様な色になる）。カメラが
// 低くてもこの高さから見下ろした距離で地面に空気遠近を掛け、地平線の霞から地面の色へ移す。
static const float HIKARI_ATMOSPHERE_BACKGROUND_HEIGHT = 200.0f;

// 天空 LUT の座標。u は方位 atan2(z, x) / 2π、v は水平面からの仰角 l を v = 0.5 + 0.5 sign(l) sqrt(|l| / (π/2))
// で写し、色が急に変わる地平線付近に texel を集める（Hillaire 2020）。v = 0.5 が地平線で texel の境目に来る。
float2 HikariAtmosphereSkyViewUv(float3 direction)
{
    direction = normalize(direction);
    const float latitude = asin(clamp(direction.y, -1.0f, 1.0f));
    return float2(
        frac(atan2(direction.z, direction.x) * 0.15915494309f),
        0.5f + 0.5f * sign(latitude) * sqrt(abs(latitude) * 0.636619772f));
}

float3 HikariAtmosphereSkyViewDirection(float2 uv)
{
    const float signedRoot = uv.y * 2.0f - 1.0f;
    const float latitude = signedRoot * abs(signedRoot) * 1.57079633f;
    const float azimuth = uv.x * 6.28318530718f;
    const float horizontal = cos(latitude);
    return float3(cos(azimuth) * horizontal, sin(latitude), sin(azimuth) * horizontal);
}

#endif
