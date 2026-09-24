#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
#include "Include/Post/HIKARI_BloomBlur.hlsli"

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    return HikariBloomBlur(input.uv, float2(gUser[1].x, 0.0f));
}
