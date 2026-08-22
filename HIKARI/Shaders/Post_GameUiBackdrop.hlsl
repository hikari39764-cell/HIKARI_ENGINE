cbuffer BackdropConstants : register(b0)
{
    float2 gInverseSize;
    float gBlurRadius;
    float gProgress;
    float gDimAmount;
    float gDesaturation;
    float gVignette;
    float gPadding;
    float4 gTint;
};

Texture2D<float4> gScene : register(t0);
SamplerState gSampler : register(s0);

float3 SampleBackdrop(float2 uv, float2 radius)
{
    float3 color = gScene.Sample(gSampler, uv).rgb * 0.20;
    color += gScene.Sample(gSampler, uv + float2(radius.x, 0.0)).rgb * 0.10;
    color += gScene.Sample(gSampler, uv - float2(radius.x, 0.0)).rgb * 0.10;
    color += gScene.Sample(gSampler, uv + float2(0.0, radius.y)).rgb * 0.10;
    color += gScene.Sample(gSampler, uv - float2(0.0, radius.y)).rgb * 0.10;
    color += gScene.Sample(gSampler, uv + radius).rgb * 0.075;
    color += gScene.Sample(gSampler, uv - radius).rgb * 0.075;
    color += gScene.Sample(gSampler, uv + float2(radius.x, -radius.y)).rgb * 0.075;
    color += gScene.Sample(gSampler, uv + float2(-radius.x, radius.y)).rgb * 0.075;
    color += gScene.Sample(gSampler, uv + radius * 1.85).rgb * 0.025;
    color += gScene.Sample(gSampler, uv - radius * 1.85).rgb * 0.025;
    color += gScene.Sample(gSampler, uv + float2(radius.x, -radius.y) * 1.85).rgb * 0.025;
    color += gScene.Sample(gSampler, uv + float2(-radius.x, radius.y) * 1.85).rgb * 0.025;
    return color;
}

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target0
{
    const float progress = smoothstep(0.0, 1.0, saturate(gProgress));
    const float2 radius = gInverseSize * gBlurRadius * progress;
    const float3 original = gScene.Sample(gSampler, uv).rgb;
    float3 color = SampleBackdrop(uv, radius);
    color = lerp(original, color, progress);
    const float luminance = dot(color, float3(0.299, 0.587, 0.114));
    color = lerp(color, luminance.xxx, gDesaturation * progress);
    color = lerp(color, color * gTint.rgb, gDimAmount * progress * gTint.a);
    const float2 centered = uv * 2.0 - 1.0;
    const float vignette = smoothstep(0.28, 1.32, dot(centered, centered));
    color *= 1.0 - vignette * gVignette * progress;
    return float4(color, 1.0);
}
