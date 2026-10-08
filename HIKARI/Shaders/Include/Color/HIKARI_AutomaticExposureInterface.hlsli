#ifndef HIKARI_AUTOMATIC_EXPOSURE_INTERFACE_HLSLI
#define HIKARI_AUTOMATIC_EXPOSURE_INTERFACE_HLSLI

#define HIKARI_AUTOMATIC_EXPOSURE_THREAD_COUNT 256

cbuffer MeteringConstants : register(b0)
{
    uint gWidth;
    uint gHeight;
    // 1 / P。Pre-Exposure 済みの値を絶対シーン輝度へ戻す。
    float gInverseMeteringPreExposure;
    uint gReserved;
};

Texture2D<float4> gSceneColor : register(t0);
// x: 百分位区間の対数平均輝度 [nits]、y: 区間の重み、z: 全体の重み、w: 明部（上位 5%）の輝度 [nits]。
RWStructuredBuffer<float4> gMeasurement : register(u0);

#endif
