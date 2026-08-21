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
    // x=weight, y=monochrome, z=chromatic separation in pixels,
    // w=contrast. This is frame-local and independent from radial waves.
    float4 gActionCinematicGrade;
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

float HikariGradeHash(float2 value)
{
    value = frac(value * float2(123.34f, 456.21f));
    value += dot(value, value + 45.32f);
    return frac(value.x * value.y);
}

float HikariGradeNoise(float2 value)
{
    const float2 cell = floor(value);
    const float2 local = frac(value);
    const float2 ease = local * local * (3.0f - 2.0f * local);
    const float n00 = HikariGradeHash(cell);
    const float n10 = HikariGradeHash(cell + float2(1.0f, 0.0f));
    const float n01 = HikariGradeHash(cell + float2(0.0f, 1.0f));
    const float n11 = HikariGradeHash(cell + float2(1.0f, 1.0f));
    return lerp(
        lerp(n00, n10, ease.x),
        lerp(n01, n11, ease.x),
        ease.y);
}

float HikariCinematicGradeField(float2 uv, float aspect)
{
    // Keep the field frame-stable. Animating the noise itself produces a
    // crawling dither, while advancing one threshold through a stable field
    // reads as an ink/lens dissolve even during the deliberately short entry.
    const float2 domain = float2(uv.x * aspect, uv.y);
    const float low = HikariGradeNoise(
        domain * float2(4.2f, 3.7f) + float2(1.71f, 4.13f));
    const float detail = HikariGradeNoise(
        domain * float2(10.6f, 9.4f) + float2(7.23f, 2.91f));
    const float diagonalSweep = saturate(
        uv.x * 0.58f + (1.0f - uv.y) * 0.42f);
    return saturate(low * 0.56f + detail * 0.24f +
        diagonalSweep * 0.20f);
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
        const float waveFrequency = clamp(
            gUser[index * 4u + 3u].y,
            0.05f,
            8.0f);
        const float waveAmplitude = clamp(
            gUser[index * 4u + 3u].z,
            0.0f,
            8.0f);
        const float packedDamping = max(
            gUser[index * 4u + 3u].w,
            16.0f);
        const uint profileWaveCount = min(
            (uint)max(floor(packedDamping / 16.0f), 1.0f),
            4u);
        const float waveDamping = max(
            packedDamping - (float)profileWaveCount * 16.0f,
            0.05f);

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
            sin(angular * 11.0f * waveFrequency +
                age * 19.0f * waveFrequency) * 0.55f * waveAmplitude +
            sin(angular * 23.0f * waveFrequency -
                age * 13.0f * waveFrequency) * 0.25f * waveAmplitude +
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

        float echo = 0.0f;
        float echoSign = 0.5f;
        [unroll]
        for (uint echoIndex = 1u; echoIndex <= 4u; ++echoIndex)
        {
            if (echoIndex > profileWaveCount)
            {
                continue;
            }
            const float layer = (float)(echoIndex - 1u);
            const float delay = 0.11f + layer * 0.045f;
            const float echoAge = saturate((age - delay) /
                max(1.0f - delay, 0.05f));
            const float echoRadius = maximumRadius *
                lerp(0.08f + layer * 0.035f,
                    1.08f + layer * 0.06f,
                    1.0f - pow(1.0f - echoAge, 2.2f));
            const float echoEnvelope = smoothstep(
                delay,
                delay + 0.13f,
                age) *
                (1.0f - smoothstep(0.78f, 1.0f, age)) *
                exp(-waveDamping * layer * 0.45f);
            const float echoLayer = HikariRing(
                distortedDistance,
                echoRadius,
                waveWidth * (1.30f + layer * 0.12f)) *
                echoEnvelope * waveAmplitude;
            echo += echoLayer;
            echoSign = saturate(
                (distortedDistance - echoRadius) /
                    max(waveWidth * (1.3f + layer * 0.12f), 0.0005f));
        }
        const float signedWarp = clamp(
            distortion * intensity * envelope * waveAmplitude *
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

    const float maskEnabled = gCommonPadding.x >= 0.0f ? 1.0f : 0.0f;
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
        const float maskStrength = clamp(gCommonPadding.x, 0.0f, 4.0f);
        const float blurScale = clamp(gCommonPadding.y, 0.0f, 8.0f);
        // The typed weapon profile owns blur.  Keep refraction active when
        // blur is zero, but collapse all blur/chromatic tap distances to the
        // displaced sample so the inspector can explicitly turn drag blur off.
        const float blurDistance = blurHint * blurScale;
        const float extensionAttenuation = lerp(
            0.58f,
            1.0f,
            saturate(coverage / max(expandedCoverage, 1.0e-4f)));
        const float2 warp = clamp(
            bestWarp * maskStrength * extensionAttenuation,
            float2(-0.12f, -0.12f),
            float2(0.12f, 0.12f));
        const float2 tangentTap = tangent * outputTexel *
            (blurDistance * 24.0f);
        const float2 normalTap = normal * outputTexel *
            (blurDistance * 4.0f);

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

        const float chromaticPixels = blurDistance * 2.5f;
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

    const float gradeWeight = saturate(gActionCinematicGrade.x);
    if (gradeWeight > 0.0001f)
    {
        const float monochromeStrength =
            saturate(gActionCinematicGrade.y);
        const float separationPixels =
            clamp(gActionCinematicGrade.z, 0.0f, 24.0f);
        const float contrastStrength =
            clamp(gActionCinematicGrade.w, 0.0f, 2.0f);
        const float2 safeResolution = float2(
            max(gResolutionX, 1.0f),
            max(gResolutionY, 1.0f));
        const float safeAspect = safeResolution.x / safeResolution.y;

        // A scalar full-screen fade still looks like a cut when its authored
        // entry is only a few frames long. Advance a soft threshold through a
        // stable spatial field so every intermediate frame has a readable,
        // coherent dissolve boundary. Exit naturally traverses it in reverse.
        const float gradeField = HikariCinematicGradeField(uv, safeAspect);
        const float gradeThreshold = lerp(-0.14f, 1.14f, gradeWeight);
        const float dissolveSoftness = 0.105f;
        const float localGradeWeight = smoothstep(
            gradeField - dissolveSoftness,
            gradeField + dissolveSoftness,
            gradeThreshold);
        const float dissolveBoundary =
            (1.0f - smoothstep(
                0.018f,
                0.092f,
                abs(gradeField - gradeThreshold))) *
            smoothstep(0.015f, 0.10f, gradeWeight) *
            (1.0f - smoothstep(0.90f, 0.995f, gradeWeight));

        float2 radial = uv - 0.5f.xx;
        radial.x *= safeAspect;
        const float radialLength = length(radial);
        const float2 radialDirection = radialLength > 1.0e-5f
            ? normalize(float2(radial.x / safeAspect, radial.y))
            : float2(1.0f, 0.0f);
        const float2 diagonalDirection = normalize(float2(0.86f, -0.51f));
        const float2 separationDirection = normalize(
            radialDirection * 0.62f + diagonalDirection * 0.38f);
        const float2 separationTangent = float2(
            -separationDirection.y,
            separationDirection.x);
        const float edgeWeight = smoothstep(0.05f, 0.74f, radialLength);
        const float separationScale = separationPixels *
            lerp(0.82f, 1.28f, edgeWeight);
        const float2 redOffset = separationDirection *
            separationScale / safeResolution;
        const float2 greenOffset = separationTangent *
            separationScale * 0.52f / safeResolution;
        const float2 blueOffset = -separationDirection *
            separationScale * 1.08f / safeResolution;
        const float3 redSample = HikariScene(uv + redOffset);
        const float3 greenSample = HikariScene(uv + greenOffset);
        const float3 blueSample = HikariScene(uv + blueOffset);
        const float3 luminanceWeights =
            float3(0.2126f, 0.7152f, 0.0722f);
        const float baseLuminance = dot(resolved, luminanceWeights);
        const float contrastGain = 1.0f + contrastStrength;
        const float contrastedLuminance = max(
            (baseLuminance - 0.18f) * contrastGain + 0.18f,
            0.0f);
        const float3 contrastedColor = max(
            (resolved - 0.18f.xxx) * contrastGain + 0.18f.xxx,
            0.0f.xxx);

        // Build the monochrome result from three independently displaced
        // luminance samples. The R/G/B fringe therefore survives full
        // desaturation instead of being erased by a later grayscale lerp.
        float3 separatedMonochrome = float3(
            dot(redSample, luminanceWeights),
            dot(greenSample, luminanceWeights),
            dot(blueSample, luminanceWeights));
        separatedMonochrome = max(
            (separatedMonochrome - 0.18f.xxx) * contrastGain + 0.18f.xxx,
            0.0f.xxx);
        const float separationWeight =
            saturate(separationPixels / 5.5f) *
            lerp(0.82f, 1.0f, edgeWeight);
        const float3 monochromeTarget = lerp(
            contrastedLuminance.xxx,
            separatedMonochrome,
            separationWeight);
        const float3 gradeTarget = lerp(
            contrastedColor,
            monochromeTarget,
            monochromeStrength);
        resolved = lerp(resolved, gradeTarget, localGradeWeight);

        // A short neutral-blue contact line makes the advancing boundary
        // legible on flat fog/sky without turning it into another decal ring.
        resolved += float3(0.80f, 0.91f, 1.05f) *
            dissolveBoundary * (0.010f + baseLuminance * 0.018f);
    }

    if (!all(isfinite(resolved)))
    {
        resolved = HikariFiniteColor(base.rgb);
    }
    return float4(max(resolved, 0.0f.xxx), base.a);
}
