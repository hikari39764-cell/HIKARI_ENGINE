cbuffer WeaponTrailConstants : register(b0)
{
    float4x4 gViewProj;
    // x = unscaled time, yz = mask resolution, w = master strength.
    float4 gTimeParams;
    // xy = inverse resolution, z = minimum readable pixel width,
    // w = distortion strength.
    float4 gScreenParams;
};

struct WeaponTrailVertex
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float emissiveIntensity : TEXCOORD1;
    float flowStrength : TEXCOORD2;
    float3 tangentWorld : TEXCOORD3;
    float3 normalWorld : TEXCOORD4;
    // x = phase, y = coverage, z = blur hint, w = edge/accent hint.
    float4 flowData : TEXCOORD5;
};

struct WeaponTrailPixel
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 color : COLOR0;
    float emissiveIntensity : TEXCOORD2;
    float flowStrength : TEXCOORD3;
    float3 tangentWorld : TEXCOORD4;
    float3 normalWorld : TEXCOORD5;
    float4 flowData : TEXCOORD6;
};

WeaponTrailPixel VSMain(WeaponTrailVertex input)
{
    WeaponTrailPixel output;
    output.position = mul(gViewProj, float4(input.position, 1.0f));
    output.worldPosition = input.position;
    output.uv = input.uv;
    output.color = input.color;
    output.emissiveIntensity = input.emissiveIntensity;
    output.flowStrength = input.flowStrength;
    output.tangentWorld = input.tangentWorld;
    output.normalWorld = input.normalWorld;
    output.flowData = input.flowData;
    return output;
}

float Hash12(float2 value)
{
    float3 p = frac(float3(value.xyx) * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float SampleTearNoise(float2 uv, float time)
{
    const float cells = Hash12(floor(
        uv * float2(113.0f, 29.0f) + float2(time * 5.5f, 0.0f)));
    const float primary = sin(
        uv.x * 91.0f - time * 21.0f +
        sin(uv.x * 19.0f + uv.y * 11.0f) * 2.4f);
    const float secondary = sin(
        uv.x * 37.0f + uv.y * 23.0f + time * 12.0f);
    return saturate(
        cells * 0.42f + primary * 0.20f + secondary * 0.10f + 0.46f);
}

float2 ProjectWorldDirectionToUv(
    float3 worldPosition,
    float3 worldDirection)
{
    const float directionLengthSq = dot(worldDirection, worldDirection);
    const float3 direction = directionLengthSq > 1.0e-10f
        ? worldDirection * rsqrt(directionLengthSq)
        : float3(1.0f, 0.0f, 0.0f);
    const float4 centerClip = mul(gViewProj, float4(worldPosition, 1.0f));
    const float4 directionClip = mul(
        gViewProj,
        float4(worldPosition + direction * 0.12f, 1.0f));
    const float2 centerNdc = centerClip.xy / max(abs(centerClip.w), 1.0e-5f);
    const float2 directionNdc =
        directionClip.xy / max(abs(directionClip.w), 1.0e-5f);
    // NDC y points up while texture/screen y points down.
    return (directionNdc - centerNdc) * float2(0.5f, -0.5f);
}

float4 PSMain(WeaponTrailPixel input) : SV_TARGET
{
    const float along = saturate(input.uv.x);
    const float across = saturate(input.uv.y);
    const float signedAcross = across * 2.0f - 1.0f;
    const float acrossDerivative = max(fwidth(across), 1.0e-4f);
    const float sheetMask =
        smoothstep(0.0f, acrossDerivative * 1.8f, across) *
        smoothstep(0.0f, acrossDerivative * 1.8f, 1.0f - across);
    const float historyFade = smoothstep(0.0f, 0.10f, along);
    const float historyTail = 1.0f - smoothstep(0.82f, 1.0f, along) * 0.12f;
    const float tearNoise = SampleTearNoise(
        input.uv,
        gTimeParams.x + input.flowData.x * 0.23f);
    const float alpha = saturate(input.flowData.y) * sheetMask *
        historyFade * historyTail;
    if (alpha <= 1.0e-4f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // The service supplies a slash-path tangent. The vector mask must encode
    // displacement strictly across that path; mixing tangent displacement into
    // RG made the post blur rotate with tessellation and camera angle.
    float2 tangentUv = ProjectWorldDirectionToUv(
        input.worldPosition,
        input.tangentWorld);
    float2 authoredNormalUv = ProjectWorldDirectionToUv(
        input.worldPosition,
        input.normalWorld);
    tangentUv = dot(tangentUv, tangentUv) > 1.0e-8f
        ? normalize(tangentUv)
        : float2(1.0f, 0.0f);
    float2 normalUv = float2(-tangentUv.y, tangentUv.x);
    if (dot(authoredNormalUv, authoredNormalUv) > 1.0e-8f &&
        dot(normalUv, authoredNormalUv) < 0.0f)
    {
        normalUv *= -1.0f;
    }

    const float flowSignal = sin(
        input.flowData.x * 35.0f - gTimeParams.x *
            (11.0f + input.flowStrength * 9.0f) +
        across * 8.0f + tearNoise * 2.1f);
    const float fold = signedAcross * exp2(-abs(signedAcross) * 1.35f);
    const float centerWeight = 1.0f - saturate(abs(signedAcross));
    const float breakup =
        (tearNoise - 0.5f) * (0.18f + centerWeight * 0.16f) +
        flowSignal * (0.10f + centerWeight * 0.08f);
    const float signedDisplacement = fold * 1.34f + breakup;
    const float distortionPixels =
        (10.0f + saturate(input.flowStrength) * 30.0f) *
        signedDisplacement * alpha * gScreenParams.w;
    const float2 vectorOffset = normalUv * distortionPixels *
        gScreenParams.xy;

    const float edge = smoothstep(0.58f, 0.98f, abs(signedAcross));
    const float velocityHint = saturate(
        abs(flowSignal) * 0.35f + saturate(input.flowStrength) * 0.65f);
    const float blurHint = saturate(
        input.flowData.z * (0.62f + velocityHint * 0.38f) * alpha);
    const float colorLuma = dot(
        max(input.color.rgb, 0.0f),
        float3(0.2126f, 0.7152f, 0.0722f));
    const float accent = saturate(
        input.flowData.w * (0.48f + colorLuma * 0.12f) * alpha +
        edge * alpha * 0.24f);

    // Mask contract for the post-temporal composite:
    //   RG = signed UV refraction vector, always normal to the slash path;
    //   B  = coverage;
    //   A  = accumulated blur/accent hint.
    return float4(vectorOffset, alpha, saturate(blurHint * 0.70f + accent * 0.30f));
}
