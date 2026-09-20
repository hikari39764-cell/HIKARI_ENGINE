#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
Texture2D<float4> gLayer : register(t0);
Texture2D<float4> gMatte : register(t1);
SamplerState gSampler : register(s0);

// 幾何レイヤー間の遮罩だけを処理する。調整効果は各材質のローカル UV で評価済み。
float4 main(HikariPostEffectInput input) : SV_TARGET {
    float4 value = gLayer.SampleLevel(gSampler,input.uv,0);
    float4 matte = gMatte.SampleLevel(gSampler,input.uv,0);
    float coverage = saturate(gUser[0].x > 0.5 ? dot(matte.rgb,float3(0.2126,0.7152,0.0722)) : matte.a);
    if (gUser[0].y > 0.5) coverage = 1-coverage;
    return value * coverage;
}
