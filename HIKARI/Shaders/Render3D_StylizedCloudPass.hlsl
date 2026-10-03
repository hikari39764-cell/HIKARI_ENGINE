#include "Include/Sky/HIKARI_StylizedCloudVolume.hlsli"
#include "Include/Lighting/HIKARI_VolumetricScattering.hlsli"
#include "Include/Lighting/HIKARI_CloudSolarVisibility.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"

// Private 368-byte renderer contract.  Keep field order and offsets in lock
// step with StylizedCloudStage's C++ upload record.
cbuffer HikariStylizedCloudPassCB : register(b0)
{
    float4x4 gHikariStylizedCloudInverseViewProj;       // 0
    float4x4 gHikariStylizedCloudPreviousViewProj;      // 64
    float4 gHikariStylizedCloudCameraPositionTime;      // 128
    float4 gHikariStylizedCloudPreviousCameraDeltaTime; // 144
    float4 gHikariStylizedCloudTraceSize;               // 160
    float4 gHikariStylizedCloudFullSize;                // 176
    float4 gHikariStylizedCloudLayer;                   // 192
    float4 gHikariStylizedCloudShape;                   // 208
    float4 gHikariStylizedCloudMotion;                  // 224
    float4 gHikariStylizedCloudLighting;                // 240
    float4 gHikariStylizedCloudSunDirectionIntensity;   // 256
    float4 gHikariStylizedCloudBrightColor;             // 272
    float4 gHikariStylizedCloudShadowColor;             // 288
    float4 gHikariStylizedCloudTemporal;                // 304
    float4 gHikariStylizedCloudQuality;                 // 320
    float4 gHikariStylizedCloudAmbientLighting;         // 336: ambient radiance, physical mode
    float4 gHikariStylizedCloudSunColorAnisotropy;       // 352
};

cbuffer CloudSolarVisibilityCB : register(b1) { HikariCloudSolarVisibilityData gCloudSolarField; };
RWTexture2D<float2> gCloudSolarAtlas : register(u0);

Texture3D<float4> gHikariStylizedCloudShapeVolume : register(t0);
Texture3D<float4> gHikariStylizedCloudDetailVolume : register(t1);
Texture2D<float4> gHikariStylizedCloudWeatherMap : register(t2);
Texture2D<float4> gHikariStylizedCloudCurrentColor : register(t3);
Texture2D<float> gHikariStylizedCloudCurrentDepth : register(t4);
Texture2D<float4> gHikariStylizedCloudPreviousHistoryColor : register(t5);
Texture2D<float> gHikariStylizedCloudPreviousHistoryDepth : register(t6);
Texture2D<float> gHikariStylizedCloudSceneDepth : register(t7);

SamplerState gHikariStylizedCloudLinearWrap : register(s0);
SamplerState gHikariStylizedCloudLinearClamp : register(s1);
SamplerState gHikariStylizedCloudPointClamp : register(s2);

struct HikariStylizedCloudFullscreenOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct HikariStylizedCloudMrtOutput
{
    float4 color : SV_TARGET0;
    float depth : SV_TARGET1;
};

struct HikariStylizedCloudBaseSample
{
    float valid;
    float heightFraction;
    float4 weather;
    float4 shape;
    float baseDensity;
};

float HikariStylizedCloudGetInnerRadius()
{
    return max(
        gHikariStylizedCloudLayer.z + gHikariStylizedCloudLayer.x,
        0.01f);
}

float HikariStylizedCloudGetThickness()
{
    return max(gHikariStylizedCloudLayer.y, 0.01f);
}

float3 HikariStylizedCloudPlanetCenter()
{
    // The private layer contract has no planet-center field.  HIKARI's
    // world-space cloud layer therefore uses the local tangent origin at
    // y=0: the planet center is one planet radius below that origin.  Every
    // shell and height query uses this same relative frame.
    float4 layer = gHikariStylizedCloudLayer;
    return float3(0.0f, -max(layer.z, 0.01f), 0.0f);
}

float3 HikariStylizedCloudPlanetRelativePosition(float3 worldPosition)
{
    return worldPosition - HikariStylizedCloudPlanetCenter();
}

float2 HikariStylizedCloudGetWindDirection()
{
    return HikariStylizedCloudWindDirection(
        gHikariStylizedCloudMotion.xy);
}

float2 HikariStylizedCloudGetWindOffset(
    float timeSeconds,
    float speedMultiplier)
{
    return HikariStylizedCloudGetWindDirection() *
        gHikariStylizedCloudMotion.z *
        timeSeconds *
        speedMultiplier;
}

float HikariStylizedCloudHeightAtWorldPosition(float3 worldPosition)
{
    float height = length(HikariStylizedCloudPlanetRelativePosition(worldPosition)) -
        HikariStylizedCloudGetInnerRadius();
    return height / HikariStylizedCloudGetThickness();
}

float4 HikariStylizedCloudSampleWeather(float3 worldPosition)
{
    float heightFraction = saturate(
        HikariStylizedCloudHeightAtWorldPosition(worldPosition));
    float scale = max(abs(gHikariStylizedCloudShape.z), 0.01f);
    float2 windOffset = HikariStylizedCloudGetWindOffset(
        gHikariStylizedCloudCameraPositionTime.w,
        0.42f);
    // The generated weather lattice is 32 cells wide.  scale*64 gives a
    // broad coverage cell of roughly 2*scale and keeps weather from aliasing
    // against the primary march while preserving world-anchored advection.
    float2 position = (worldPosition.xz - windOffset) /
        (scale * 64.0f);
    position += HikariStylizedCloudGetWindDirection() *
        (heightFraction * 0.13f);
    return gHikariStylizedCloudWeatherMap.SampleLevel(
        gHikariStylizedCloudLinearWrap,
        position,
        0.0f);
}

float3 HikariStylizedCloudShapeUv(float3 worldPosition)
{
    float heightFraction = saturate(
        HikariStylizedCloudHeightAtWorldPosition(worldPosition));
    float2 windOffset = HikariStylizedCloudGetWindOffset(
        gHikariStylizedCloudCameraPositionTime.w,
        1.0f);
    // Shape noise is generated with an 8-cell horizontal lattice.  Sampling
    // at scale*8 therefore makes its primary world cell approximately the
    // authored scale instead of scale/8 (which would alias a 32-step ray).
    float shapeWorldCell = max(
        abs(gHikariStylizedCloudShape.z) * 8.0f,
        0.01f);
    float2 horizontal = (worldPosition.xz - windOffset) /
        shapeWorldCell;
    return float3(
        horizontal.x,
        heightFraction * 0.65f + dot(horizontal, float2(0.017f, -0.013f)),
        horizontal.y);
}

float3 HikariStylizedCloudDetailUv(float3 worldPosition)
{
    float heightFraction = saturate(
        HikariStylizedCloudHeightAtWorldPosition(worldPosition));
    float2 windOffset = HikariStylizedCloudGetWindOffset(
        gHikariStylizedCloudCameraPositionTime.w,
        1.65f);
    float detailWorldCell = max(
        abs(gHikariStylizedCloudShape.z) * 4.2f,
        0.01f);
    float2 horizontal = (worldPosition.xz - windOffset) /
        detailWorldCell;
    return float3(
        horizontal.x,
        heightFraction * 0.90f + dot(horizontal, float2(-0.023f, 0.019f)),
        horizontal.y);
}

HikariStylizedCloudBaseSample HikariStylizedCloudSampleBase(
    float3 worldPosition)
{
    HikariStylizedCloudBaseSample result;
    result.valid = 0.0f;
    result.heightFraction = HikariStylizedCloudHeightAtWorldPosition(
        worldPosition);
    result.weather = 0.0f.xxxx;
    result.shape = 0.0f.xxxx;
    result.baseDensity = 0.0f;

    if (result.heightFraction <= 0.0f || result.heightFraction >= 1.0f)
    {
        return result;
    }

    result.weather = HikariStylizedCloudSampleWeather(worldPosition);
    result.shape = gHikariStylizedCloudShapeVolume.SampleLevel(
        gHikariStylizedCloudLinearWrap,
        HikariStylizedCloudShapeUv(worldPosition),
        0.0f);
    HikariStylizedCloudDensityTerms terms =
        HikariStylizedCloudBuildDensityTerms(
            gHikariStylizedCloudShape.x,
            1.0f,
            0.0f,
            gHikariStylizedCloudMotion.w,
            result.heightFraction,
            result.weather,
            result.shape,
            1.0f.xxxx,
            max(gHikariStylizedCloudTemporal.z, 0.25f),
            0.0f);
    result.baseDensity = terms.baseDensity;
    result.valid = 1.0f;
    return result;
}

float HikariStylizedCloudSampleFullDensity(
    float3 worldPosition,
    HikariStylizedCloudBaseSample baseSample)
{
    if (baseSample.valid < 0.5f ||
        baseSample.baseDensity <= HIKARI_CLOUD_DENSITY_THRESHOLD)
    {
        return 0.0f;
    }

    float4 detail = gHikariStylizedCloudDetailVolume.SampleLevel(
        gHikariStylizedCloudLinearWrap,
        HikariStylizedCloudDetailUv(worldPosition),
        0.0f);
    HikariStylizedCloudDensityTerms terms =
        HikariStylizedCloudBuildDensityTerms(
            gHikariStylizedCloudShape.x,
            1.0f,
            gHikariStylizedCloudShape.w,
            gHikariStylizedCloudMotion.w,
            baseSample.heightFraction,
            baseSample.weather,
            baseSample.shape,
            detail,
            max(gHikariStylizedCloudTemporal.z, 0.25f),
            max(gHikariStylizedCloudTemporal.w, 0.0f));
    return terms.density;
}

float HikariStylizedCloudDensityToExtinction(float density)
{
    const float coverageWeight = gHikariStylizedCloudAmbientLighting.w > 0.5f
        ? smoothstep(0.0f, 0.10f, saturate(gHikariStylizedCloudShape.x)) : 1.0f;
    return density * coverageWeight *
        max(gHikariStylizedCloudShape.y, 0.0f) *
        3.4f /
        HikariStylizedCloudGetThickness();
}

[numthreads(8,8,1)]
void SunTransmittanceCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint resolution=uint(gCloudSolarField.upResolution.w),steps=uint(gCloudSolarField.directionSteps.w);
    const uint2 column=threadId.xy;
    if(any(column>=resolution)) return;
    const float2 plane=(float2(column)+.5f)/float(resolution)*2-1;
    const float3 origin=gCloudSolarField.originDescriptor.xyz+(plane.x*gCloudSolarField.rightExtent.xyz+
        plane.y*gCloudSolarField.upResolution.xyz)*gCloudSolarField.rightExtent.w;
    const float3 direction=gCloudSolarField.directionSteps.xyz;
    float outerNear,outerFar,innerNear,innerFar;
    const float3 relative=HikariStylizedCloudPlanetRelativePosition(origin);
    const bool outerHit=HikariStylizedCloudRaySphereRoots(relative,direction,
        HikariStylizedCloudGetInnerRadius()+HikariStylizedCloudGetThickness(),outerNear,outerFar);
    const bool innerHit=HikariStylizedCloudRaySphereRoots(relative,direction,
        HikariStylizedCloudGetInnerRadius(),innerNear,innerFar);
    const float start=innerHit ? innerFar : outerNear;
    const bool valid=outerHit && outerFar>start;
    gCloudSolarAtlas[HikariCloudSolarAtlasPixel(column,0u,resolution)]=valid ? float2(start,outerFar) : 0.0f.xx;
    const float stepLength=valid ? (outerFar-start)/float(steps) : 0;
    float opticalDepth=0;
    [loop] for(uint step=1u;step<=steps;++step)
    {
        if(valid && opticalDepth<12.0f)
        {
            [unroll] for(uint sampleIndex=0u;sampleIndex<2u;++sampleIndex)
            {
                const float distance=outerFar-(float(step-1u)+(float(sampleIndex)+.5f)*.5f)*stepLength;
                const float3 position=origin+direction*distance;
                const HikariStylizedCloudBaseSample base=HikariStylizedCloudSampleBase(position);
                opticalDepth+=HikariStylizedCloudDensityToExtinction(HikariStylizedCloudSampleFullDensity(position,base))*stepLength*.5f;
            }
        }
        gCloudSolarAtlas[HikariCloudSolarAtlasPixel(column,step,resolution)]=float2(exp(-opticalDepth),0);
    }
}

float HikariStylizedCloudFrameJitter(uint2 pixel)
{
    int3 cell = int3(
        int(pixel.x),
        int(pixel.y),
        int((uint)max(gHikariStylizedCloudTemporal.x, 0.0f)));
    uint hash = HikariNoiseHashInt3(cell, 0x73c5a4f1u);
    float sequence = HikariNoiseHashUnit(hash);
    return (sequence - 0.5f) * 0.32f;
}

float3 HikariStylizedCloudSunDirection()
{
    return HikariStylizedCloudSafeNormalize(
        gHikariStylizedCloudSunDirectionIntensity.xyz,
        float3(0.0f, 1.0f, 0.0f));
}

float HikariStylizedCloudLightTransmittance(
    float3 worldPosition,
    float3 sunDirection)
{
    HikariStylizedCloudShellInterval interval =
        HikariStylizedCloudResolveShell(
            HikariStylizedCloudPlanetRelativePosition(worldPosition),
            sunDirection,
            gHikariStylizedCloudLayer.z,
            gHikariStylizedCloudLayer.x,
            gHikariStylizedCloudLayer.y,
            gHikariStylizedCloudAmbientLighting.w > 0.5f
                ? 2.0f * (gHikariStylizedCloudLayer.z + gHikariStylizedCloudLayer.x + gHikariStylizedCloudLayer.y)
                : min(gHikariStylizedCloudLayer.w, 12000.0f));
    if (interval.valid < 0.5f)
    {
        return 1.0f;
    }

    uint lightSteps = min(
        max((uint)round(gHikariStylizedCloudQuality.y), 1u),
        HIKARI_CLOUD_MAX_LIGHT_STEPS);
    float probeLength = interval.farDistance-interval.nearDistance;
    float stepLength = probeLength / (float)lightSteps;
    float opticalDepth = 0.0f;

    [loop]
    for (uint step = 0u; step < HIKARI_CLOUD_MAX_LIGHT_STEPS; ++step)
    {
        if (step >= lightSteps)
        {
            break;
        }
        float sampleDistance = interval.nearDistance+(float(step) + 0.5f) * stepLength;
        HikariStylizedCloudBaseSample baseSample =
            HikariStylizedCloudSampleBase(
                worldPosition + sunDirection * sampleDistance);
        if (baseSample.baseDensity <= HIKARI_CLOUD_DENSITY_THRESHOLD)
        {
            continue;
        }

        float density = HikariStylizedCloudSampleFullDensity(
            worldPosition + sunDirection * sampleDistance,
            baseSample);
        opticalDepth += HikariStylizedCloudDensityToExtinction(density) *
            stepLength;
        if (exp(-opticalDepth) <= HIKARI_CLOUD_TRANSMITTANCE_EPSILON)
        {
            break;
        }
    }

    // 物理モードは view ray と同じ消散係数。別倍率で遮蔽だけ変えない。
    return exp(-opticalDepth * (gHikariStylizedCloudAmbientLighting.w > 0.5f
        ? 1.0f : max(gHikariStylizedCloudLighting.x, 0.0f)));
}

float3 HikariStylizedCloudSampleLighting(
    float3 worldPosition,
    float3 viewDirection,
    float3 sunDirection,
    float density,
    float heightFraction)
{
    float lightTransmittance = HikariStylizedCloudLightTransmittance(
        worldPosition,
        sunDirection);
    if (gHikariStylizedCloudAmbientLighting.w > 0.5f)
        return HikariSingleScatteringSource(gHikariStylizedCloudBrightColor.rgb,
            gHikariStylizedCloudAmbientLighting.rgb,
            gHikariStylizedCloudSunColorAnisotropy.rgb*gHikariStylizedCloudSunDirectionIntensity.w,
            lightTransmittance, dot(viewDirection,sunDirection), gHikariStylizedCloudSunColorAnisotropy.w);
    float sunElevation = saturate(sunDirection.y * 0.5f + 0.5f);
    float viewToSun = saturate(dot(viewDirection, sunDirection));
    float valueBand = HikariStylizedCloudValueBand(
        lightTransmittance);
    float upperBodyLight = smoothstep(0.16f, 0.86f, heightFraction);
    float stylizedLight = saturate(
        valueBand * 0.82f +
        upperBodyLight * 0.11f +
        sunElevation * 0.07f);
    float3 baseColor = lerp(
        max(gHikariStylizedCloudShadowColor.rgb, 0.0f.xxx),
        max(gHikariStylizedCloudBrightColor.rgb, 0.0f.xxx),
        stylizedLight);
    float direct = max(gHikariStylizedCloudSunDirectionIntensity.w, 0.0f);
    // Directional-light intensity is an input to an artistic response, not a
    // raw HDR multiplier.  This preserves cloud bands when a scene uses a
    // strong sun and prevents Bloom from turning the whole layer white.
    float sunResponse = 1.0f - exp(-direct * 0.32f);
    float powder = lerp(0.82f, 1.04f, saturate(heightFraction));
    float edge = 1.0f - smoothstep(0.22f, 0.86f, density);
    float silver = pow(viewToSun, 7.0f) *
        max(gHikariStylizedCloudLighting.y, 0.0f) *
        (0.045f + 0.14f * sunElevation) *
        edge * sqrt(saturate(lightTransmittance));
    float lightAccess = lerp(0.74f, 1.0f, saturate(lightTransmittance));
    float3 radiance = baseColor *
        lerp(0.64f, 1.08f, sunResponse) *
        lightAccess * powder;
    radiance += max(gHikariStylizedCloudBrightColor.rgb, 0.0f.xxx) * silver;
    return max(radiance, 0.0f.xxx);
}

float3 HikariStylizedCloudReconstructRay(float2 uv)
{
    float4 clip = float4(
        uv.x * 2.0f - 1.0f,
        1.0f - uv.y * 2.0f,
        1.0f,
        1.0f);
    float4 world = mul(
        gHikariStylizedCloudInverseViewProj,
        clip);
    float inverseW = rcp(max(abs(world.w), HIKARI_CLOUD_EPSILON));
    return HikariStylizedCloudSafeNormalize(
        world.xyz * inverseW -
            gHikariStylizedCloudCameraPositionTime.xyz,
        float3(0.0f, 1.0f, 0.0f));
}

// Full-resolution scene depth と同じ共通 projection contract を使う。
float HikariStylizedCloudSceneDistance(float2 uv)
{
    uint width, height;
    gHikariStylizedCloudSceneDepth.GetDimensions(width, height);
    const uint2 pixel = min(uint2(saturate(uv) * float2(width, height)), uint2(width, height) - 1u);
    const float depth = gHikariStylizedCloudSceneDepth.Load(int3(pixel, 0));
    float3 world;
    return HikariReconstructWorldPosition((float2(pixel) + 0.5f) / float2(width, height), depth,
        gHikariStylizedCloudInverseViewProj, world)
        ? length(world - gHikariStylizedCloudCameraPositionTime.xyz) : 1.0e20f;
}

float2 HikariStylizedCloudProjectPreviousUv(float3 worldPosition)
{
    float4 previousClip = mul(
        gHikariStylizedCloudPreviousViewProj,
        float4(worldPosition, 1.0f));
    if (abs(previousClip.w) <= HIKARI_CLOUD_EPSILON)
    {
        return float2(-1.0f, -1.0f);
    }
    float3 previousNdc = previousClip.xyz / previousClip.w;
    return float2(
        previousNdc.x * 0.5f + 0.5f,
        0.5f - previousNdc.y * 0.5f);
}

int2 HikariStylizedCloudClampTracePixel(int2 pixel)
{
    int2 size = max(int2(gHikariStylizedCloudTraceSize.xy), int2(1, 1));
    return clamp(pixel, int2(0, 0), size - 1);
}

float4 HikariStylizedCloudLoadCurrentColor(int2 pixel)
{
    return gHikariStylizedCloudCurrentColor.Load(int3(
        HikariStylizedCloudClampTracePixel(pixel),
        0));
}

float HikariStylizedCloudLoadCurrentDepth(int2 pixel)
{
    return gHikariStylizedCloudCurrentDepth.Load(int3(
        HikariStylizedCloudClampTracePixel(pixel),
        0));
}

float4 HikariStylizedCloudClampHistoryColor(
    int2 pixel,
    float4 historyColor)
{
    // Current 3x3 neighborhood variance is the history color box.  The
    // variance clamp favors the newly traced premultiplied sample when wind,
    // camera motion, or a cloud edge invalidates old information.
    float3 mean = 0.0f.xxx;
    float3 meanSquared = 0.0f.xxx;
    float3 minimum = 65504.0f.xxx;
    float3 maximum = 0.0f.xxx;
    float alphaMean = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float4 sample = HikariStylizedCloudLoadCurrentColor(
                pixel + int2(x, y));
            mean += sample.rgb;
            meanSquared += sample.rgb * sample.rgb;
            minimum = min(minimum, sample.rgb);
            maximum = max(maximum, sample.rgb);
            alphaMean += sample.a;
        }
    }

    mean *= 1.0f / 9.0f;
    meanSquared *= 1.0f / 9.0f;
    float3 sigma = sqrt(max(meanSquared - mean * mean, 0.0f));
    float3 lower = max(minimum, mean - sigma * 1.75f - 0.015f.xxx);
    float3 upper = min(maximum, mean + sigma * 1.75f + 0.015f.xxx);
    float3 clamped = clamp(historyColor.rgb, lower, upper);
    float alphaLower = max(0.0f, alphaMean / 9.0f - 0.18f);
    float alphaUpper = min(1.0f, alphaMean / 9.0f + 0.18f);
    return float4(clamped, clamp(historyColor.a, alphaLower, alphaUpper));
}

HikariStylizedCloudMrtOutput HikariStylizedCloudEmptyMrt()
{
    HikariStylizedCloudMrtOutput output;
    output.color = 0.0f.xxxx;
    output.depth = 0.0f;
    return output;
}

HikariStylizedCloudMrtOutput HikariStylizedCloudRenderPixel(
    float2 uv,
    uint2 pixel)
{
    HikariStylizedCloudMrtOutput output =
        HikariStylizedCloudEmptyMrt();
    float3 rayDirection = HikariStylizedCloudReconstructRay(uv);
    HikariStylizedCloudShellInterval interval =
        HikariStylizedCloudResolveShell(
            HikariStylizedCloudPlanetRelativePosition(
                gHikariStylizedCloudCameraPositionTime.xyz),
            rayDirection,
            gHikariStylizedCloudLayer.z,
            gHikariStylizedCloudLayer.x,
            gHikariStylizedCloudLayer.y,
            // layer.w is a deliberate hard safety bound.  The sphere gives
            // the finite geometric interval; horizon fading handles the
            // low-elevation tail without expanding the sampling bandwidth.
            gHikariStylizedCloudLayer.w);
    if (interval.valid < 0.5f)
    {
        return output;
    }

    if (gHikariStylizedCloudAmbientLighting.w > 0.5f)
    {
        interval.farDistance = min(interval.farDistance, HikariStylizedCloudSceneDistance(uv));
        if (interval.farDistance <= interval.nearDistance) return output;
    }

    uint primarySteps = min(
        max((uint)round(gHikariStylizedCloudQuality.x), 1u),
        HIKARI_CLOUD_MAX_PRIMARY_STEPS);
    float intervalLength = interval.farDistance - interval.nearDistance;
    float stepLength = intervalLength / (float)primarySteps;
    float jitter = HikariStylizedCloudFrameJitter(pixel);
    float transmittance = 1.0f;
    float3 radiance = 0.0f.xxx;
    float firstCloudDistance = 0.0f;
    float3 sunDirection = HikariStylizedCloudSunDirection();

    [loop]
    for (uint step = 0u; step < HIKARI_CLOUD_MAX_PRIMARY_STEPS; ++step)
    {
        if (step >= primarySteps)
        {
            break;
        }

        float sampleDistance = interval.nearDistance +
            (float(step) + 0.5f + jitter) * stepLength;
        float3 worldPosition =
            gHikariStylizedCloudCameraPositionTime.xyz +
            rayDirection * sampleDistance;
        HikariStylizedCloudBaseSample baseSample =
            HikariStylizedCloudSampleBase(worldPosition);
        if (baseSample.baseDensity <= HIKARI_CLOUD_DENSITY_THRESHOLD)
        {
            continue;
        }

        float density = HikariStylizedCloudSampleFullDensity(
            worldPosition,
            baseSample);
        // Fade density inside the bounded march interval instead of fading
        // the completed ray from its clipped endpoint. This distributes the
        // safety horizon across many samples and prevents a shared geometric
        // cutoff from becoming a visible slice in the sky.
        float safetyDistance = max(gHikariStylizedCloudLayer.w, 1.0f);
        float sampleDistanceFade = 1.0f - smoothstep(
            safetyDistance * 0.72f,
            safetyDistance,
            sampleDistance);
        density *= sampleDistanceFade;
        if (density <= HIKARI_CLOUD_DENSITY_THRESHOLD)
        {
            continue;
        }

        float extinction = HikariStylizedCloudDensityToExtinction(density);
        float sampleTransmittance = exp(-extinction * stepLength);
        float sampleOpacity = saturate(1.0f - sampleTransmittance);
        if (firstCloudDistance <= 0.0f)
        {
            firstCloudDistance = sampleDistance;
        }

        float3 sampleRadiance = HikariStylizedCloudSampleLighting(
            worldPosition,
            rayDirection,
            sunDirection,
            density,
            baseSample.heightFraction);
        radiance += transmittance * sampleRadiance * sampleOpacity;
        transmittance *= sampleTransmittance;
        if (transmittance <= HIKARI_CLOUD_TRANSMITTANCE_EPSILON)
        {
            break;
        }
    }

    float opacity = saturate(1.0f - transmittance);
    // Fade only the geometrically invalid below-horizon skirt.  Treating the
    // authored value as a positive screen-space elevation erased a broad band
    // of otherwise valid clouds and produced the reported horizontal cut.
    float horizonFade = lerp(
        0.002f,
        0.080f,
        saturate(gHikariStylizedCloudLighting.z));
    const bool cameraBelowClouds = length(HikariStylizedCloudPlanetRelativePosition(
        gHikariStylizedCloudCameraPositionTime.xyz)) < HikariStylizedCloudGetInnerRadius();
    float horizonWeight = cameraBelowClouds ? smoothstep(
        -horizonFade,
        0.0f,
        rayDirection.y) : 1.0f;
    float edgeWeight = horizonWeight;
    opacity *= edgeWeight;
    radiance *= edgeWeight;
    output.color = float4(max(radiance, 0.0f.xxx), opacity);
    output.depth = opacity > 0.001f ? firstCloudDistance : 0.0f;
    return output;
}

HikariStylizedCloudFullscreenOutput FullscreenVS(uint vertexId : SV_VertexID)
{
    HikariStylizedCloudFullscreenOutput output;
    float2 position = float2(
        (vertexId << 1u) & 2u,
        vertexId & 2u);
    output.uv = position;
    output.position = float4(
        position * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f),
        1.0f,
        1.0f);
    return output;
}

HikariStylizedCloudMrtOutput RenderCloudPS(
    HikariStylizedCloudFullscreenOutput input)
{
    uint2 pixel = uint2(input.position.xy);
    return HikariStylizedCloudRenderPixel(input.uv, pixel);
}

HikariStylizedCloudMrtOutput HikariStylizedCloudReprojectPixel(
    uint2 pixel)
{
    HikariStylizedCloudMrtOutput output;
    float4 current = HikariStylizedCloudLoadCurrentColor(int2(pixel));
    float currentDepth = HikariStylizedCloudLoadCurrentDepth(int2(pixel));
    output.color = current;
    output.depth = currentDepth;

    if (gHikariStylizedCloudTemporal.y < 0.5f ||
        currentDepth <= 0.0f ||
        current.a <= 0.0001f)
    {
        return output;
    }

    float2 traceUv = (float2(pixel) + 0.5f) *
        gHikariStylizedCloudTraceSize.zw;
    float3 currentRay = HikariStylizedCloudReconstructRay(traceUv);
    float3 currentWorld =
        gHikariStylizedCloudCameraPositionTime.xyz +
        currentRay * currentDepth;
    float2 windDirection = HikariStylizedCloudGetWindDirection();
    float windDelta = gHikariStylizedCloudMotion.z *
        max(gHikariStylizedCloudPreviousCameraDeltaTime.w, 0.0f);
    float3 previousWorld = currentWorld -
        float3(windDirection.x, 0.0f, windDirection.y) * windDelta;
    float2 previousUv = HikariStylizedCloudProjectPreviousUv(previousWorld);
    if (any(previousUv < 0.0f) || any(previousUv > 1.0f))
    {
        return output;
    }

    float previousDepth = gHikariStylizedCloudPreviousHistoryDepth.SampleLevel(
        gHikariStylizedCloudLinearClamp,
        previousUv,
        0.0f);
    float expectedPreviousDepth = length(
        previousWorld - gHikariStylizedCloudPreviousCameraDeltaTime.xyz);
    float depthThreshold = max(expectedPreviousDepth * 0.14f, 2.0f);
    float depthConfidence = previousDepth > 0.0f
        ? 1.0f - saturate(
            abs(previousDepth - expectedPreviousDepth) /
            depthThreshold)
        : 0.0f;
    if (depthConfidence <= 0.0f)
    {
        return output;
    }

    float4 history = gHikariStylizedCloudPreviousHistoryColor.SampleLevel(
        gHikariStylizedCloudLinearClamp,
        previousUv,
        0.0f);
    // 動く不透明境界が ray の長さを変えた場合、古い opacity を引きずらない。
    if (abs(history.a - current.a) > 0.05f) return output;
    history = HikariStylizedCloudClampHistoryColor(
        int2(pixel),
        history);
    float historyWeight = saturate(
        gHikariStylizedCloudLighting.w * depthConfidence);
    float shapeScale = max(abs(gHikariStylizedCloudShape.z), 1.0f);
    float advectionFraction = abs(windDelta) / shapeScale;
    float motionConfidence = 1.0f - saturate(advectionFraction * 18.0f);
    historyWeight *= motionConfidence;
    output.color = lerp(current, history, historyWeight);
    output.depth = lerp(currentDepth, previousDepth, historyWeight);
    return output;
}

HikariStylizedCloudMrtOutput ReprojectCloudPS(
    HikariStylizedCloudFullscreenOutput input)
{
    return HikariStylizedCloudReprojectPixel(
        uint2(input.position.xy));
}

// current/history 共通。空の tap も透過率の補間に含め、雲の輪郭を膨張させない。
float4 HikariStylizedCloudSpatialComposite(Texture2D<float4> colors, Texture2D<float> depths,
    float2 uv, float sceneDistance)
{
    uint width, height;
    colors.GetDimensions(width, height);
    const int2 traceSize = int2(width, height);
    const float2 coordinate = uv * float2(traceSize) - 0.5f;
    const int2 basePixel = int2(floor(coordinate));
    const float2 fraction = frac(coordinate);
    const int2 referencePixel = clamp(int2(floor(coordinate + 0.5f)), 0, traceSize - 1);
    const float centerDepth = depths.Load(int3(referencePixel, 0));
    float weightSum = 0.0f;
    float4 result = 0.0f.xxxx;
    [unroll] for (int y = 0; y <= 1; ++y)
    [unroll] for (int x = 0; x <= 1; ++x)
    {
        const int2 pixel = clamp(basePixel + int2(x, y), 0, traceSize - 1);
        const float depth = depths.Load(int3(pixel, 0));
        const float4 color = colors.Load(int3(pixel, 0));
        const float tapSceneDistance = HikariStylizedCloudSceneDistance((float2(pixel) + 0.5f) / float2(traceSize));
        // 不透明面と空の境界、および別の奥行きの物体からの cloud color を混ぜない。
        if ((sceneDistance < 1.0e19f) != (tapSceneDistance < 1.0e19f)) continue;
        if (depth > 0.0f && depth >= sceneDistance) continue;
        const float2 spatial = float2(x == 0 ? 1.0f - fraction.x : fraction.x,
            y == 0 ? 1.0f - fraction.y : fraction.y);
        const float cloudWeight = centerDepth > 0.0f && depth > 0.0f
            ? exp(-abs(depth - centerDepth) / max(centerDepth * 0.10f, 12.0f)) : 1.0f;
        const float surfaceWeight = exp(-abs(tapSceneDistance - sceneDistance) / max(sceneDistance * 0.02f, 2.0f));
        const float weight = spatial.x * spatial.y * cloudWeight * surfaceWeight;
        result += color * weight;
        weightSum += weight;
    }
    return weightSum > 0.0001f ? result / weightSum : 0.0f.xxxx;
}

float4 CompositeCloudPS(HikariStylizedCloudFullscreenOutput input) : SV_TARGET
{
    const float sceneDistance = HikariStylizedCloudSceneDistance(input.uv);
    // 旧 artistic mode の背景専用動作を保持。physical mode は物体手前まで積分済み。
    if (gHikariStylizedCloudAmbientLighting.w < 0.5f && sceneDistance < 1.0e19f) return 0.0f.xxxx;
    const float4 resolved = HikariStylizedCloudSpatialComposite(
        gHikariStylizedCloudPreviousHistoryColor, gHikariStylizedCloudPreviousHistoryDepth, input.uv, sceneDistance);
    if (resolved.a > 0.0001f) return resolved;
    return HikariStylizedCloudSpatialComposite(
        gHikariStylizedCloudCurrentColor, gHikariStylizedCloudCurrentDepth, input.uv, sceneDistance);
}
