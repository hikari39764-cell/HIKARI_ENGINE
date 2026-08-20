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
SamplerState gLinearClamp : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float HikariLuma(float3 value)
{
    return dot(value, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 HikariFiniteColor(float3 value)
{
    if (!all(isfinite(value)))
    {
        return 0.0f.xxx;
    }
    return max(value, 0.0f.xxx);
}

float3 HikariSampleFiveTap(
    float2 uv,
    float2 warp,
    float chromatic)
{
    // The fallback still has to behave like a displaced lens.  The old
    // symmetric average sampled +warp and -warp equally, cancelling the
    // optical displacement into a soft blur.
    const float3 displaced = HikariFiniteColor(
        gSceneTex.SampleLevel(gLinearClamp, saturate(uv + warp), 0.0f).rgb);
    const float3 nearSample = HikariFiniteColor(
        gSceneTex.SampleLevel(
            gLinearClamp,
            saturate(uv + warp * 0.52f),
            0.0f).rgb);
    const float3 farSample = HikariFiniteColor(
        gSceneTex.SampleLevel(
            gLinearClamp,
            saturate(uv + warp * 1.38f),
            0.0f).rgb);
    const float3 shoulderA = HikariFiniteColor(
        gSceneTex.SampleLevel(
            gLinearClamp,
            saturate(uv + warp * 0.82f + float2(-warp.y, warp.x) * 0.16f),
            0.0f).rgb);
    const float3 shoulderB = HikariFiniteColor(
        gSceneTex.SampleLevel(
            gLinearClamp,
            saturate(uv + warp * 0.82f - float2(-warp.y, warp.x) * 0.16f),
            0.0f).rgb);
    const float3 fiveTap = displaced * 0.42f + nearSample * 0.18f +
        farSample * 0.16f + (shoulderA + shoulderB) * 0.12f;

    const float2 splitDirection = dot(warp, warp) > 1.0e-10f
        ? normalize(warp)
        : float2(0.0f, 1.0f);
    const float2 split = splitDirection *
        min(max(chromatic, 0.0f) * 0.52f, 0.0065f);
    const float red = HikariFiniteColor(
        gSceneTex.SampleLevel(
            gLinearClamp,
            saturate(uv + split),
            0.0f).rgb).r;
    const float green = HikariFiniteColor(
        gSceneTex.SampleLevel(
            gLinearClamp,
            saturate(uv),
            0.0f).rgb).g;
    const float blue = HikariFiniteColor(
        gSceneTex.SampleLevel(
            gLinearClamp,
            saturate(uv - split),
            0.0f).rgb).b;
    const float splitWeight = saturate(chromatic * 38.0f);
    return lerp(fiveTap, float3(red, green, blue), splitWeight);
}

float HikariRing(
    float distanceToCenter,
    float radius,
    float thickness)
{
    const float safeThickness = max(thickness, 0.0005f);
    const float ringDistance = distanceToCenter - radius;
    return exp(-ringDistance * ringDistance /
        max(safeThickness * safeThickness * 1.65f, 1.0e-7f));
}

float4 main(PSInput input) : SV_TARGET
{
    const float4 base = gSceneTex.SampleLevel(
        gLinearClamp,
        input.uv,
        0.0f);
    if (!all(isfinite(base)))
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float3 resolved = HikariFiniteColor(base.rgb);
    const float aspect = max(gResolutionX / max(gResolutionY, 1.0f), 0.1f);
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
        const float maximumRadius = max(
            gUser[index * 4u + 1u].x,
            0.01f);
        const float thickness = max(
            gUser[index * 4u + 1u].y,
            min(1.0f / max(gResolutionX, 1.0f),
                1.0f / max(gResolutionY, 1.0f)) * 0.75f);
        const float distortion = max(
            gUser[index * 4u + 1u].z,
            0.0f);
        const float intensity = max(
            gUser[index * 4u + 1u].w,
            0.0f);
        const float3 tint = max(
            gUser[index * 4u + 2u].rgb,
            0.0f.xxx);
        const float chromatic = max(
            gUser[index * 4u + 2u].w,
            0.0f);
        const float flashIntensity = max(
            gUser[index * 4u + 3u].x,
            0.0f);
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

        float2 delta = input.uv - centerUv;
        delta.x *= aspect;
        const float distanceToCenter = length(delta);
        const float2 direction = distanceToCenter > 1.0e-5f
            ? delta / distanceToCenter
            : float2(0.0f, 0.0f);

        const float growth = 1.0f - pow(1.0f - age, 2.55f);
        const float fadeIn = smoothstep(0.0f, 0.035f, age);
        const float fadeOut = 1.0f - smoothstep(0.72f, 1.0f, age);
        const float envelope = fadeIn * fadeOut;
        const float radius = maximumRadius * lerp(0.025f, 1.0f, growth);
        const float ring = HikariRing(
            distanceToCenter,
            radius,
            thickness);

        const float angle = atan2(direction.y, direction.x);
        const float organic = sin(
            angle * 11.0f * waveFrequency + age * 19.0f * waveFrequency) *
            thickness * 0.12f * waveAmplitude;
        const float distortedDistance = distanceToCenter + organic;
        const float inner = exp(-pow(
            (distortedDistance - radius + thickness * 0.62f) /
                max(thickness, 0.001f),
            2.0f) * 1.35f);
        const float outer = exp(-pow(
            (distortedDistance - radius - thickness * 0.58f) /
                max(thickness, 0.001f),
            2.0f) * 1.15f);
        const float signedWarp = clamp(
            distortion * intensity * envelope * waveAmplitude * 1.30f *
                (outer - inner * 0.96f),
            -0.12f,
            0.12f);
        const float2 uvDirection = float2(direction.x / aspect, direction.y);
        const float2 warp = uvDirection * signedWarp;

        const float waveEnergy = ring * envelope * intensity;
        const float edge = saturate(
            waveEnergy * 0.72f + abs(outer - inner) * envelope * 0.62f);
        const float flashEnvelope = 1.0f - smoothstep(0.0f, 0.16f, age);
        const float flash = exp(-distanceToCenter * distanceToCenter /
            max(radius * radius * 0.42f, 1.0e-5f)) *
            flashEnvelope * flashIntensity * intensity;

        float echo = 0.0f;
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
            const float echoEnvelope = smoothstep(
                delay,
                delay + 0.13f,
                age) *
                (1.0f - smoothstep(0.78f, 1.0f, age)) *
                exp(-waveDamping * layer * 0.45f);
            const float echoRadius = maximumRadius *
                lerp(0.08f + layer * 0.035f,
                    1.08f + layer * 0.06f,
                    1.0f - pow(1.0f - echoAge, 2.2f));
            echo += HikariRing(
                distanceToCenter,
                echoRadius,
                thickness * (1.30f + layer * 0.12f)) *
                echoEnvelope * intensity * 0.28f * waveAmplitude;
        }

        const float lensWeight = saturate(edge * 0.94f + echo * 0.28f);
        const float3 refracted = HikariSampleFiveTap(
            input.uv,
            warp,
            chromatic);
        resolved = lerp(resolved, refracted, lensWeight);
        const float3 opticalColor = lerp(1.0f.xxx, tint, 0.36f);
        resolved += opticalColor *
            (ring * envelope * intensity * 0.075f +
                echo * 0.035f + flash * 0.16f);
    }

    if (!all(isfinite(resolved)))
    {
        resolved = HikariFiniteColor(base.rgb);
    }
    return float4(max(resolved, 0.0f.xxx), base.a);
}
