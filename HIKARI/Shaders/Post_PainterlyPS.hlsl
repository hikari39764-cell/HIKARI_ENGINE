// 油彩風。表示の色の段階で一般化 Kuwahara（四つの扇の平均を、ばらつきの小さい扇ほど重く混ぜる）を掛け、輪郭を保った
// まま細部を筆の塊にまとめる。明るさの勾配で縁を暗くし、キャンバスの織り目と筆のむらを重ねる。
#include "Include/Post/HIKARI_PostStylize.hlsli"
#include "Include/Color/HIKARI_Luminance.hlsli"

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    const float3 source = gSceneTex.Sample(gSamp, input.uv).rgb;
    const float strength = saturate(gUser[0].x * gIntensity);
    if (strength <= 0.0001f)
        return float4(source, 1.0f);

    const float scale = HikariStylizePixelScale();
    const float radius = clamp(gUser[0].y * scale, 1.0f, 24.0f);
    const float sharpness = clamp(gUser[0].z, 1.0f, 16.0f);
    const float saturation = max(gUser[0].w, 0.0f);
    const float canvas = saturate(gUser[1].x);
    const float edgeDarken = saturate(gUser[1].y);
    const float brushNoise = saturate(gUser[1].z);
    const float2 texel = HikariStylizeTexel();

    // 筆のむら：扇の向きを画素ごとに少し回す。
    const float jitter = (HikariHash12(floor(input.position.xy / max(radius, 1.0f))) - 0.5f) * brushNoise * 1.2f;
    const float2x2 rotation = float2x2(cos(jitter), -sin(jitter), sin(jitter), cos(jitter));
    static const float2 kQuadrants[4] = {float2(-1.0f, -1.0f), float2(1.0f, -1.0f), float2(-1.0f, 1.0f),
                                         float2(1.0f, 1.0f)};
    float3 weightedColor = 0.0f.xxx;
    float weightSum = 0.0f;
    [unroll]
    for (uint quadrant = 0u; quadrant < 4u; ++quadrant)
    {
        float3 sum = 0.0f.xxx;
        float3 sumSquares = 0.0f.xxx;
        [unroll]
        for (uint y = 0u; y < 3u; ++y)
        {
            [unroll]
            for (uint x = 0u; x < 3u; ++x)
            {
                const float2 offset = mul(rotation, kQuadrants[quadrant] * (float2(x, y) + 0.5f) / 3.0f) * radius;
                const float3 sampleColor = gSceneTex.SampleLevel(gSamp, input.uv + offset * texel, 0.0f).rgb;
                sum += sampleColor;
                sumSquares += sampleColor * sampleColor;
            }
        }
        const float3 mean = sum / 9.0f;
        const float variance = HikariLuminance(max(sumSquares / 9.0f - mean * mean, 0.0f));
        const float weight = rcp(1.0f + pow(max(variance * 255.0f, 0.0f), sharpness * 0.25f));
        weightedColor += mean * weight;
        weightSum += weight;
    }
    float3 painted = weightedColor / max(weightSum, 1e-5f);
    painted = lerp(HikariLuminance(painted).xxx, painted, saturation);

    // 筆の跡：明るさの等高線に沿って細長く伸ばした雑音で色を揺らし、絵の具の塊を数段に寄せる。
    const float2 t = texel * radius;
    const float gx = HikariLuminance(gSceneTex.SampleLevel(gSamp, input.uv + float2(t.x, 0.0f), 0.0f).rgb) -
                     HikariLuminance(gSceneTex.SampleLevel(gSamp, input.uv - float2(t.x, 0.0f), 0.0f).rgb);
    const float gy = HikariLuminance(gSceneTex.SampleLevel(gSamp, input.uv + float2(0.0f, t.y), 0.0f).rgb) -
                     HikariLuminance(gSceneTex.SampleLevel(gSamp, input.uv - float2(0.0f, t.y), 0.0f).rgb);
    const float2 across = length(float2(gx, gy)) > 1e-3f ? normalize(float2(gx, gy)) : float2(0.7071f, 0.7071f);
    const float2 along = float2(-across.y, across.x);
    const float2 strokeCoord = float2(dot(input.position.xy, along) / (radius * 3.0f),
                                      dot(input.position.xy, across) / max(radius * 0.45f, 1.0f));
    const float stroke = HikariStylizeValueNoise(strokeCoord) - 0.5f;
    painted *= 1.0f + stroke * brushNoise * 0.22f;
    painted = lerp(painted, round(painted * 14.0f) / 14.0f, 0.35f * brushNoise);

    // 縁の暗さは物の上だけ（空の雲の縁は暗くしない）。
    const float onSurface = HikariPostReadInverseDepth(input.uv) > 0.0f ? 1.0f : 0.25f;
    const float edge = smoothstep(0.08f, 0.6f, HikariStylizeLumaEdge(input.uv, radius * 0.5f)) * onSurface;
    painted *= 1.0f - edge * edgeDarken * 0.45f;

    // 斜めに織ったキャンバス。明るい所ほど地の凹凸が見える。
    const float2 weave = input.position.xy / max(3.0f * scale, 1.0f);
    const float threads = 0.5f + 0.25f * (sin(weave.x * 3.14159265f) + sin(weave.y * 3.14159265f));
    const float grain = HikariHash12(floor(input.position.xy)) - 0.5f;
    painted *= 1.0f - canvas * (0.2f * threads + 0.08f * grain) * (0.4f + 0.6f * HikariLuminance(painted));
    return float4(lerp(source, saturate(painted), strength), 1.0f);
}
