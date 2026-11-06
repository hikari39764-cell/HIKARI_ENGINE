#ifndef HIKARI_FLUID_CAMERA_VOLUME
#define HIKARI_FLUID_CAMERA_VOLUME
// 透明物体のための流体の相機体積：画面の uv と相機からの距離までに流体を通った分（rgb 散乱、a 透過率）。
// 奥行きの層は near から far まで対数に並び、texel k の中心が距離 near * exp((k + 0.5) / slices * logRange)。
// 不透明面には步进の層が流体を合成済みなので、透明物体は自分の色を src * T + S * alpha（加算合成は src * T）にする。
// params：x near [m]、y log(far / near)、z 層の数、w 1 で有効。
float4 HikariFluidCameraVolumeAt(Texture3D<float4> volume, SamplerState linearClamp, float2 uv, float distance,
    float4 params)
{
    if (params.w < 0.5f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    const float slice = log(max(distance, params.x) / params.x) / max(params.y, 1.0e-4f);
    return volume.SampleLevel(linearClamp, float3(uv, saturate(slice)), 0);
}
#endif
