cbuffer GroundTelegraphConstants : register(b0)
{
    float4x4 gViewProj;
    float4 gTimeParams;
};

struct GroundTelegraphVertex
{
    float3 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float2 effect : TEXCOORD1;
};

struct GroundTelegraphPixel
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float2 effect : TEXCOORD1;
};

GroundTelegraphPixel VSMain(GroundTelegraphVertex input)
{
    GroundTelegraphPixel output;
    output.position = mul(gViewProj, float4(input.position, 1.0f));
    output.color = input.color;
    output.uv = input.uv;
    output.effect = input.effect;
    return output;
}

float4 PSMain(GroundTelegraphPixel input) : SV_TARGET
{
    float alpha = saturate(input.color.a);
    float3 color = max(input.color.rgb, 0.0f.xxx);
    if (input.effect.x > 0.5f)
    {
        const float2 cells = abs(frac(
            input.uv * float2(10.0f, 7.0f) +
            float2(gTimeParams.x * 0.08f, -gTimeParams.x * 0.16f)) - 0.5f);
        const float grid = smoothstep(
            0.40f,
            0.49f,
            max(cells.x, cells.y));
        const float scan = 1.0f - smoothstep(
            0.04f,
            0.18f,
            abs(frac(input.uv.y * 2.0f - gTimeParams.x * 0.42f) - 0.5f));
        const float interaction = saturate(input.effect.y);
        const float energy = 0.42f + grid * 0.34f + scan * 0.16f +
            interaction * (0.42f + grid * 0.28f);
        alpha *= saturate(energy);
        color *= 0.72f + grid * 0.26f + scan * 0.12f +
            interaction * 0.38f;
    }
    return float4(color * alpha, alpha);
}
