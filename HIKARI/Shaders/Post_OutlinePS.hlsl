// 輪郭線。表示の色の段階で深度の段差と折れ目（と、感度を上げれば明るさの境）に線を引く。線の色は固定の色と、下の面の
// 色を暗くした色を混ぜる（アニメ調の色トレス）。
#include "Include/Post/HIKARI_PostStylize.hlsli"

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    const float3 source = gSceneTex.Sample(gSamp, input.uv).rgb;
    const float strength = saturate(gUser[0].x * gIntensity);
    if (strength <= 0.0001f)
        return float4(source, 1.0f);

    const float lineRadius = max(gUser[0].y * HikariStylizePixelScale(), 0.5f);
    // 幾何の線は PreTemporal、表示の明暗境界だけをこの段階で描く。
    const float lines = HikariStylizeToneLines(input.uv, lineRadius, gUser[0].w);
    const float3 traced = source * saturate(gUser[1].y);
    const float3 lineColor = lerp(saturate(gUser[2].rgb), traced, saturate(gUser[1].x));
    return float4(lerp(source, lineColor, lines * strength), 1.0f);
}
