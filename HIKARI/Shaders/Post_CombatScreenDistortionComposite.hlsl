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

Texture2D<float4> gSceneTex : register(t0);
Texture2D<float4> gMaskTex : register(t1);
SamplerState gLinearClamp : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float3 HikariFiniteColor(float3 value)
{
    return all(isfinite(value)) ? max(value, 0.0f.xxx) : 0.0f.xxx;
}

float3 HikariScene(float2 uv)
{
    return HikariFiniteColor(
        gSceneTex.SampleLevel(gLinearClamp, saturate(uv), 0.0f).rgb);
}

float HikariRing(float distanceToCenter, float radius, float thickness)
{
    const float safeThickness = max(thickness, 0.00035f);
    const float ringDistance = (distanceToCenter - radius) / safeThickness;
    return exp(-ringDistance * ringDistance * 1.35f);
}

float HikariNoise(float2 uv)
{
    const float2 cell = floor(uv * float2(811.0f, 457.0f));
    return frac(sin(dot(cell, float2(127.1f, 311.7f))) * 43758.5453f);
}

float3 HikariRefractedScene(
    float2 uv,
    float2 warp,
    float2 tangentBlur,
    float chromatic)
{
    // The displaced sample owns the result. Symmetric-only sampling cancels
    // refraction into an ordinary blur and was a major reason the old effect
    // looked like a colored decal instead of a lens.
    const float3 displaced = HikariScene(uv + warp);
    const float3 displacedNear = HikariScene(uv + warp * 0.55f);
    const float3 displacedFar = HikariScene(uv + warp * 1.38f);
    const float3 trailA = HikariScene(uv + warp + tangentBlur);
    const float3 trailB = HikariScene(uv + warp - tangentBlur);
    const float3 trailFarA = HikariScene(uv + warp + tangentBlur * 2.15f);
    const float3 trailFarB = HikariScene(uv + warp - tangentBlur * 2.15f);
    float3 refracted = displaced * 0.34f + displacedNear * 0.14f +
        displacedFar * 0.14f + (trailA + trailB) * 0.12f +
        (trailFarA + trailFarB) * 0.07f;

    const float splitPixels = max(chromatic, 0.0f) *
        max(gResolutionX, gResolutionY) * 0.52f;
    const float2 splitDirection = dot(warp, warp) > 1.0e-10f
        ? normalize(warp)
        : float2(0.0f, 1.0f);
    const float2 splitUv = splitDirection *
        min(splitPixels, 7.0f) /
        float2(max(gResolutionX, 1.0f), max(gResolutionY, 1.0f));
    const float red = HikariScene(uv + warp + splitUv).r;
    const float green = refracted.g;
    const float blue = HikariScene(uv + warp - splitUv).b;
    const float splitWeight = saturate(chromatic * 38.0f);
    return lerp(refracted, float3(red, green, blue), splitWeight);
}

void HikariConsiderMaskSample(
    float4 sampleValue,
    float distanceWeight,
    float maximumMaskDimension,
    inout float bestScore,
    inout float4 bestSample,
    inout float expandedCoverage)
{
    const float sampleCoverage = saturate(sampleValue.b);
    const float displacementPixels = length(sampleValue.rg) *
        maximumMaskDimension;
    const float vectorConfidence = saturate(displacementPixels / 6.0f);
    const float score = sampleCoverage * distanceWeight *
        (0.38f + vectorConfidence * 0.62f);
    expandedCoverage = max(
        expandedCoverage,
        sampleCoverage * distanceWeight);
    if (score > bestScore)
    {
        bestScore = score;
        bestSample = sampleValue;
    }
}

float4 main(PSInput input) : SV_TARGET
{
    const float2 uv = saturate(input.uv);
    const float4 base = gSceneTex.SampleLevel(gLinearClamp, uv, 0.0f);
    if (!all(isfinite(base)))
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float3 resolved = HikariFiniteColor(base.rgb);
    const float aspect = max(
        gResolutionX / max(gResolutionY, 1.0f),
        0.1f);
    const float2 outputTexel = float2(
        1.0f / max(gResolutionX, 1.0f),
        1.0f / max(gResolutionY, 1.0f));
    const uint waveCount = min((uint)max(gCombo, 0.0f), 4u);

    [unroll]
    for (uint index = 0u; index < 4u; ++index)
    {
        if (index >= waveCount || gUser[index * 4u].w <= 0.5f)
        {
            continue;
        }

        const float2 centerUv = gUser[index * 4u].xy;
        const float age = saturate(gUser[index * 4u].z);
        const float maximumRadius = max(gUser[index * 4u + 1u].x, 0.01f);
        const float thickness = max(
            gUser[index * 4u + 1u].y,
            min(outputTexel.x, outputTexel.y) * 1.2f);
        const float distortion = max(gUser[index * 4u + 1u].z, 0.0f);
        const float intensity = max(gUser[index * 4u + 1u].w, 0.0f);
        const float3 tint = max(gUser[index * 4u + 2u].rgb, 0.0f.xxx);
        const float chromatic = max(gUser[index * 4u + 2u].w, 0.0f);
        const float flashIntensity = max(gUser[index * 4u + 3u].x, 0.0f);

        float2 delta = uv - centerUv;
        delta.x *= aspect;
        const float distanceToCenter = length(delta);
        const float2 radialDirection = distanceToCenter > 1.0e-5f
            ? delta / distanceToCenter
            : float2(0.0f, 1.0f);
        const float2 uvDirection = float2(
            radialDirection.x / aspect,
            radialDirection.y);
        const float2 tangentDirection = float2(
            -uvDirection.y,
            uvDirection.x);

        const float easeOutGrowth = 1.0f - pow(1.0f - age, 2.55f);
        const float radius = maximumRadius * lerp(0.025f, 1.0f, easeOutGrowth);
        const float fadeIn = smoothstep(0.0f, 0.035f, age);
        const float fadeOut = 1.0f - smoothstep(0.72f, 1.0f, age);
        const float envelope = fadeIn * fadeOut;
        const float angular = atan2(radialDirection.y, radialDirection.x);
        const float organic =
            sin(angular * 11.0f + age * 19.0f) * 0.55f +
            sin(angular * 23.0f - age * 13.0f) * 0.25f +
            (HikariNoise(uv + centerUv + age) - 0.5f) * 0.35f;
        const float waveWidth = thickness * lerp(0.90f, 1.50f, age);
        const float distortedDistance = distanceToCenter +
            organic * waveWidth * 0.22f;
        const float front = (distortedDistance - radius) /
            max(waveWidth, 0.0005f);
        const float compression = exp(-pow(front + 0.62f, 2.0f) * 1.35f);
        const float expansion = exp(-pow(front - 0.58f, 2.0f) * 1.15f);
        const float signedWarpProfile = expansion - compression * 0.96f;
        const float ring = exp(-front * front * 1.15f);
        const float interior = saturate(
            1.0f - distortedDistance / max(radius, 0.001f));
        const float interiorLens = interior * interior *
            (1.0f - smoothstep(0.48f, 0.92f, age));

        const float echoAge = saturate((age - 0.11f) / 0.89f);
        const float echoRadius = maximumRadius *
            lerp(0.08f, 1.08f, 1.0f - pow(1.0f - echoAge, 2.2f));
        const float echo = HikariRing(
            distortedDistance,
            echoRadius,
            waveWidth * 1.30f) *
            smoothstep(0.10f, 0.24f, age) *
            (1.0f - smoothstep(0.78f, 1.0f, age));
        const float echoSign = saturate(
            (distortedDistance - echoRadius) /
                max(waveWidth * 1.3f, 0.0005f));
        const float signedWarp = clamp(
            distortion * intensity * envelope *
                (signedWarpProfile * 1.30f + interiorLens * 0.18f +
                    echo * lerp(-0.12f, 0.18f, echoSign)),
            -0.12f,
            0.12f);
        const float2 radialWarp = uvDirection * signedWarp;
        const float tangentBlurPixels =
            (1.5f + ring * 7.0f + abs(signedWarpProfile) * 4.0f) *
            envelope;
        const float2 tangentBlur = tangentDirection * outputTexel *
            tangentBlurPixels;

        const float waveCoverage = saturate(
            (ring * 0.72f + abs(signedWarpProfile) * 0.62f +
                echo * 0.28f + interiorLens * 0.10f) *
            envelope * intensity);
        const float3 refracted = HikariRefractedScene(
            uv,
            radialWarp,
            tangentBlur,
            chromatic);
        resolved = lerp(resolved, refracted, saturate(waveCoverage * 0.94f));

        // Keep color as a restrained optical rim. The old 0.82 tint term was
        // effectively a full-screen particle circle that hid the refraction.
        const float flashEnvelope = 1.0f - smoothstep(0.0f, 0.16f, age);
        const float flash = exp(-distanceToCenter * distanceToCenter /
            max(radius * radius * 0.32f, 1.0e-5f)) *
            flashEnvelope * flashIntensity * intensity;
        const float rim = ring * envelope * intensity;
        const float3 opticalColor = lerp(1.0f.xxx, tint, 0.36f);
        resolved += opticalColor *
            (rim * 0.075f + echo * intensity * 0.035f + flash * 0.16f);
    }

    const float maskEnabled = saturate(gUser[15].y);
    if (maskEnabled > 0.5f)
    {
        uint maskWidth = 1u;
        uint maskHeight = 1u;
        gMaskTex.GetDimensions(maskWidth, maskHeight);
        const float2 maskTexel = float2(
            1.0f / max((float)maskWidth, 1.0f),
            1.0f / max((float)maskHeight, 1.0f));
        const float maximumMaskDimension = max(
            (float)maskWidth,
            (float)maskHeight);
        const float4 maskSample = gMaskTex.SampleLevel(
            gLinearClamp,
            uv,
            0.0f);
        // Contract: RG is signed UV warp, B coverage, A accumulated blur.
        const float coverage = saturate(maskSample.b);
        const float2 rawWarp = maskSample.rg;
        float bestScore = coverage *
            (0.38f + saturate(length(rawWarp) * maximumMaskDimension / 6.0f) *
                0.62f);
        float4 bestSample = maskSample;
        float expandedCoverage = coverage;

        static const float2 directions[8] =
        {
            float2(1.0f, 0.0f), float2(-1.0f, 0.0f),
            float2(0.0f, 1.0f), float2(0.0f, -1.0f),
            float2(0.70710678f, 0.70710678f),
            float2(-0.70710678f, 0.70710678f),
            float2(0.70710678f, -0.70710678f),
            float2(-0.70710678f, -0.70710678f)
        };
        [unroll]
        for (uint directionIndex = 0u; directionIndex < 8u; ++directionIndex)
        {
            const float2 direction = directions[directionIndex];
            const float4 nearSample = gMaskTex.SampleLevel(
                gLinearClamp,
                saturate(uv + direction * maskTexel * 4.0f),
                0.0f);
            HikariConsiderMaskSample(
                nearSample,
                0.82f,
                maximumMaskDimension,
                bestScore,
                bestSample,
                expandedCoverage);
            if (directionIndex < 4u)
            {
                const float4 farSample = gMaskTex.SampleLevel(
                    gLinearClamp,
                    saturate(uv + direction * maskTexel * 14.0f),
                    0.0f);
                HikariConsiderMaskSample(
                    farSample,
                    0.46f,
                    maximumMaskDimension,
                    bestScore,
                    bestSample,
                    expandedCoverage);
            }
        }

        const float bestCoverage = saturate(bestSample.b);
        const float2 bestWarp = bestSample.rg;
        const float warpLength = length(bestWarp);
        float2 normal = warpLength > 1.0e-6f
            ? bestWarp / warpLength
            : float2(0.0f, 1.0f);
        const float coverageRight = gMaskTex.SampleLevel(
            gLinearClamp,
            saturate(uv + float2(maskTexel.x * 3.0f, 0.0f)),
            0.0f).b;
        const float coverageLeft = gMaskTex.SampleLevel(
            gLinearClamp,
            saturate(uv - float2(maskTexel.x * 3.0f, 0.0f)),
            0.0f).b;
        const float coverageDown = gMaskTex.SampleLevel(
            gLinearClamp,
            saturate(uv + float2(0.0f, maskTexel.y * 3.0f)),
            0.0f).b;
        const float coverageUp = gMaskTex.SampleLevel(
            gLinearClamp,
            saturate(uv - float2(0.0f, maskTexel.y * 3.0f)),
            0.0f).b;
        const float2 coverageGradient = float2(
            coverageRight - coverageLeft,
            coverageDown - coverageUp);
        if (warpLength <= 1.0e-6f &&
            dot(coverageGradient, coverageGradient) > 1.0e-8f)
        {
            normal = normalize(coverageGradient);
        }
        const float2 tangent = float2(-normal.y, normal.x);
        const float blurHint = saturate(
            bestSample.a / max(coverage, max(bestCoverage, 1.0e-4f)));
        const float maskStrength = clamp(gUser[15].z, 0.0f, 4.0f);
        const float blurScale = clamp(gUser[15].w, 0.05f, 8.0f);
        const float extensionAttenuation = lerp(
            0.58f,
            1.0f,
            saturate(coverage / max(expandedCoverage, 1.0e-4f)));
        const float2 warp = clamp(
            bestWarp * maskStrength * extensionAttenuation,
            float2(-0.12f, -0.12f),
            float2(0.12f, 0.12f));
        const float2 tangentTap = tangent * outputTexel *
            (4.0f + blurHint * 24.0f) * blurScale;
        const float2 normalTap = normal * outputTexel *
            (1.2f + blurHint * 4.0f) * sqrt(blurScale);

        const float3 displaced = HikariScene(uv + warp);
        const float3 streakNearA = HikariScene(uv + warp + tangentTap);
        const float3 streakNearB = HikariScene(uv + warp - tangentTap);
        const float3 streakFarA = HikariScene(uv + warp + tangentTap * 2.25f);
        const float3 streakFarB = HikariScene(uv + warp - tangentTap * 2.25f);
        const float3 crossA = HikariScene(uv + warp + normalTap);
        const float3 crossB = HikariScene(uv + warp - normalTap);
        float3 directionalBlur = displaced * 0.34f +
            (streakNearA + streakNearB) * 0.18f +
            (streakFarA + streakFarB) * 0.08f +
            (crossA + crossB) * 0.07f;

        const float chromaticPixels = 1.5f + blurHint * 2.5f;
        const float2 chromaticOffset = normal * outputTexel * chromaticPixels;
        directionalBlur.r = HikariScene(uv + warp + chromaticOffset).r;
        directionalBlur.b = HikariScene(uv + warp - chromaticOffset).b;
        const float gradientMagnitude = saturate(length(coverageGradient) * 4.0f);
        const float tearEnergy = saturate(
            length(bestWarp) * maximumMaskDimension / 12.0f);
        const float maskWeight = saturate(
            (coverage * 0.82f + expandedCoverage * 0.72f) *
            maskStrength * (0.72f + blurHint * 0.28f));
        resolved = lerp(resolved, directionalBlur, saturate(maskWeight * 0.96f));

        // A restrained neutral rim makes refraction readable against flat sky
        // or fog without turning the trail back into a painted blue ribbon.
        const float airRim = saturate(
            gradientMagnitude * 0.12f +
            tearEnergy * expandedCoverage * 0.055f +
            coverage * blurHint * 0.018f);
        resolved += float3(0.78f, 0.92f, 1.05f) * airRim;
    }

    if (!all(isfinite(resolved)))
    {
        resolved = HikariFiniteColor(base.rgb);
    }
    return float4(max(resolved, 0.0f.xxx), base.a);
}
