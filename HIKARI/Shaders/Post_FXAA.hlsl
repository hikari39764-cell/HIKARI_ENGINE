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

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float Luma(float3 color)
{
    return dot(color, float3(0.299f, 0.587f, 0.114f));
}

float4 main(PSInput input) : SV_TARGET
{
    float4 center = gSceneTex.Sample(gSamp, input.uv);

    const bool enabled = gUser[1].x >= 0.5f;
    if (!enabled)
    {
        return center;
    }

    float2 texel = gUser[0].zw;
    if (texel.x <= 0.0f || texel.y <= 0.0f)
    {
        texel = 1.0f / max(float2(gResolutionX, gResolutionY), float2(1.0f, 1.0f));
    }

    const float edgeThreshold = max(gUser[1].y, 0.0312f);
    const float edgeThresholdMin = max(gUser[1].z, 0.0f);
    const float subpixelQuality = saturate(gUser[1].w);

    float3 rgbM = center.rgb;
    float3 rgbN = gSceneTex.Sample(gSamp, input.uv + float2(0.0f, -texel.y)).rgb;
    float3 rgbS = gSceneTex.Sample(gSamp, input.uv + float2(0.0f, texel.y)).rgb;
    float3 rgbW = gSceneTex.Sample(gSamp, input.uv + float2(-texel.x, 0.0f)).rgb;
    float3 rgbE = gSceneTex.Sample(gSamp, input.uv + float2(texel.x, 0.0f)).rgb;

    float lumaM = Luma(rgbM);
    float lumaN = Luma(rgbN);
    float lumaS = Luma(rgbS);
    float lumaW = Luma(rgbW);
    float lumaE = Luma(rgbE);

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float contrast = lumaMax - lumaMin;
    float threshold = max(edgeThresholdMin, lumaMax * edgeThreshold);
    if (contrast < threshold)
    {
        return center;
    }

    float3 rgbNW = gSceneTex.Sample(gSamp, input.uv + float2(-texel.x, -texel.y)).rgb;
    float3 rgbNE = gSceneTex.Sample(gSamp, input.uv + float2(texel.x, -texel.y)).rgb;
    float3 rgbSW = gSceneTex.Sample(gSamp, input.uv + float2(-texel.x, texel.y)).rgb;
    float3 rgbSE = gSceneTex.Sample(gSamp, input.uv + float2(texel.x, texel.y)).rgb;

    float lumaNW = Luma(rgbNW);
    float lumaNE = Luma(rgbNE);
    float lumaSW = Luma(rgbSW);
    float lumaSE = Luma(rgbSE);

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25f * subpixelQuality), 1.0f / 128.0f);
    float rcpDirMin = rcp(min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, -8.0f, 8.0f) * texel;

    float3 rgbA = 0.5f * (
        gSceneTex.Sample(gSamp, input.uv + dir * (1.0f / 3.0f - 0.5f)).rgb +
        gSceneTex.Sample(gSamp, input.uv + dir * (2.0f / 3.0f - 0.5f)).rgb);

    float3 rgbB = rgbA * 0.5f + 0.25f * (
        gSceneTex.Sample(gSamp, input.uv + dir * -0.5f).rgb +
        gSceneTex.Sample(gSamp, input.uv + dir * 0.5f).rgb);

    float lumaB = Luma(rgbB);
    float3 finalColor = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;

    return float4(finalColor, center.a);
}
