#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
#include "Include/Color/HIKARI_Luminance.hlsli"

// Bloom のピラミッドの一段を作る 13 点の縮小（Jimenez 2014）。双線形の 13 回の取得で 4x4 texel を重なり付きで
// 平均し、段を重ねても格子や角張った光の輪にならない。
// 先頭の段だけが閾値と上限を掛け、明るさで重みを下げる Karis 平均で明るい 1 点が点滅する光の輪になるのを防ぐ。
// gUser[0]: x 閾値（表示の明るさ、0 で全体）、y 先頭の段で掛ける明るさ、z 1 なら先頭の段
// gUser[1]: xy 入力の texel、z 表示から場面色（Pre-Exposure 済み）へ戻す P/E
Texture2D gSourceTex : register(t0);
SamplerState gSamp : register(s0);

// 表示の明るさでこれを超える値（太陽の円盤など）は止める。
static const float kMaximumDisplayRadiance = 4096.0f;

float3 SampleSource(float2 uv)
{
    const float3 color = gSourceTex.SampleLevel(gSamp, uv, 0.0f).rgb;
    return all(isfinite(color)) ? max(color, 0.0f.xxx) : 0.0f.xxx;
}

float3 Prefilter(float3 color, float displayToScene)
{
    const float peak = max(max(color.r, color.g), color.b);
    const float limit = kMaximumDisplayRadiance * displayToScene;
    if (peak > limit)
    {
        color *= limit / peak;
    }
    const float threshold = max(gUser[0].x, 0.0f) * displayToScene;
    if (threshold <= 0.0f)
    {
        return color;
    }
    // 閾値の前後を半分の幅でなめらかにつなぐ。
    const float luminance = HikariLuminance(color);
    const float knee = threshold * 0.5f;
    float soft = clamp(luminance - threshold + knee, 0.0f, 2.0f * knee);
    soft = soft * soft / (4.0f * knee + 1.0e-4f);
    return color * (max(luminance - threshold, soft) / max(luminance, 1.0e-4f));
}

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    const float2 t = gUser[1].xy;
    const float2 uv = input.uv;
    const float3 a = SampleSource(uv + t * float2(-2.0f, -2.0f));
    const float3 b = SampleSource(uv + t * float2(0.0f, -2.0f));
    const float3 c = SampleSource(uv + t * float2(2.0f, -2.0f));
    const float3 d = SampleSource(uv + t * float2(-2.0f, 0.0f));
    const float3 e = SampleSource(uv);
    const float3 f = SampleSource(uv + t * float2(2.0f, 0.0f));
    const float3 g = SampleSource(uv + t * float2(-2.0f, 2.0f));
    const float3 h = SampleSource(uv + t * float2(0.0f, 2.0f));
    const float3 i = SampleSource(uv + t * float2(2.0f, 2.0f));
    const float3 j = SampleSource(uv + t * float2(-1.0f, -1.0f));
    const float3 k = SampleSource(uv + t * float2(1.0f, -1.0f));
    const float3 l = SampleSource(uv + t * float2(-1.0f, 1.0f));
    const float3 m = SampleSource(uv + t * float2(1.0f, 1.0f));
    // 中央の箱（重み 0.5）と四隅の箱（各 0.125）。
    const float3 boxes[5] = {
        (j + k + l + m) * 0.25f,
        (a + b + d + e) * 0.25f,
        (b + c + e + f) * 0.25f,
        (d + e + g + h) * 0.25f,
        (e + f + h + i) * 0.25f,
    };
    static const float kBoxWeights[5] = {0.5f, 0.125f, 0.125f, 0.125f, 0.125f};
    if (gUser[0].z <= 0.5f)
    {
        float3 sum = 0.0f.xxx;
        [unroll] for (uint index = 0u; index < 5u; ++index)
        {
            sum += boxes[index] * kBoxWeights[index];
        }
        return float4(sum, 0.0f);
    }
    const float displayToScene = max(gUser[1].z, 1.0e-8f);
    float3 sum = 0.0f.xxx;
    float weightSum = 0.0f;
    [unroll] for (uint index = 0u; index < 5u; ++index)
    {
        const float3 box = Prefilter(boxes[index], displayToScene);
        const float weight = kBoxWeights[index] / (1.0f + HikariLuminance(box) / displayToScene);
        sum += box * weight;
        weightSum += weight;
    }
    return float4(sum / max(weightSum, 1.0e-8f) * max(gUser[0].y, 0.0f), 0.0f);
}
