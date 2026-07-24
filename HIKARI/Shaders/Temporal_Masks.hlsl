Texture2D<float4> gCompositionBase : register(t0);
Texture2D<float4> gCompositedColor : register(t1);

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

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
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
        max(max(Luminance(base), Luminance(composited)), 0.25f);
    float lumaDifference =
        abs(Luminance(composited) - Luminance(base)) /
        max(max(Luminance(base), Luminance(composited)), 0.25f);

    PSOut output;
    output.transparency = saturate(relativeDifference * 2.0f);
    output.reactive = saturate(max(relativeDifference * 1.5f, lumaDifference * 2.5f));
    // Composited pixels use opaque depth and motion data that no longer
    // describe the final color. Keep this as a separate vendor-neutral hint.
    output.invalidDepthMotion = saturate(max(
        relativeDifference * 4.0f,
        max(max(difference.r, difference.g), difference.b) * 8.0f));
    return output;
}
