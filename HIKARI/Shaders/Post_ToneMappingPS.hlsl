cbuffer CommonParams : register(b0)
{
    float gTime;
    float gDeltaTime;
    float gCombo;
    float gIntensity;
    float gResolutionX;
    float gResolutionY;
    float2 gCommonPadding;
    float4 gUser[16];
};

Texture2D gSceneTex : register(t0);
SamplerState gSamp : register(s0);

// TODO: Add unified tone mapping / exposure / gamma correction pass.
// Current lighting outputs are assumed to be consumed by the existing post pipeline.

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float3 ApplyReinhard(float3 color)
{
    return color / (color + 1.0f);
}

float3 ApplyACESApprox(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 main(PSInput input) : SV_TARGET
{
    float4 sceneColor = gSceneTex.Sample(gSamp, input.uv);
    const bool enabled = gUser[0].x >= 0.5f;
    const float exposure = max(gUser[0].y, 0.0f);
    const float gamma = max(gUser[0].z, 0.01f);
    const int mode = (int)round(gUser[0].w);

    float3 color = sceneColor.rgb * exposure;
    if (enabled)
    {
        if (mode == 1)
        {
            color = ApplyReinhard(color);
        }
        else if (mode == 2)
        {
            color = ApplyACESApprox(color);
        }
    }

    color = pow(saturate(color), 1.0f / gamma);
    return float4(color, sceneColor.a);
}
