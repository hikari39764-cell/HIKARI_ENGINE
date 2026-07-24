cbuffer MotionVectorDebugCB : register(b0)
{
    float4 gDebugParams; // x: pixels-to-color scale, yz: texture size
};

Texture2D<float2> gMotionVectors : register(t0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
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

float4 PSMain(VSOut input) : SV_TARGET
{
    uint2 textureSize = uint2(
        max(gDebugParams.y, 1.0f),
        max(gDebugParams.z, 1.0f));
    uint2 pixel = min(uint2(input.position.xy), textureSize - 1u);
    float2 motionPixels = gMotionVectors.Load(int3(pixel, 0));

    float scale = max(gDebugParams.x, 1.0f / 512.0f);
    float magnitude = length(motionPixels) * scale;
    if (magnitude < 0.002f)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float2 signedColor = saturate(float2(
        0.5f + motionPixels.x * scale,
        0.5f - motionPixels.y * scale));
    float magColor = saturate(magnitude);
    return float4(signedColor, magColor, 1.0f);
}
