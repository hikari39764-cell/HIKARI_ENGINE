#ifndef HIKARI_ATMOSPHERE_AERIAL_PERSPECTIVE_CS_INCLUDED
#define HIKARI_ATMOSPHERE_AERIAL_PERSPECTIVE_CS_INCLUDED

#define HIKARI_ATMOSPHERE_DECLARE_LUT_CB
#define HIKARI_ATMOSPHERE_DECLARE_VIEW_CB

// 視点ごとの空気遠近の体積 LUT（Hillaire 2020）。画面の 32x32 の方向ごとに、カメラから各奥行きまでの
// 大気の内散乱光（このビューの Pre-Exposure 済み）と平均の透過率を 32 段の奥行きへ書く。
// 奥行きは d = maxDistance * w^2（w = (slice + 1) / 32）で、近くを細かく取る。
// 雲があると、雲の陰の境より下の空気は主光源の一部しか受けず、残りは雲の下の空の光（等方とみなす）で照らされる。
// 境より上（雲の上の視点から境へ下りるまで）の空気は晴れた空として主光源をそのまま受ける。

Texture2D<float4> gAtmosphereTransmittance : register(t0);
Texture2D<float4> gAtmosphereMultiScattering : register(t1);
RWTexture3D<float4> gAerialPerspective : register(u0);
SamplerState gLinearClamp : register(s0);

#define HIKARI_ATMOSPHERE_USE_TRANSMITTANCE_LUT
#define HIKARI_ATMOSPHERE_USE_MULTISCATTERING_LUT
#include "Include/Atmosphere/HIKARI_AtmosphereScattering.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width = 0u;
    uint height = 0u;
    uint depth = 0u;
    gAerialPerspective.GetDimensions(width, height, depth);
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }
    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(width, height);
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    const float4 farPoint = mul(gAtmosphereViewInverseViewProj, float4(ndc, 1.0f, 1.0f));
    const float4 nearPoint = mul(gAtmosphereViewInverseViewProj, float4(ndc, 0.0f, 1.0f));
    const float3 direction = HikariAtmosphereSafeNormalize(
        farPoint.xyz / max(abs(farPoint.w), 1.0e-6f) - nearPoint.xyz / max(abs(nearPoint.w), 1.0e-6f));
    const float3 origin = HikariAtmosphereViewObserver();
    const float3 sunDirection = HikariAtmosphereSafeNormalize(gAtmosphereSunDirection.xyz);
    const float3 sunRadiance = max(gAtmosphereSunColorRadiance.rgb, 0.0f.xxx) *
        max(0.0f, HikariAtmosphereFiniteOr(gAtmosphereSunColorRadiance.a, 0.0f)) *
        max(0.0f, HikariAtmosphereFiniteOr(gAtmosphereOzoneParams.y, 1.0f));
    const float maxDistance = max(gAtmosphereViewParams.x, 1.0f);
    const float visibility = saturate(gAtmosphereViewCloudShading.w);
    // 境より手前の晴れた空気（雲の下にいる時は長さ 0）。どの奥行きの層も同じ区間なので一度だけ積分する。
    const float shadeDistance = visibility < 1.0f ? HikariAtmosphereCloudShadeDistance(origin, direction) : 0.0f;
    HikariAtmosphereViewIntegration clear = (HikariAtmosphereViewIntegration)0;
    clear.transmittance = 1.0f.xxx;
    if (shadeDistance > 0.0f && shadeDistance < maxDistance)
    {
        clear = HikariAtmosphereIntegrateSegment(origin, direction, sunDirection, 12u, 4u, shadeDistance, false);
    }
    [loop]
    for (uint slice = 0u; slice < depth; ++slice)
    {
        const float w = float(slice + 1u) / float(depth);
        const float distance = maxDistance * w * w;
        const HikariAtmosphereViewIntegration integration = HikariAtmosphereIntegrateSegment(
            origin, direction, sunDirection, 4u + slice / 4u, 4u, distance, false);
        // 層までの全体が境より手前なら晴れた空気だけ。
        const bool allClear = shadeDistance >= distance;
        const float3 clearScattering = allClear ? integration.scattering : clear.scattering;
        const float3 clearTransmittance = allClear ? integration.transmittance : clear.transmittance;
        const float3 scattering = ((clearScattering + max(integration.scattering - clearScattering, 0.0f.xxx) *
            visibility) * sunRadiance + max(gAtmosphereViewCloudShading.rgb, 0.0f.xxx) *
            max(clearTransmittance - integration.transmittance, 0.0f.xxx) * (1.0f - visibility)) *
            max(gAtmosphereViewCamera.w, 0.0f);
        const float transmittance = dot(integration.transmittance, 1.0f / 3.0f);
        gAerialPerspective[uint3(dispatchThreadId.xy, slice)] = all(isfinite(scattering))
            ? float4(max(scattering, 0.0f.xxx), saturate(transmittance))
            : float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
}

#endif
