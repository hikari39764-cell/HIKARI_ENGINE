#ifndef HIKARI_VOLUMETRIC_SCATTERING_INCLUDED
#define HIKARI_VOLUMETRIC_SCATTERING_INCLUDED

// 区間内で一定の消散係数を積分する。薄い媒体では exp の桁落ちを避ける。
float3 HikariExtinctionIntegral(float3 extinction, float distance)
{
    const float length = max(distance, 0.0f);
    const float3 tau = max(extinction, 0.0f.xxx) * length;
    const float3 series = 1.0f - tau * 0.5f + tau * tau / 6.0f;
    const float3 quotient = (1.0f - exp(-tau)) / max(tau, 1e-30f);
    return length * float3(tau.x < 0.001f ? series.x : quotient.x,
        tau.y < 0.001f ? series.y : quotient.y, tau.z < 0.001f ? series.z : quotient.z);
}

// viewRay は camera -> sample、directionToLight は sample -> light。
// 両者が同じ向きの時に前方散乱となる。位相関数の球面積分は 1。
float HikariHenyeyGreenstein(float cosTheta, float anisotropy)
{
    const float g = clamp(anisotropy, -0.8f, 0.8f);
    const float g2 = g*g;
    const float denominator = max(1.0f+g2-2.0f*g*clamp(cosTheta,-1.0f,1.0f),1e-4f);
    return (1.0f-g2)/(12.56637061436f*denominator*sqrt(denominator));
}

float3 HikariSingleScatteringSource(float3 albedo, float3 ambientRadiance,
    float3 solarIrradiance, float solarTransmittance, float viewToSun, float anisotropy)
{
    // uniform ambient の位相畳み込みは radiance のまま。太陽入力は照度。
    return saturate(albedo)*(max(ambientRadiance,0.0f.xxx)+max(solarIrradiance,0.0f.xxx)*
        saturate(solarTransmittance)*HikariHenyeyGreenstein(viewToSun,anisotropy));
}

#endif
