cbuffer GameUiConstants : register(b0)
{
    float2 gScreenSize;
    float2 gTexelSize;
    float4 gTint;
    float4 gEffectColor;
    float4 gHighlightColor;
    float4 gParams0; // fill, progress, intensity, time
    float4 gParams1; // direction, effect type, flags
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    const float2 safeScreen = max(gScreenSize, 1.0.xx);
    const float2 ndc = float2(
        input.position.x / safeScreen.x * 2.0 - 1.0,
        1.0 - input.position.y / safeScreen.y * 2.0);
    output.position = float4(ndc, 0.0, 1.0);
    output.uv = input.uv;
    return output;
}

float Hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float SampleExpandedAlpha(float2 uv, float radius)
{
    const float2 texel = gTexelSize * radius;
    float alpha = gTexture.Sample(gSampler, uv).a;
    alpha = max(alpha, gTexture.Sample(gSampler, uv + float2(texel.x, 0.0)).a);
    alpha = max(alpha, gTexture.Sample(gSampler, uv - float2(texel.x, 0.0)).a);
    alpha = max(alpha, gTexture.Sample(gSampler, uv + float2(0.0, texel.y)).a);
    alpha = max(alpha, gTexture.Sample(gSampler, uv - float2(0.0, texel.y)).a);
    return alpha;
}

float4 ApplyOutline(float4 color, float2 uv, float width, float strength)
{
    const float expanded = SampleExpandedAlpha(uv, width);
    const float outline = saturate(expanded - color.a) * strength;
    color.rgb = lerp(color.rgb, gEffectColor.rgb, outline);
    color.a = max(color.a, outline * gEffectColor.a);
    return color;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    const float fill = saturate(gParams0.x);
    const float progress = saturate(gParams0.y);
    const float intensity = max(gParams0.z, 0.0);
    const float time = gParams0.w;
    const uint effect = (uint)round(gParams1.z);
    float4 color = gTexture.Sample(gSampler, input.uv) * gTint;

    if (effect == 1u || effect == 2u)
    {
        clip(fill - input.uv.x);
        const float leading = 1.0 - smoothstep(
            0.0,
            0.035,
            abs(input.uv.x - fill));
        color.rgb += gHighlightColor.rgb * leading *
            intensity * (effect == 2u ? 0.28 : 0.18);
        if (effect == 2u)
        {
            const float recoveryBand = 0.5 + 0.5 * sin(
                (input.uv.x * 18.0 - time * 4.0) * 6.2831853);
            color.rgb += gHighlightColor.rgb * recoveryBand *
                0.08 * intensity * color.a;
        }
    }
    else if (effect == 3u)
    {
        // Cooldown never hides or radially masks the icon. The whole image
        // remains legible while energy bands, desaturation and hard outline
        // communicate the remaining fraction.
        const float luminance = dot(color.rgb, float3(0.299, 0.587, 0.114));
        color.rgb = lerp(color.rgb, luminance.xxx, fill * 0.82);
        color.rgb *= lerp(1.0, 0.46, fill);
        const float diagonal = frac(
            input.uv.x * 5.0 + input.uv.y * 3.0 - time * 0.85);
        const float band = 1.0 - smoothstep(0.08, 0.22, abs(diagonal - 0.5));
        color.rgb += gEffectColor.rgb * band * fill * 0.34 * intensity * color.a;
        color = ApplyOutline(color, input.uv, 2.0 + intensity, 0.90);
    }
    else if (effect == 4u)
    {
        const float pulse = 0.5 + 0.5 * sin(time * 7.0);
        color.rgb += gHighlightColor.rgb * pulse * 0.24 * intensity * color.a;
        color = ApplyOutline(color, input.uv, 3.0, 1.0 + pulse * intensity);
    }
    else if (effect == 5u || effect == 6u)
    {
        const float noise = (Hash21(floor(input.uv * 96.0)) - 0.5) * 0.12;
        const float diagonal = input.uv.x * 0.72 + input.uv.y * 0.28 + noise;
        const float threshold = lerp(-0.12, 1.12, progress);
        const float revealIn = 1.0 - smoothstep(
            threshold - 0.055,
            threshold + 0.055,
            diagonal);
        const float reveal = effect == 5u ? revealIn : 1.0 - revealIn;
        const float edge = 1.0 - smoothstep(
            0.018,
            0.075,
            abs(diagonal - threshold));
        color.a *= reveal;
        color.rgb = lerp(color.rgb, gHighlightColor.rgb, edge * intensity);
        color = ApplyOutline(color, input.uv, 3.0, 1.15 * intensity);
    }
    else if (effect == 7u)
    {
        const float diagonal = input.uv.x * 0.75 + input.uv.y * 0.25;
        const float threshold = lerp(-0.12, 1.12, progress);
        const float reveal = 1.0 - smoothstep(
            threshold - 0.08,
            threshold + 0.08,
            diagonal);
        const float revealEdge = 1.0 - smoothstep(
            0.02,
            0.10,
            abs(diagonal - threshold));
        const float pulse = 0.5 + 0.5 * sin(time * 9.0);
        const float2 split = normalize(gParams1.xy + float2(1.0e-4, 0.0)) *
            gTexelSize * (1.5 + pulse * 2.0) * intensity;
        const float red = gTexture.Sample(gSampler, input.uv + split).r;
        const float blue = gTexture.Sample(gSampler, input.uv - split).b;
        color.r = max(color.r, red * 0.72);
        color.b = max(color.b, blue * 0.82);
        color.rgb += gHighlightColor.rgb *
            (pulse * 0.18 + revealEdge * 0.42) *
            color.a * intensity;
        color.a *= reveal;
        color = ApplyOutline(color, input.uv, 3.0, 1.2 + pulse * intensity);
    }
    else if (effect == 8u)
    {
        const float collapse = abs(input.uv.x - 0.5) * 2.0;
        const float keep = 1.0 - smoothstep(
            1.0 - progress,
            1.04 - progress,
            collapse);
        const float flash = 1.0 - smoothstep(0.0, 0.24, progress);
        color.rgb = lerp(color.rgb, gHighlightColor.rgb, flash * intensity);
        color.a *= keep;
        color = ApplyOutline(color, input.uv, 4.0, 1.5 * intensity);
    }
    else if (effect == 9u)
    {
        const float pulse = sin(progress * 3.14159265);
        const float2 shake = float2(sin(time * 73.0), cos(time * 61.0)) *
            gTexelSize * 4.0 * pulse * intensity;
        const float4 shifted = gTexture.Sample(gSampler, input.uv + shake);
        color.rgb = lerp(color.rgb, shifted.bgr, 0.45 * pulse);
        color = ApplyOutline(color, input.uv, 4.0, 1.4 * pulse);
    }
    else if (effect == 10u)
    {
        const float distance = gTexture.Sample(gSampler, input.uv).a;
        const float antialias = max(fwidth(distance) * 1.35, 0.004);
        const float fillAlpha = smoothstep(
            0.5 - antialias,
            0.5 + antialias,
            distance) * gTint.a;
        const float outlineEdge = 0.5 - min(intensity, 12.0) * 0.020;
        const float outlineField = smoothstep(
            outlineEdge - antialias,
            outlineEdge + antialias,
            distance);
        const float outlineAlpha = max(
            outlineField - fillAlpha,
            0.0) * gEffectColor.a;
        const float2 shadowUv = input.uv - gParams1.xy * gTexelSize;
        const float shadowDistance = gTexture.Sample(
            gSampler,
            shadowUv).a;
        const float shadowField = smoothstep(
            outlineEdge - antialias,
            outlineEdge + antialias,
            shadowDistance);
        const float shadowAlpha = max(
            shadowField - max(fillAlpha, outlineField),
            0.0) * gHighlightColor.a * progress;

        float3 premultiplied = gHighlightColor.rgb * shadowAlpha;
        float alpha = shadowAlpha;
        premultiplied = gEffectColor.rgb * outlineAlpha +
            premultiplied * (1.0 - outlineAlpha);
        alpha = outlineAlpha + alpha * (1.0 - outlineAlpha);
        premultiplied = gTint.rgb * fillAlpha +
            premultiplied * (1.0 - fillAlpha);
        alpha = fillAlpha + alpha * (1.0 - fillAlpha);
        color = float4(
            alpha > 1.0e-5 ? premultiplied / alpha : 0.0.xxx,
            alpha);
    }
    else if (effect == 11u)
    {
        // A single authored dialogue panel remains intact. The transition is
        // a directional ink-edge reveal and never assumes separate white and
        // offset layers in the source artwork.
        const float diagonal = input.uv.x * 0.82 + input.uv.y * 0.18;
        const float threshold = lerp(-0.14, 1.14, progress);
        const float edgeNoise =
            (Hash21(floor(input.uv * float2(120.0, 42.0))) - 0.5) * 0.045;
        const float reveal = 1.0 - smoothstep(
            threshold - 0.055,
            threshold + 0.055,
            diagonal + edgeNoise);
        const float edge = 1.0 - smoothstep(
            0.012,
            0.070,
            abs(diagonal + edgeNoise - threshold));
        color.a *= reveal;
        color.rgb = lerp(
            color.rgb,
            gHighlightColor.rgb,
            edge * gHighlightColor.a * intensity);
        color = ApplyOutline(color, input.uv, 2.5, 0.75 * intensity);
    }
    else if (effect == 12u || effect == 13u)
    {
        const float selected = effect == 13u ? 1.0 : 0.0;
        const float sweep = 1.0 - smoothstep(
            0.035,
            0.16,
            abs(frac(input.uv.x - time * 1.35) - 0.5));
        const float pulse = 0.5 + 0.5 * sin(time * 8.0);
        color.rgb += gHighlightColor.rgb * color.a * selected *
            (0.18 + sweep * 0.42) * intensity;
        color = ApplyOutline(
            color,
            input.uv,
            2.0 + selected * 2.0,
            0.75 + selected * (0.65 + pulse * 0.35));
    }
    else if (effect == 14u)
    {
        const float distance = gTexture.Sample(gSampler, input.uv).a;
        const float antialias = max(fwidth(distance) * 1.45, 0.004);
        const float outerEdge = 0.5 - min(intensity, 12.0) * 0.026;
        const float outer = smoothstep(
            outerEdge - antialias * 2.0,
            0.5 + antialias,
            distance);
        const float core = smoothstep(
            0.5 - antialias,
            0.5 + antialias,
            distance);
        const float halo = saturate(outer - core * 0.28);
        const float glowAlpha = halo * gTint.a * min(time, 8.0);
        color = float4(gTint.rgb, saturate(glowAlpha));
    }
    else if (effect == 15u)
    {
        // Dialogue portraits keep one texture resident while focus moves
        // between the two authored slots. Entry uses a restrained diagonal
        // edge reveal; speaker changes use focus-driven color/scale on CPU.
        const float2 portraitUv = gParams1.x < 0.0
            ? float2(1.0 - input.uv.x, input.uv.y)
            : input.uv;
        color = gTexture.Sample(gSampler, portraitUv) * gTint;
        const float diagonal = portraitUv.x * 0.78 + portraitUv.y * 0.22;
        const float edgeNoise =
            (Hash21(floor(portraitUv * float2(88.0, 64.0))) - 0.5) * 0.035;
        const float threshold = lerp(-0.10, 1.10, progress);
        const float reveal = 1.0 - smoothstep(
            threshold - 0.060,
            threshold + 0.060,
            diagonal + edgeNoise);
        const float edge = 1.0 - smoothstep(
            0.012,
            0.070,
            abs(diagonal + edgeNoise - threshold));
        const float focus = fill;
        const float luminance = dot(color.rgb, float3(0.299, 0.587, 0.114));
        color.rgb = lerp(luminance.xxx, color.rgb, 0.38 + focus * 0.62);
        color.rgb += gEffectColor.rgb * edge * intensity * color.a;
        color.a *= reveal;
    }
    else if (effect == 16u || effect == 17u)
    {
        // Burst never turns its uncharged range transparent.  The authored
        // Front remains fully readable: charge is blue/white energy and the
        // remainder is a neutral grey silhouette of the same artwork.
        const float active = effect == 17u ? 1.0 : 0.0;
        const float charged = 1.0 - step(fill, input.uv.x);
        const float luminance = dot(color.rgb, float3(0.299, 0.587, 0.114));
        const float3 neutral = lerp(
            float3(0.25, 0.28, 0.33),
            luminance.xxx,
            0.22);
        const float stream = 0.5 + 0.5 * sin(
            (input.uv.x * 13.0 - time * (2.2 + active * 2.8) +
                input.uv.y * 2.0) * 6.2831853);
        const float leading = 1.0 - smoothstep(
            0.008,
            0.045,
            abs(input.uv.x - fill));
        const float readyPulse = smoothstep(0.94, 1.0, fill) *
            (0.5 + 0.5 * sin(time * 8.0));
        float3 chargedColor = color.rgb;
        chargedColor += gHighlightColor.rgb *
            (stream * (0.09 + active * 0.13) +
                leading * (0.52 + active * 0.34) +
                readyPulse * 0.20) * intensity * color.a;
        chargedColor = lerp(
            chargedColor,
            gHighlightColor.rgb,
            active * (0.08 + stream * 0.13) * intensity * color.a);
        color.rgb = lerp(neutral, chargedColor, charged);
        color = ApplyOutline(
            color,
            input.uv,
            2.0 + active * 1.0,
            (0.75 + active * 0.55 + readyPulse * 0.30) * intensity);
    }
    else if (effect == 18u || effect == 19u)
    {
        const float available = step(0.5, fill);
        const float luminance = dot(color.rgb, float3(0.299, 0.587, 0.114));
        color.rgb = lerp(
            lerp(float3(0.24, 0.27, 0.31), luminance.xxx, 0.18),
            color.rgb,
            available);
        const float phase = time * 3.7 + progress * 4.13;
        const float sweep = 1.0 - smoothstep(
            0.035,
            0.16,
            abs(frac(input.uv.x + input.uv.y * 0.35 - phase) - 0.5));
        const float usePulse = effect == 19u
            ? sin(saturate(progress) * 3.14159265)
            : 0.0;
        color.rgb += gHighlightColor.rgb * color.a * available * intensity *
            (sweep * 0.18 + usePulse * 0.58);
        color = ApplyOutline(
            color,
            input.uv,
            2.0 + usePulse * 2.0,
            (0.68 + usePulse * 0.90) * intensity);
    }
    else if (effect == 20u)
    {
        const float diagonal = frac(
            input.uv.x * 2.4 + input.uv.y * 0.75 - time * 0.34);
        const float sweep = 1.0 - smoothstep(
            0.045,
            0.18,
            abs(diagonal - 0.5));
        const float pulse = 0.5 + 0.5 * sin(time * 2.6);
        color.rgb += gHighlightColor.rgb * color.a * intensity *
            (sweep * 0.16 + pulse * 0.035);
        color = ApplyOutline(
            color,
            input.uv,
            2.0,
            (0.45 + pulse * 0.18) * intensity);
    }

    return color;
}
