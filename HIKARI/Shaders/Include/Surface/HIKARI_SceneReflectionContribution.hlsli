#ifndef HIKARI_SCENE_REFLECTION_CONTRIBUTION_HLSLI
#define HIKARI_SCENE_REFLECTION_CONTRIBUTION_HLSLI
#include "../Math/HIKARI_FiniteMath.hlsli"

// a=1 は同じ Forward 表面の有効な環境鏡面分量。未対応・未描画は全成分ゼロ。
bool HikariHasReflectionContribution(float4 fallback, float4 response)
{
    return HikariIsFiniteFloat4(fallback) && HikariIsFiniteFloat4(response) &&
        fallback.a == 1.0f && response.a == 1.0f &&
        all(fallback.rgb >= 0.0f) && all(fallback.rgb < 65504.0f) &&
        all(response.rgb >= 0.0f) && all(response.rgb < 65504.0f);
}

void HikariAttenuateReflectionContribution(inout float4 fallback, inout float4 response,
    float transmission, bool supported)
{
    supported = supported && HikariHasReflectionContribution(fallback, response) &&
        HikariIsFiniteFloat(transmission) && transmission >= 0.0f && transmission <= 1.0f;
    fallback = supported ? float4(fallback.rgb * transmission, 1) : 0.0f.xxxx;
    response = supported ? float4(response.rgb * transmission, 1) : 0.0f.xxxx;
}
#endif
