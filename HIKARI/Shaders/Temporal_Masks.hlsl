#define HIKARI_TEMPORAL_MASKS
#include "Include/Temporal/HIKARI_TemporalShaderInterface.hlsli"
#include "Include/Color/HIKARI_Luminance.hlsli"
#include "Include/Post/HIKARI_ScreenReadability.hlsli"

// 場景キャプチャを映す面の手前に物があるかを見る深度（0 = 近い面）。面の四隅は b1 の HikariScreenReadabilityConstants。
Texture2D<float> gMaskSceneDepth : register(t2);

static const float kSceneCaptureReactive = 0.25f;
static const float kSceneCaptureHistoryBias = 1.0f;

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct PSOut
{
    float reactive : SV_TARGET0;
    float transparency : SV_TARGET1;
    float invalidDepthMotion : SV_TARGET2;
    float biasCurrentColor : SV_TARGET3;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut output;
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = pos;
    output.position =
        float4(pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

PSOut PSMain(VSOut input)
{
    uint width;
    uint height;
    gCompositedColor.GetDimensions(width, height);
    uint2 pixel = min(uint2(input.position.xy), uint2(width - 1u, height - 1u));
    float3 base = max(gCompositionBase.Load(int3(pixel, 0)).rgb, 0.0f);
    float3 composited = max(gCompositedColor.Load(int3(pixel, 0)).rgb, 0.0f);
    float3 difference = abs(composited - base);
    float relativeDifference =
        max(max(difference.r, difference.g), difference.b) /
        max(max(HikariLuminance(base), HikariLuminance(composited)), 0.25f);
    float lumaDifference =
        abs(HikariLuminance(composited) - HikariLuminance(base)) /
        max(max(HikariLuminance(base), HikariLuminance(composited)), 0.25f);

    PSOut output;
    output.transparency = saturate(relativeDifference * 2.0f);
    output.reactive = saturate(max(relativeDifference * 1.5f, lumaDifference * 2.5f));
    output.invalidDepthMotion = saturate(max(
        relativeDifference * 4.0f,
        max(max(difference.r, difference.g), difference.b) * 8.0f));
    // 場景キャプチャを映す面（手前に別の物が無い所だけ）。外部 upscaler の偏りはこの面だけに掛け、合成の差は含めない
    // （霧や反射の合成まで履歴を捨てると画面全体がちらつく）。
    const float surface = HikariScreenReadabilityAt((float2(pixel) + 0.5f) / float2(width, height), float2(width, height),
                                                    gMaskSceneDepth.Load(int3(pixel, 0)));
    output.reactive = max(output.reactive, surface * kSceneCaptureReactive);
    output.biasCurrentColor = surface * kSceneCaptureHistoryBias;
    return output;
}
