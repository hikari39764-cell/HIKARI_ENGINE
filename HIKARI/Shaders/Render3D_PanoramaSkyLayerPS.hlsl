cbuffer PanoramaLayerCB : register(b0)
{
    float4x4 gPanoramaInverseViewProj;
    float4 gPanoramaYawOffsetOpacity; // x yaw, yz UV offset, w opacity
    float4 gPanoramaTintExposure;     // rgb tint, w scene-linear multiplier × pre-exposure
};

Texture2D gPanoramaCloudLayer : register(t0);
SamplerState gPanoramaSampler : register(s0);

#include "Include/Sky/HIKARI_EquirectangularSampling.hlsli"
#include "Include/Sky/HIKARI_SkyViewRay.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

float3 RotateYaw(float3 direction, float yaw)
{
    float sineYaw = sin(yaw);
    float cosineYaw = cos(yaw);
    return float3(
        direction.x * cosineYaw - direction.z * sineYaw,
        direction.y,
        direction.x * sineYaw + direction.z * cosineYaw);
}

float4 main(PSInput input) : SV_TARGET
{
    const float3 worldDirection = HikariSkyViewRay(gPanoramaInverseViewProj, input.ndc);
    const float3 sampleDirection = normalize(RotateYaw(
        worldDirection,
        gPanoramaYawOffsetOpacity.x));
    float2 uv = HikariDirectionToEquirectangularUv(sampleDirection);
    uv.x = frac(uv.x + gPanoramaYawOffsetOpacity.y);
    uv.y = saturate(uv.y + gPanoramaYawOffsetOpacity.z);

    float2 uvDerivativeX;
    float2 uvDerivativeY;
    HikariResolveEquirectangularGradients(
        sampleDirection,
        uv,
        uvDerivativeX,
        uvDerivativeY);
    float4 layer = gPanoramaCloudLayer.SampleGrad(
        gPanoramaSampler,
        uv,
        uvDerivativeX,
        uvDerivativeY);
    float alpha = saturate(layer.a * gPanoramaYawOffsetOpacity.w);
    float3 color = max(layer.rgb, 0.0f.xxx) *
        max(gPanoramaTintExposure.rgb, 0.0f.xxx) *
        max(gPanoramaTintExposure.w, 0.0f);
    if (!all(isfinite(color)) || !isfinite(alpha))
    {
        return 0.0f.xxxx;
    }
    // Premultiplied output matches the stage blend contract and avoids dark
    // fringes around transparent PNG texels.
    return float4(color * alpha, alpha);
}
