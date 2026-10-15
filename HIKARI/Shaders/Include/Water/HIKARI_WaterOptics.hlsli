#ifndef HIKARI_WATER_OPTICS_INCLUDED
#define HIKARI_WATER_OPTICS_INCLUDED
#include "../Color/HIKARI_Luminance.hlsli"

// Shared water optical helpers.
//
// The water surface currently has no material-texture slots.  Keep this file
// deliberately analytic and bounded so it is a reliable fallback for both
// authored water profiles until texture-backed normal/flow/foam providers are
// available.  The functions only consume existing forward CBs and samplers;
// they do not change the water GPU record layout.

float HikariWaterOpticsHash21(float2 value)
{
    value = frac(value * float2(123.34f, 456.21f));
    value += dot(value, value + 45.32f);
    return frac(value.x * value.y);
}

float HikariWaterOpticsValueNoise(float2 value)
{
    const float2 cell = floor(value);
    const float2 local = frac(value);
    const float2 blend = local * local * (3.0f - 2.0f * local);
    const float n00 = HikariWaterOpticsHash21(cell);
    const float n10 = HikariWaterOpticsHash21(cell + float2(1.0f, 0.0f));
    const float n01 = HikariWaterOpticsHash21(cell + float2(0.0f, 1.0f));
    const float n11 = HikariWaterOpticsHash21(cell + float2(1.0f, 1.0f));
    return lerp(
        lerp(n00, n10, blend.x),
        lerp(n01, n11, blend.x),
        blend.y);
}

float HikariWaterOpticsLayeredNoise(float2 localXZ, float time)
{
    float noise = 0.0f;
    noise += HikariWaterOpticsValueNoise(
        localXZ * 0.34f + float2(0.08f, 0.05f) * time);
    noise += HikariWaterOpticsValueNoise(
        localXZ * 0.63f + 13.7f + float2(-0.06f, 0.09f) * time);
    noise += HikariWaterOpticsValueNoise(
        localXZ * 1.17f + 29.3f + float2(0.10f, -0.04f) * time);
    return noise * (1.0f / 3.0f);
}

// 表示上の明るさ（露出後）で滑らかに頭を抑え、絶対量へ戻す。露出に依らず同じ見た目になる。
float3 HikariWaterSoftCompressHdr(
    float3 radiance,
    float knee)
{
    const float3 display = HikariRadianceToDisplayReferred(
        max(HikariFiniteOr(radiance, 0.0f.xxx), 0.0f.xxx),
        gViewExposure);
    const float safeKnee = max(
        HikariFiniteOr(knee, 0.12f),
        0.01f);
    return HikariDisplayReferredToRadiance(
        display / (1.0f.xxx + display * safeKnee),
        gViewExposure);
}

float3 HikariWaterApplyReflectionBudget(
    float3 radiance,
    float reflectionStrength)
{
    const float gain = clamp(HikariFiniteOr(
        reflectionStrength,
        0.62f), 0.0f, 2.0f);
    // 予算は表示上の明るさで決める。
    const float3 safeRadiance = HikariRadianceToDisplayReferred(
        max(HikariFiniteOr(radiance, 0.0f.xxx), 0.0f.xxx),
        gViewExposure);
    const float peak = max(
        max(safeRadiance.r, safeRadiance.g),
        safeRadiance.b);
    // Preserve a readable HDR lobe for bloom/tone mapping while keeping
    // multiple providers bounded.  The denominator is intentionally softer
    // than the old 1:1 knee so a clear-water sun reflection does not collapse
    // below the scene-linear highlight range.
    const float budget = max(0.24f, gain * 2.40f);
    const float budgetScale = budget / max(
        budget + peak * 0.35f,
        1.0e-4f);
    return HikariDisplayReferredToRadiance(
        safeRadiance * budgetScale * gain,
        gViewExposure);
}

float HikariWaterResolveFresnel(
    float cosineIncident,
    bool realistic)
{
    const float cosine = saturate(HikariFiniteOr(
        abs(cosineIncident),
        0.0f));
    const float f0 = HikariDielectricF0FromIor(1.0f, 1.333f);
    return realistic
        ? HikariWaterExactDielectricFresnel(
            cosine,
            1.0f,
            1.333f)
        : saturate(HikariFiniteOr(
            HikariFresnelSchlick(cosine, f0.xxx, 1.0f).x,
            f0));
}

float3 HikariWaterResolveSunRadiance()
{
    const float3 directional = max(
        HikariFiniteOr(
            gDirectionalColor.rgb * max(
                HikariFiniteOr(gDirectionalIntensity, 0.0f),
                0.0f),
            0.0f.xxx),
        0.0f.xxx);
    const float3 atmosphere = max(
        HikariFiniteOr(
            gAtmosphereSunColorRadiance.rgb * max(
                HikariFiniteOr(gAtmosphereSunRadiance, 0.0f),
                0.0f) *
                saturate(HikariFiniteOr(
                    gAtmosphereOzoneParams.z,
                    1.0f)),
            0.0f.xxx),
        0.0f.xxx);
    // The directional light remains the legacy source of truth; atmosphere
    // can raise it when its observer-side, transmittance-aware solar radiance
    // is available.  The source radiance must never reach the water glint
    // unattenuated at a low solar elevation.
    return HikariWaterSoftCompressHdr(max(directional, atmosphere), 0.08f);
}

// 上向きの面が空から受ける拡散光（反照率 1 の面の出射輝度）。水中の散乱と泡の照明に使う。
float3 HikariWaterSkyAmbient()
{
    return max(
        HikariFiniteOr(
            HikariEvaluateSkyDiffuseIrradiance(float3(0.0f, 1.0f, 0.0f)),
            0.0f.xxx),
        0.0f.xxx);
}

// 泡などの拡散面の照明（空 + 太陽）。sunShadow は太陽の影。
float3 HikariWaterDiffuseLighting(float3 normal, float3 lightDirection, float sunShadow)
{
    const float3 sun = max(
        HikariFiniteOr(
            gDirectionalColor.rgb * max(HikariFiniteOr(gDirectionalIntensity, 0.0f), 0.0f),
            0.0f.xxx),
        0.0f.xxx);
    return HikariWaterSkyAmbient() +
        sun * (saturate(dot(normal, lightDirection)) * saturate(sunShadow) / 3.14159265f);
}

float3 HikariWaterResolveSkyFallback(
    float3 direction,
    float3 authoredFallback,
    float strength)
{
    const float3 safeDirection = HikariSafeNormalize(
        direction,
        float3(0.0f, 1.0f, 0.0f));
    const float3 sky = max(
        HikariFiniteOr(
            HikariSampleSkyEnvironment(safeDirection),
            0.0f.xxx),
        0.0f.xxx);
    // 作者の散乱色は空の拡散光で照らした反射率として扱う（夜は暗く、昼は明るい）。
    const float3 authored = max(
        HikariFiniteOr(authoredFallback, 0.0f.xxx),
        0.0f.xxx) * HikariWaterSkyAmbient();
    const float safeStrength = clamp(
        HikariFiniteOr(strength, 1.0f),
        0.0f,
        2.0f);
    // Never let an unavailable sky/environment descriptor turn the water
    // black.  The lit authored scattering color is a controlled final fallback.
    return min(
        max(sky * safeStrength, authored * 0.35f),
        HikariMaxSceneRadiance(gViewExposure).xxx);
}

float3 HikariWaterResolveSceneCapture(
    float3 sampledColor,
    float3 skyFallback,
    float validity,
    float confidence)
{
    const float3 safeSample = min(
        max(HikariFiniteOr(sampledColor, 0.0f.xxx), 0.0f.xxx),
        HikariMaxSceneRadiance(gViewExposure).xxx);
    const float3 safeFallback = min(
        max(HikariFiniteOr(skyFallback, 0.0f.xxx), 0.0f.xxx),
        HikariMaxSceneRadiance(gViewExposure).xxx);
    const float valid = saturate(HikariFiniteOr(validity, 0.0f));
    const float safeConfidence = saturate(HikariFiniteOr(
        confidence,
        0.75f));
    const float3 guardedSample = max(
        safeSample,
        safeFallback * (valid > 0.5f ? 0.015f : 1.0f));
    return min(
        lerp(safeFallback, guardedSample, valid * safeConfidence),
        HikariMaxSceneRadiance(gViewExposure).xxx);
}

float3 HikariWaterBlendWithSceneHorizon(
    float3 waterColor,
    float3 capturedSceneColor,
    float3 explicitSkyRadiance,
    float3 stableSurfaceNormal,
    float3 viewDirection,
    float distanceToCamera,
    float opaqueReceiverValidity)
{
    const float3 safeWater = min(
        max(HikariFiniteOr(waterColor, 0.0f.xxx), 0.0f.xxx),
        HikariMaxSceneRadiance(gViewExposure).xxx);
    const float3 safeScene = min(
        max(HikariFiniteOr(capturedSceneColor, safeWater), 0.0f.xxx),
        HikariMaxSceneRadiance(gViewExposure).xxx);
    const float3 safeSky = min(
        max(HikariFiniteOr(explicitSkyRadiance, safeWater), 0.0f.xxx),
        HikariMaxSceneRadiance(gViewExposure).xxx);
    if (saturate(HikariFiniteOr(opaqueReceiverValidity, 0.0f)) > 0.5f)
    {
        return safeWater;
    }

    // Some sky paths are composited after the opaque SceneColor snapshot. In
    // that case a finite black clear color is not a valid sky sample. Use the
    // active sky model as the authority and accept captured SceneColor only
    // when it carries a credible amount of that radiance. Texture2D
    // panoramas remain authoritative because their captured radiance passes
    // this gate; procedural/atmosphere skies cannot create a black horizon
    // merely because their snapshot is empty.
    const float capturedLuminance = HikariLuminance(safeScene);
    const float skyLuminance = HikariLuminance(safeSky);
    const float lowCaptureThreshold = skyLuminance * 0.025f;
    const float highCaptureThreshold = max(
        skyLuminance * 0.18f,
        lowCaptureThreshold + 1.0e-4f);
    const float captureConfidence = skyLuminance > 1.0e-4f
        ? smoothstep(
            lowCaptureThreshold,
            highCaptureThreshold,
            capturedLuminance)
        : 1.0f;
    const float3 horizonRadiance = lerp(
        safeSky,
        safeScene,
        captureConfidence);

    // At a distant grazing angle converge to resolved sky radiance. Nearby
    // water keeps its authored scattering, reflection and foam.
    const float3 safeNormal = HikariSafeNormalize(
        stableSurfaceNormal,
        float3(0.0f, 1.0f, 0.0f));
    const float3 safeView = HikariSafeNormalize(
        viewDirection,
        float3(0.0f, 0.0f, 1.0f));
    const float grazing = 1.0f - saturate(abs(dot(safeNormal, safeView)));
    const float angularWeight = smoothstep(0.58f, 0.96f, grazing);
    const float distanceWeight = smoothstep(
        32.0f,
        160.0f,
        max(HikariFiniteOr(distanceToCamera, 0.0f), 0.0f));
    const float horizonWeight = saturate(angularWeight * distanceWeight);
    return lerp(safeWater, horizonRadiance, horizonWeight);
}

float3 HikariWaterResolveBeerLambert(
    float3 transmittanceAtReferenceDistance,
    float referenceDistance,
    float opticalDistance)
{
    const float3 authored = clamp(
        HikariFiniteOr(transmittanceAtReferenceDistance, 1.0f.xxx),
        1.0e-4f.xxx,
        1.0f.xxx);
    const float safeReferenceDistance = max(
        HikariFiniteOr(referenceDistance, 1.0f),
        1.0e-3f);
    const float safeDistance = clamp(
        HikariFiniteOr(opticalDistance, 4.0f),
        0.0f,
        4096.0f);
    return saturate(HikariFiniteOr(
        pow(authored, (safeDistance / safeReferenceDistance).xxx),
        0.0f.xxx));
}

// scatteringColor は散乱の反照率。ambientRadiance（空の拡散光）と太陽で照らす。
float3 HikariWaterResolveSingleLayerScattering(
    float3 sceneRadiance,
    float3 scatteringColor,
    float3 ambientRadiance,
    float3 transmittance,
    float3 viewDirection,
    float3 lightDirection,
    float3 sunRadiance,
    float solarStrength,
    bool realistic)
{
    const float3 safeScene = max(
        HikariFiniteOr(sceneRadiance, 0.0f.xxx),
        0.0f.xxx);
    const float3 safeScatter = max(
        HikariFiniteOr(scatteringColor, 0.0f.xxx),
        0.0015f.xxx);
    const float3 safeTransmittance = saturate(HikariFiniteOr(
        transmittance,
        1.0f.xxx));
    const float3 opticalLayer = 1.0f.xxx - safeTransmittance;
    const float forwardCosine = saturate(dot(
        HikariSafeNormalize(viewDirection, float3(0.0f, 0.0f, 1.0f)),
        HikariSafeNormalize(lightDirection, float3(0.0f, 1.0f, 0.0f))));
    const float forwardPhase = 0.18f +
        0.82f * pow(
            forwardCosine,
            realistic ? 8.0f : 4.0f);
    const float safeSolarStrength = clamp(
        HikariFiniteOr(solarStrength, 0.0f),
        0.0f,
        2.0f);
    const float solarScale = (realistic ? 0.34f : 0.18f) *
        safeSolarStrength * forwardPhase;
    const float3 singleLayer =
        safeScene * safeTransmittance +
        safeScatter * max(HikariFiniteOr(ambientRadiance, 0.0f.xxx), 0.0f.xxx) * opticalLayer +
        max(HikariFiniteOr(sunRadiance, 0.0f.xxx), 0.0f.xxx) *
            opticalLayer * solarScale;
    return min(
        max(HikariFiniteOr(singleLayer, safeScatter), 0.0f.xxx),
        HikariMaxSceneRadiance(gViewExposure).xxx);
}

float3 HikariWaterResolveEnvironmentFallback(
    float3 providerRadiance,
    float3 reflectionDirection,
    float normalViewCosine,
    float fresnel,
    float roughness,
    float reflectionIntensity,
    float3 authoredFallback)
{
    const float3 provider = max(
        HikariFiniteOr(providerRadiance, 0.0f.xxx),
        0.0f.xxx);
    const float3 sky = HikariWaterResolveSkyFallback(
        reflectionDirection,
        authoredFallback,
        max(HikariFiniteOr(gSkyReflectionIntensity, 0.0f), 0.0f));
    const float2 envBrdf = HikariAnalyticEnvBrdf(
        saturate(HikariFiniteOr(normalViewCosine, 0.0f)),
        saturate(roughness));
    const float3 skyProvider = sky *
        saturate(HikariFiniteOr(fresnel, 0.04f)) *
        max(envBrdf.x + envBrdf.y * 0.35f, 0.12f) *
        clamp(HikariFiniteOr(reflectionIntensity, 1.0f), 0.0f, 2.0f);
    // max() is intentional: this is a replacement fallback, not another
    // additive reflection provider, so SSR/probe/sky cannot double energy.
    return min(max(provider, skyProvider), HikariMaxSceneRadiance(gViewExposure).xxx);
}

float3 HikariWaterResolveSsrReplacement(
    float3 providerRadiance,
    HikariScreenSpaceReflectionTraceResult ssr,
    float fresnel,
    float maxWeight)
{
    const float3 provider = max(
        HikariFiniteOr(providerRadiance, 0.0f.xxx),
        0.0f.xxx);
    // 範囲の検査は呼び出し側が Pre-Exposure 済みの値で済ませている。
    const bool validRadiance =
        ssr.hit != 0u &&
        HikariIsFiniteFloat3(ssr.radiance) &&
        all(ssr.radiance >= 0.0f.xxx);
    const float weight = validRadiance
        ? saturate(HikariFiniteOr(
            ssr.confidence * maxWeight,
            0.0f))
        : 0.0f;
    const float3 reflected = max(
        HikariFiniteOr(ssr.radiance, 0.0f.xxx) * saturate(fresnel),
        provider * 0.06f);
    return min(
        lerp(provider, reflected, weight),
        HikariMaxSceneRadiance(gViewExposure).xxx);
}

float3 HikariWaterResolveMicroNormal(
    float3 geometricNormal,
    float4 tangentWS,
    float2 localXZ,
    float time,
    float strength,
    bool anime,
    float windDirectionRadians,
    float windSpeed,
    float distanceToCamera)
{
    const float3 n = HikariSafeNormalize(
        geometricNormal,
        float3(0.0f, 1.0f, 0.0f));
    const float3 tangentFallbackAxis = abs(n.y) < 0.999f
        ? float3(0.0f, 1.0f, 0.0f)
        : float3(1.0f, 0.0f, 0.0f);
    const float3 tangentFallback = HikariSafeNormalize(
        cross(tangentFallbackAxis, n),
        float3(1.0f, 0.0f, 0.0f));
    float3 tangent = tangentWS.xyz - n * dot(tangentWS.xyz, n);
    tangent = HikariSafeNormalize(
        length(tangent) > 1.0e-5f ? tangent : tangentFallback,
        tangentFallback);
    const float tangentSign = HikariFiniteOr(tangentWS.w, 1.0f) < 0.0f
        ? -1.0f
        : 1.0f;
    const float3 bitangent = HikariSafeNormalize(
        cross(n, tangent) * tangentSign,
        cross(n, tangentFallback) * tangentSign);

    float sine;
    float cosine;
    const float2 flowDirection = float2(
        cos(HikariFiniteOr(windDirectionRadians, 0.0f)),
        sin(HikariFiniteOr(windDirectionRadians, 0.0f)));
    const float2 crossDirection = float2(
        -flowDirection.y,
        flowDirection.x);
    const float flowRate = 0.32f +
        clamp(HikariFiniteOr(windSpeed, 0.0f), 0.0f, 50.0f) * 0.018f;
    const float2 advected = HikariFiniteOr(localXZ, 0.0f.xx) -
        flowDirection * HikariFiniteOr(time, 0.0f) * flowRate;
    const float footprint = max(
        length(fwidth(HikariFiniteOr(localXZ, 0.0f.xx))),
        1.0e-3f);
    const float safeDistance = max(
        HikariFiniteOr(distanceToCamera, 0.0f),
        0.0f);
    const float farWeight = 1.0f - smoothstep(80.0f, 220.0f, safeDistance);

    // Two stable scales are retained at distance; the detail band is
    // footprint- and distance-filtered so a 2 m grid cannot turn it into
    // shimmer.
    const float frequency0 = anime ? 0.24f : 0.18f;
    const float frequency1 = anime ? 0.62f : 0.46f;
    const float filter0 = exp(-pow(frequency0 * footprint * 1.40f, 2.0f));
    const float filter1 = exp(-pow(frequency1 * footprint * 1.55f, 2.0f));
    const float detailDistanceWeight = lerp(0.46f, 1.0f, farWeight);
    const float phase0 = dot(advected, flowDirection) * frequency0 -
        HikariFiniteOr(time, 0.0f) * (0.28f + flowRate * 0.45f);
    const float phase1 = dot(advected, crossDirection) * frequency1 +
        dot(advected, flowDirection) * 0.17f +
        HikariFiniteOr(time, 0.0f) * (0.36f + flowRate * 0.62f);
    sincos(phase0, sine, cosine);
    float2 gradient = flowDirection * cosine * frequency0 * filter0 * 0.56f;
    sincos(phase1, sine, cosine);
    gradient += crossDirection * cosine * frequency1 * filter1 *
        detailDistanceWeight * 0.31f;

    const float amplitude = max(
        HikariFiniteOr(strength, 0.0f),
        0.0f) * (anime ? 0.105f : 0.16f);
    return HikariSafeNormalize(
        n - tangent * gradient.x * amplitude -
            bitangent * gradient.y * amplitude,
        n);
}

#endif
