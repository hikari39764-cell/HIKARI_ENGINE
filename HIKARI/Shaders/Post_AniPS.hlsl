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

float3 DisplayMap(float3 color)
{
    color = max(color, 0.0f.xxx);
    return color / (1.0f.xxx + color);
}

float3 ApplySaturation(float3 color, float saturation)
{
    float luma = Luma(color);
    return lerp(luma.xxx, color, saturation);
}

float3 ApplyContrast(float3 color, float contrast)
{
    return (color - 0.5f.xxx) * contrast + 0.5f.xxx;
}

float3 TonePosterize(float3 color, float amount, float levels)
{
    amount = saturate(amount);
    levels = max(levels, 2.0f);

    float luma = max(Luma(DisplayMap(color)), 1.0e-4f);
    float quantized = floor(luma * levels + 0.5f) / levels;
    float scale = clamp(quantized / luma, 0.25f, 2.5f);
    return lerp(color, color * scale, amount);
}

float4 main(PSInput input) : SV_TARGET
{
    const float4 center = gSceneTex.Sample(gSamp, input.uv);

    const float strength = saturate(gUser[0].x * gIntensity);
    if (strength <= 0.0001f)
    {
        return center;
    }

    float2 texel = 1.0f / max(float2(gResolutionX, gResolutionY), float2(1.0f, 1.0f));

    const float edgeSoftness = max(gUser[0].y, 0.0f);
    const float posterizeAmount = saturate(gUser[0].z);
    const float inkAmount = saturate(gUser[0].w);
    const float saturation = max(gUser[1].x, 0.0f);
    const float contrast = max(gUser[1].y, 0.01f);
    const float shadowLift = max(gUser[1].z, 0.0f);
    const float edgeThreshold = max(gUser[1].w, 0.001f);
    const float toneLevels = max(gUser[2].x, 2.0f);

    float3 c = center.rgb;
    float3 n = gSceneTex.Sample(gSamp, input.uv + float2(0.0f, -texel.y)).rgb;
    float3 s = gSceneTex.Sample(gSamp, input.uv + float2(0.0f, texel.y)).rgb;
    float3 w = gSceneTex.Sample(gSamp, input.uv + float2(-texel.x, 0.0f)).rgb;
    float3 e = gSceneTex.Sample(gSamp, input.uv + float2(texel.x, 0.0f)).rgb;
    float3 nw = gSceneTex.Sample(gSamp, input.uv + float2(-texel.x, -texel.y)).rgb;
    float3 ne = gSceneTex.Sample(gSamp, input.uv + float2(texel.x, -texel.y)).rgb;
    float3 sw = gSceneTex.Sample(gSamp, input.uv + float2(-texel.x, texel.y)).rgb;
    float3 se = gSceneTex.Sample(gSamp, input.uv + float2(texel.x, texel.y)).rgb;

    float lN = Luma(DisplayMap(n));
    float lS = Luma(DisplayMap(s));
    float lW = Luma(DisplayMap(w));
    float lE = Luma(DisplayMap(e));
    float lNW = Luma(DisplayMap(nw));
    float lNE = Luma(DisplayMap(ne));
    float lSW = Luma(DisplayMap(sw));
    float lSE = Luma(DisplayMap(se));

    float sobelX = (lNE + 2.0f * lE + lSE) - (lNW + 2.0f * lW + lSW);
    float sobelY = (lSW + 2.0f * lS + lSE) - (lNW + 2.0f * lN + lNE);
    float edgeMagnitude = length(float2(sobelX, sobelY));
    float edgeMask = smoothstep(edgeThreshold, edgeThreshold * 2.5f + 1.0e-4f, edgeMagnitude);

    float2 gradient = float2(sobelX, sobelY);
    float gradientLen = max(length(gradient), 1.0e-4f);
    float2 tangent = float2(-gradient.y, gradient.x) / gradientLen;
    float2 aaStep = tangent * texel * edgeSoftness;

    float3 alongEdge =
        c * 0.40f +
        gSceneTex.Sample(gSamp, input.uv + aaStep * 0.65f).rgb * 0.24f +
        gSceneTex.Sample(gSamp, input.uv - aaStep * 0.65f).rgb * 0.24f +
        gSceneTex.Sample(gSamp, input.uv + aaStep * 1.35f).rgb * 0.06f +
        gSceneTex.Sample(gSamp, input.uv - aaStep * 1.35f).rgb * 0.06f;

    float aaAmount = saturate(edgeMask * edgeSoftness * 0.75f) * strength;
    float3 softened = lerp(c, alongEdge, aaAmount);

    float3 stylized = ApplySaturation(softened, saturation);
    stylized = ApplyContrast(stylized, contrast);
    stylized = TonePosterize(stylized, posterizeAmount, toneLevels);
    stylized += shadowLift.xxx * (1.0f - saturate(Luma(DisplayMap(stylized))));

    float ink = 1.0f - edgeMask * inkAmount * strength;
    stylized *= max(ink, 0.0f);

    float3 finalColor = lerp(c, stylized, strength);
    return float4(max(finalColor, 0.0f.xxx), center.a);
}
