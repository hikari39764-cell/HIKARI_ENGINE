Texture2D gTex : register(t0);
SamplerState gSamp : register(s0);

cbuffer CommonParams : register(b0)
{
    float time;
    float deltaTime;
    float combo;
    float intensity;

    float resolutionX;
    float resolutionY;
    float pad0;
    float pad1;

    float4 user[16];
};

struct PS_IN
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float2 GradientHash(float2 cell)
{
    const float2 value = float2(
        dot(cell, float2(127.1, 311.7)),
        dot(cell, float2(269.5, 183.3)));
    const float2 gradient = frac(sin(value) * 43758.5453123) * 2.0 - 1.0;
    return normalize(gradient + float2(0.0001, -0.0001));
}

float GradientNoise(float2 value)
{
    const float2 cell = floor(value);
    const float2 local = frac(value);
    const float2 smoothLocal = local * local * local *
        (local * (local * 6.0 - 15.0) + 10.0);
    const float a = dot(GradientHash(cell), local);
    const float b = dot(
        GradientHash(cell + float2(1.0, 0.0)),
        local - float2(1.0, 0.0));
    const float c = dot(
        GradientHash(cell + float2(0.0, 1.0)),
        local - float2(0.0, 1.0));
    const float d = dot(
        GradientHash(cell + 1.0.xx),
        local - 1.0.xx);
    return lerp(lerp(a, b, smoothLocal.x),
        lerp(c, d, smoothLocal.x), smoothLocal.y) * 0.72 + 0.5;
}

float BurnFbm(float2 value)
{
    const float2x2 rotateA = float2x2(
        0.819152, -0.573576,
        0.573576, 0.819152);
    const float2x2 rotateB = float2x2(
        0.422618, -0.906308,
        0.906308, 0.422618);
    float result = GradientNoise(value) * 0.50;
    value = mul(rotateA, value * 2.03) + float2(13.71, -8.19);
    result += GradientNoise(value) * 0.25;
    value = mul(rotateB, value * 2.01) + float2(-5.31, 17.23);
    result += GradientNoise(value) * 0.15;
    value = mul(rotateA, value * 2.07) + float2(9.47, 3.83);
    result += GradientNoise(value) * 0.10;
    return saturate(result);
}

float BurnField(float2 screenUv, float scale)
{
    const float aspect = max(resolutionX, 1.0) / max(resolutionY, 1.0);
    float2 coordinate = screenUv - 0.5;
    coordinate.x *= aspect;
    coordinate *= scale;

    const float2 warp = float2(
        BurnFbm(coordinate * 0.48 + float2(21.3, -7.8)),
        BurnFbm(coordinate * 0.48 + float2(-12.6, 16.4))) - 0.5;
    coordinate += warp * 1.10;
    const float organic = BurnFbm(coordinate);
    const float directional =
        screenUv.x * 0.12 + (1.0 - screenUv.y) * 0.055;
    return saturate(organic * 0.91 + directional);
}

float4 main(PS_IN input) : SV_TARGET
{
    const float progress = saturate(user[14].x);
    const bool transitionIn = user[14].y > 0.5;
    const float cover = transitionIn ? 1.0 - progress : progress;
    const float scale = max(0.75, user[0].x);
    const float softness = max(0.004, user[0].y);
    const float monochrome = saturate(user[0].z);
    const float chromaticPixels = max(0.0, user[0].w);

    const float2 resolution = max(
        float2(resolutionX, resolutionY), 1.0.xx);
    // SV_POSITION remains global for the complete render target. Building the
    // field from it prevents local quad UVs or sub-pass state from imprinting
    // rectangular boundaries into the dissolve.
    const float2 screenUv = saturate(input.pos.xy / resolution);
    const float2 texel = 1.0 / resolution;
    const float gradeEnter = smoothstep(0.015, 0.10, cover);
    const float gradeExit = 1.0 - smoothstep(0.50, 0.78, cover);
    const float gradeWeight = gradeEnter * gradeExit;

    const float2 separation = float2(
        chromaticPixels * texel.x * gradeWeight,
        chromaticPixels * texel.y * 0.16 * gradeWeight);
    const float4 centre = gTex.Sample(gSamp, screenUv);
    float3 separated = centre.rgb;
    separated.r = gTex.Sample(gSamp, screenUv + separation).r;
    separated.b = gTex.Sample(gSamp, screenUv - separation).b;
    const float luminance = dot(
        separated, float3(0.2126, 0.7152, 0.0722));
    const float3 coldMonochrome =
        luminance * float3(0.91, 0.97, 1.045);
    const float3 graded = lerp(
        separated, coldMonochrome, monochrome * gradeWeight);

    const float field = BurnField(screenUv, scale);
    const float antialias = max(fwidth(field) * 1.35, 0.0015);
    const float edgeWidth = max(softness, antialias);
    const float coverMask = smoothstep(
        field - edgeWidth,
        field + edgeWidth,
        cover);
    const float edgeDistance = abs(field - cover);
    const float edgeActive = smoothstep(0.015, 0.065, cover) *
        (1.0 - smoothstep(0.93, 0.995, cover));
    const float hotCore = (1.0 - smoothstep(
        edgeWidth * 0.16,
        edgeWidth * 1.15,
        edgeDistance)) * edgeActive;
    const float outerGlow = (1.0 - smoothstep(
        edgeWidth * 0.75,
        edgeWidth * 3.5,
        edgeDistance)) * edgeActive;
    const float emberVariation = 0.82 +
        GradientNoise(screenUv * float2(37.0, 23.0) + time * 0.35) * 0.28;
    const float3 ember =
        float3(2.20, 0.34, 0.025) * hotCore * emberVariation +
        float3(1.15, 0.075, 0.008) * outerGlow * 0.34;
    const float3 covered = float3(0.0003, 0.00015, 0.0001);
    const float3 result = lerp(graded, covered, coverMask) + ember;
    return float4(result, centre.a);
}
