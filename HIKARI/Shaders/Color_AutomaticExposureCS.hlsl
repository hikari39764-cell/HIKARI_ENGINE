#include "Include/Color/HIKARI_AutomaticExposureInterface.hlsli"

groupshared float gLogLuminance[
    HIKARI_AUTOMATIC_EXPOSURE_THREAD_COUNT];
groupshared float gLinearLuminance[
    HIKARI_AUTOMATIC_EXPOSURE_THREAD_COUNT];
groupshared float gSampleWeight[
    HIKARI_AUTOMATIC_EXPOSURE_THREAD_COUNT];

float HikariMeteringWeight(float logLuminance)
{
    float shadowWeight = smoothstep(-12.0f, -6.0f, logLuminance);
    float highlightWeight =
        1.0f - 0.85f * smoothstep(8.0f, 14.0f, logLuminance);
    return max(0.001f, shadowWeight * highlightWeight);
}

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
    const uint gridWidth = HIKARI_AUTOMATIC_EXPOSURE_THREAD_COUNT;
    const uint gridHeight = 36u;
    const uint sampleCount = gridWidth * gridHeight;

    float logSum = 0.0f;
    float linearSum = 0.0f;
    float weightSum = 0.0f;
    for (uint sampleIndex = threadIndex;
        sampleIndex < sampleCount;
        sampleIndex += HIKARI_AUTOMATIC_EXPOSURE_THREAD_COUNT)
    {
        uint2 gridCoord = uint2(
            sampleIndex % gridWidth,
            sampleIndex / gridWidth);
        // Low-discrepancy jitter per sample reduces grid structure and the
        // CPU-side exposure smoother filters the remaining frame noise.
        const float2 uv = float2(
            ((float)gridCoord.x + HikariHalton(sampleIndex, 2u)) /
                (float)gridWidth,
            ((float)gridCoord.y + HikariHalton(sampleIndex, 3u)) /
                (float)gridHeight);
        uint2 pixelCoord = uint2(
            min(gWidth - 1u, (uint)(uv.x * (float)gWidth)),
            min(gHeight - 1u, (uint)(uv.y * (float)gHeight)));
        float3 color = max(
            gSceneColor.Load(int3(pixelCoord, 0)).rgb,
            0.0f.xxx);
        float luminance = max(
            dot(color, float3(0.2126f, 0.7152f, 0.0722f)),
            1e-6f);
        float logLuminance = clamp(
            log2(luminance),
            -16.0f,
            16.0f);
        float weight = HikariMeteringWeight(logLuminance);
        logSum += logLuminance * weight;
        linearSum += luminance * weight;
        weightSum += weight;
    }

    gLogLuminance[threadIndex] = logSum;
    gLinearLuminance[threadIndex] = linearSum;
    gSampleWeight[threadIndex] = weightSum;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = HIKARI_AUTOMATIC_EXPOSURE_THREAD_COUNT / 2u;
        stride > 0u;
        stride >>= 1u)
    {
        if (threadIndex < stride)
        {
            gLogLuminance[threadIndex] +=
                gLogLuminance[threadIndex + stride];
            gLinearLuminance[threadIndex] +=
                gLinearLuminance[threadIndex + stride];
            gSampleWeight[threadIndex] +=
                gSampleWeight[threadIndex + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (threadIndex == 0u)
    {
        float weight = max(gSampleWeight[0], 1e-4f);
        gMeasurement[0] = float4(
            exp2(gLogLuminance[0] / weight),
            gLinearLuminance[0] / weight,
            weight,
            0.0f);
    }
}
