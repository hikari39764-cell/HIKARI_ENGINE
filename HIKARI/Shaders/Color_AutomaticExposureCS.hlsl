#include "Include/Color/HIKARI_AutomaticExposureInterface.hlsli"

// 対数輝度のヒストグラムから、暗部と明部の外れ値を除いた区間の対数平均を測る。
// 太陽や発光体などの小さな高輝度領域と、黒つぶれした影が露出を引っ張らない。
static const uint kBinCount = 128u;
static const float kMinimumLog2Luminance = -12.0f;
static const float kMaximumLog2Luminance = 20.0f;
static const uint kGridWidth = 128u;
static const uint kGridHeight = 72u;
static const float kLowPercentile = 0.10f;
static const float kHighPercentile = 0.90f;
// 明部の保護に使う百分位。空や光源など画面の上位 5% の明るさ。
static const float kHighlightPercentile = 0.95f;
// 重みは groupshared の整数加算のための固定小数点。
static const float kWeightScale = 256.0f;

groupshared uint gBins[kBinCount];

float HikariHalton(uint index, uint base)
{
    float result = 0.0f;
    float f = 1.0f / (float)base;
    uint i = index + 1u;
    while (i > 0u)
    {
        result += f * (float)(i % base);
        i /= base;
        f /= (float)base;
    }
    return result;
}

[numthreads(HIKARI_AUTOMATIC_EXPOSURE_THREAD_COUNT, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint threadIndex = dispatchThreadId.x;
    if (threadIndex < kBinCount)
    {
        gBins[threadIndex] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    const float binsPerLog2 = (float)kBinCount / (kMaximumLog2Luminance - kMinimumLog2Luminance);
    for (uint sampleIndex = threadIndex; sampleIndex < kGridWidth * kGridHeight;
        sampleIndex += HIKARI_AUTOMATIC_EXPOSURE_THREAD_COUNT)
    {
        const uint2 gridCoord = uint2(sampleIndex % kGridWidth, sampleIndex / kGridWidth);
        // 格子内の低食い違い量ジッタで格子模様を避ける。
        const float2 uv = float2(
            ((float)gridCoord.x + HikariHalton(sampleIndex, 2u)) / (float)kGridWidth,
            ((float)gridCoord.y + HikariHalton(sampleIndex, 3u)) / (float)kGridHeight);
        const uint2 pixelCoord = uint2(
            min(gWidth - 1u, (uint)(uv.x * (float)gWidth)),
            min(gHeight - 1u, (uint)(uv.y * (float)gHeight)));
        const float3 color = max(gSceneColor.Load(int3(pixelCoord, 0)).rgb, 0.0f.xxx);
        const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f)) * gInverseMeteringPreExposure;
        const float log2Luminance = clamp(log2(max(luminance, 1e-8f)), kMinimumLog2Luminance,
            kMaximumLog2Luminance - 1e-3f);
        const uint bin = min((uint)((log2Luminance - kMinimumLog2Luminance) * binsPerLog2), kBinCount - 1u);
        // 画面中央を 2.5 倍まで重く見る（被写体優先の中央重点測光）。
        const float2 centered = uv * 2.0f - 1.0f;
        const float weight = lerp(1.0f, 2.5f, saturate(1.0f - dot(centered, centered)));
        InterlockedAdd(gBins[bin], (uint)(weight * kWeightScale));
    }
    GroupMemoryBarrierWithGroupSync();

    if (threadIndex == 0u)
    {
        float total = 0.0f;
        for (uint bin = 0u; bin < kBinCount; ++bin)
        {
            total += (float)gBins[bin];
        }
        const float lowCut = total * kLowPercentile;
        const float highCut = total * kHighPercentile;
        const float highlightCut = total * kHighlightPercentile;
        float accumulated = 0.0f;
        float logSum = 0.0f;
        float windowWeight = 0.0f;
        float highlightLog2 = kMaximumLog2Luminance;
        bool highlightFound = false;
        for (uint index = 0u; index < kBinCount; ++index)
        {
            const float count = (float)gBins[index];
            const float inside = max(0.0f, min(accumulated + count, highCut) - max(accumulated, lowCut));
            logSum += (kMinimumLog2Luminance + ((float)index + 0.5f) / binsPerLog2) * inside;
            windowWeight += inside;
            if (!highlightFound && accumulated + count >= highlightCut && count > 0.0f)
            {
                highlightLog2 = kMinimumLog2Luminance + ((float)index + 0.5f) / binsPerLog2;
                highlightFound = true;
            }
            accumulated += count;
        }
        const float averageLog2 = windowWeight > 0.0f ? logSum / windowWeight : log2(0.18f);
        gMeasurement[0] = float4(exp2(averageLog2), windowWeight / kWeightScale, total / kWeightScale,
            exp2(highlightFound ? highlightLog2 : averageLog2));
    }
}
