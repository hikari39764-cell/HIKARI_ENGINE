#ifndef HIKARI_ATMOSPHERE_SKY_VIEW_CS_INCLUDED
#define HIKARI_ATMOSPHERE_SKY_VIEW_CS_INCLUDED

#define HIKARI_ATMOSPHERE_DECLARE_LUT_CB
#define HIKARI_ATMOSPHERE_DECLARE_VIEW_CB


Texture2D<float4> gAtmosphereTransmittance : register(t0);
Texture2D<float4> gAtmosphereMultiScattering : register(t1);
RWTexture2D<float4> gAtmosphereSkyView : register(u0);
SamplerState gLinearClamp : register(s0);

#define HIKARI_ATMOSPHERE_USE_TRANSMITTANCE_LUT
#define HIKARI_ATMOSPHERE_USE_MULTISCATTERING_LUT
#include "Include/Atmosphere/HIKARI_AtmosphereScattering.hlsli"

// 地表の天空照度。グループの 64 スレッドが余弦分布の 64 方向の空の輝度を一つずつ求め、平均の π 倍を照度にする。
groupshared float3 gSkyIrradianceSamples[64];

float3 GroundSkyIrradiance(float3 observer, uint sampleIndex, float aerialPerspectiveStrength)
{
    const float u = (float(sampleIndex) + 0.5f) / 64.0f;
    const float v = float(reversebits(sampleIndex)) * 2.3283064365386963e-10f;
    const float cosine = sqrt(1.0f - v);
    const float sine = sqrt(v);
    const float azimuth = u * 6.28318530718f;
    gSkyIrradianceSamples[sampleIndex] = HikariAtmosphereEvaluateSkyRadianceWithSteps(observer,
        float3(cos(azimuth) * sine, cosine, sin(azimuth) * sine), gAtmosphereGroundColor.rgb,
        gAtmosphereOzoneParams.w, aerialPerspectiveStrength, 12u, 4u, 0.0f.xxx);
    GroupMemoryBarrierWithGroupSync();
    float3 sum = 0.0f.xxx;
    [loop] for (uint index = 0u; index < 64u; ++index)
    {
        sum += gSkyIrradianceSamples[index];
    }
    return sum * (3.14159265f / 64.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    // 観測者はカメラの高さ（低い時は遠い地表を見渡す高さ）に置く。高い視点ほど地面までの視線が長く、遠くの地面が
    // 大気で白む。
    const float3 observer = HikariAtmosphereBackgroundObserver();
    const float aerialPerspectiveStrength = max(
        0.0f,
        HikariAtmosphereFiniteOr(gAtmosphereOzoneParams.y, 1.0f));
    // 範囲外のスレッドも同期に加わる。
    const float3 skyIrradiance = GroundSkyIrradiance(observer, groupIndex, aerialPerspectiveStrength);

    uint width = 0u;
    uint height = 0u;
    gAtmosphereSkyView.GetDimensions(width, height);
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) /
        float2(max(1u, width), max(1u, height));
    const float3 viewDirection = HikariAtmosphereSkyViewDirection(uv);
    float3 radiance = HikariAtmosphereEvaluateSkyRadianceWithSteps(
        observer,
        viewDirection,
        gAtmosphereGroundColor.rgb,
        gAtmosphereOzoneParams.w,
        aerialPerspectiveStrength,
        HIKARI_ATMOSPHERE_MAX_SKY_STEPS,
        HIKARI_ATMOSPHERE_MAX_SUN_STEPS,
        skyIrradiance);
    // 水平より下（地面と、背景の観測者から見える地平線の先の空気）のうち、雲の陰の境より下は雲の下にある。主光源は
    // 雲を抜けた割合だけ届き、空気と地面は雲の下の空の光でも照らされる。空の照度と主光源の項は線形なので、照度 0 と
    // 1 で評価し直して分ける（地面に当たらない視線は地面の項が 0 で、透過率 0 の長い空気の道として扱う）。観測者が
    // 雲の上にいる時、視線が境へ下りるまでの空気は晴れた空として主光源をそのまま受ける（雲海の上から見る地平線が
    // 灰色に沈まない）。水平より上の空は雲の向こうの晴天のまま（雲が重ねて描かれる。雲は実際のカメラの高さから
    // 描くので、背景の観測者の高さで水平より下に見える晴天は雲の外に出てしまう）。
    const float visibility = saturate(gAtmosphereViewCloudShading.w);
    const float shadeDistance = HikariAtmosphereCloudShadeDistance(observer, viewDirection);
    if (visibility < 1.0f && viewDirection.y < 0.0f && shadeDistance < HIKARI_ATMOSPHERE_NO_CLOUD_SHADE)
    {
        const float3 sunPart = HikariAtmosphereEvaluateSkyRadianceWithSteps(observer, viewDirection,
            gAtmosphereGroundColor.rgb, gAtmosphereOzoneParams.w, aerialPerspectiveStrength,
            HIKARI_ATMOSPHERE_MAX_SKY_STEPS, HIKARI_ATMOSPHERE_MAX_SUN_STEPS, 0.0f.xxx);
        // 地面の照度 1 あたりの輝度（アルベド × 地面までの透過率 / π）。
        const float3 groundPerIrradiance = HikariAtmosphereEvaluateSkyRadianceWithSteps(observer, viewDirection,
            gAtmosphereGroundColor.rgb, gAtmosphereOzoneParams.w, aerialPerspectiveStrength,
            HIKARI_ATMOSPHERE_MAX_SKY_STEPS, HIKARI_ATMOSPHERE_MAX_SUN_STEPS, 1.0f.xxx) - sunPart;
        const float3 transmittance = saturate(groundPerIrradiance * 3.14159265f /
            max(gAtmosphereGroundColor.rgb, 1.0e-4f.xxx));
        const float3 underCloud = max(gAtmosphereViewCloudShading.rgb, 0.0f.xxx);
        // 境より手前の晴れた空気の散乱光と透過率（雲の下にいる時は 0 と 1）。
        float3 clearScattering = 0.0f.xxx;
        float3 clearTransmittance = 1.0f.xxx;
        if (shadeDistance > 0.0f)
        {
            const HikariAtmosphereViewIntegration clear = HikariAtmosphereIntegrateSegment(observer, viewDirection,
                HikariAtmosphereSafeNormalize(gAtmosphereSunDirection.xyz), HIKARI_ATMOSPHERE_MAX_SKY_STEPS,
                HIKARI_ATMOSPHERE_MAX_SUN_STEPS, shadeDistance, false);
            clearScattering = clear.scattering * max(gAtmosphereSunColorRadiance.rgb, 0.0f.xxx) *
                max(0.0f, HikariAtmosphereFiniteOr(gAtmosphereSunColorRadiance.a, 0.0f)) * aerialPerspectiveStrength;
            clearTransmittance = clear.transmittance;
        }
        radiance = clearScattering + max(sunPart - clearScattering, 0.0f.xxx) * visibility +
            groundPerIrradiance * lerp(underCloud * 3.14159265f, skyIrradiance, visibility) +
            underCloud * max(clearTransmittance - transmittance, 0.0f.xxx) * (1.0f - visibility);
    }
    gAtmosphereSkyView[dispatchThreadId.xy] =
        float4(
            all(isfinite(radiance)) ? max(0.0f.xxx, radiance) : 0.0f.xxx,
            1.0f);
}

#endif
