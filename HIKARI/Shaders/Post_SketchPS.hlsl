// 鉛筆の素描。表示の色の段階で明るさを斜線の重なり（明るい所は無し、暗くなるほど一方向・交差・三方向）に置き換え、
// 深度の輪郭と折れ目、明るさの境を揺らいだ鉛筆の線で描く。紙の地に元の色を淡く残せる。
#include "Include/Post/HIKARI_PostStylize.hlsli"
#include "Include/Color/HIKARI_Luminance.hlsli"

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    const float3 source = gSceneTex.Sample(gSamp, input.uv).rgb;
    const float strength = saturate(gUser[0].x * gIntensity);
    if (strength <= 0.0001f)
        return float4(source, 1.0f);

    const float scale = HikariStylizePixelScale();
    const float lineRadius = max(gUser[0].y * scale, 0.5f);
    const float whitePoint = gUser[1].w;
    const float blackPoint = gUser[2].w;
    const float hatchPixels = max(gUser[1].x * scale, 2.0f);
    const float wobble = saturate(gUser[1].y);
    const float colorAmount = saturate(gUser[1].z);
    const float3 pencil = saturate(gUser[2].rgb);
    const float3 paper = saturate(gUser[3].rgb);

    // 線の揺らぎ：数十画素ごとに変わる低周波の値で採取点をずらす。
    const float2 cellNoise = float2(HikariHash12(floor(input.position.xy / (24.0f * scale))),
                                    HikariHash12(floor(input.position.xy / (24.0f * scale)) + 7.0f)) - 0.5f;
    const float2 uv = input.uv + cellNoise * wobble * lineRadius * HikariStylizeTexel();

    const float luma = HikariStylizeTone(gSceneTex.Sample(gSamp, uv).rgb, blackPoint, whitePoint);
    const float2 pixel = input.position.xy;
    const float2 grid = HikariStylizeRotatedGrid(pixel, hatchPixels, 35.0f);
    const float2 steepGrid = HikariStylizeRotatedGrid(pixel, hatchPixels * 0.8f, -55.0f);
    const float pencilGrain = 0.75f + 0.25f * HikariHash12(floor(pixel * 0.5f));
    const float hatchA = HikariStylizeLines(grid.x, 0.35f) * smoothstep(0.75f, 0.6f, luma);
    const float hatchB = HikariStylizeLines(steepGrid.x, 0.35f) * smoothstep(0.5f, 0.35f, luma);
    const float hatchC = HikariStylizeLines(grid.y, 0.4f) * smoothstep(0.25f, 0.1f, luma);
    float graphite = max(max(hatchA, hatchB), hatchC) * pencilGrain * 0.8f;

    graphite = max(graphite, HikariStylizeInkLines(uv, lineRadius, gUser[0].z, gUser[0].w) * pencilGrain);

    const float3 ground = lerp(paper, paper * saturate(source / max(HikariLuminance(source), 0.05f)), colorAmount);
    const float3 sketch = lerp(saturate(ground), pencil, saturate(graphite));
    return float4(lerp(source, sketch, strength), 1.0f);
}
