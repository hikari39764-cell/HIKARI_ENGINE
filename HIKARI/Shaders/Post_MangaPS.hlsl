// 漫画の網点（スクリーントーン）。表示の色の段階で明るさを数段の調子に分け、白は紙、黒はベタか斜線、中間は回した
// 格子の網点の面積で表す。深度の輪郭と折れ目、明るさの境に墨の線を引く。
#include "Include/Post/HIKARI_PostStylize.hlsli"

// 網点の面積 coverage（0..1）の黒の割合。点の縁は画素一つ分で均す。
float Screentone(float2 pixel, float cellPixels, float angleDegrees, float coverage)
{
    const float2 grid = HikariStylizeRotatedGrid(pixel, cellPixels, angleDegrees);
    const float2 cell = frac(grid) - 0.5f;
    // 半分を超えると黒地に白抜きの点へ切り替え、面積と見た目の明るさを揃える。
    const bool invert = coverage > 0.5f;
    const float area = invert ? 1.0f - coverage : coverage;
    const float radius = sqrt(area / 3.14159265f);
    const float distanceToCenter = length(cell);
    const float aa = max(fwidth(grid.x), 1e-4f);
    const float disc = 1.0f - smoothstep(radius - aa, radius + aa, distanceToCenter);
    return invert ? 1.0f - disc : disc;
}

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    const float3 source = gSceneTex.Sample(gSamp, input.uv).rgb;
    // 画面（キャプチャを貼った電視など、ProtectReadability の領域）には網点を掛けない。画面の中は画面専用の描き方に任せる。
    const float2 resolution = max(float2(gResolutionX, gResolutionY), 1.0f.xx);
    // 画面の前にある物（他の電視など）の上では保護を外す（場景の深度で判定。深度の入力が無ければ従来どおり）。
    const float readability = HikariScreenReadabilityAt(input.uv, resolution, 1.0f - HikariPostReadInverseDepth(input.uv));
    const float strength = saturate(gUser[0].x * gIntensity) * (1.0f - readability);
    if (strength <= 0.0001f)
        return float4(source, 1.0f);

    const float scale = HikariStylizePixelScale();
    const float levels = clamp(round(gUser[0].y), 2.0f, 8.0f);
    const float screentone = saturate(gUser[0].z);
    const float dotPixels = max(gUser[0].w * scale, 1.5f);
    const float dotAngle = gUser[1].x;
    const float ink = saturate(gUser[1].y);
    const float lineRadius = max(gUser[1].z * scale, 0.5f);
    const float hatching = saturate(gUser[2].y);
    const float whitePoint = gUser[2].w;
    const float blackPoint = gUser[3].w;
    const float keepColor = saturate(gUser[2].z);
    const float3 paper = saturate(gUser[3].rgb);
    const float3 inkColor = saturate(gUser[4].rgb);

    // 調子：0 が黒、levels - 1 が白。
    const float luma = HikariStylizeTone(source, blackPoint, whitePoint);
    const float tone = min(floor(luma * levels), levels - 1.0f) / (levels - 1.0f);
    const float coverage = 1.0f - tone;
    // 微分を使うので網点と斜線はどの画素でも求め、調子で選ぶ。いちばん暗い調子はベタ、hatching があれば交差した斜線。
    const float2 pixel = input.position.xy;
    const float2 hatchGrid = HikariStylizeRotatedGrid(pixel, dotPixels * 0.75f, dotAngle);
    const float crossHatch = max(HikariStylizeLines(hatchGrid.x, 0.55f), HikariStylizeLines(hatchGrid.y, 0.55f));
    const float dots = Screentone(pixel, dotPixels, dotAngle, coverage) * screentone;
    float inkMask = coverage >= 0.999f ? lerp(1.0f, crossHatch, hatching) : dots;

    inkMask = max(inkMask, HikariStylizeInkLines(input.uv, lineRadius, gUser[1].w, gUser[2].x) * ink);

    // 色を残すと紙の白の代わりに元の色相を淡く載せる（カラー漫画）。
    const float3 tint = lerp(paper, paper * normalize(max(source, 1e-4f)) * 1.7320508f, keepColor);
    const float3 manga = lerp(saturate(tint), inkColor, saturate(inkMask));
    return float4(lerp(source, manga, strength), 1.0f);
}
