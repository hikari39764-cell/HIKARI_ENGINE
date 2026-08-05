#ifndef HIKARI_STYLIZED_EYE_MATERIAL_INCLUDED
#define HIKARI_STYLIZED_EYE_MATERIAL_INCLUDED

void HikariResolveEyeTangentFrame(
    float3 normalWS,
    float4 tangentWS,
    out float3 tangent,
    out float3 bitangent)
{
    tangent = tangentWS.xyz;
    if (dot(tangent, tangent) < 1.0e-5f)
    {
        tangent = cross(float3(0.0f, 1.0f, 0.0f), normalWS);
    }
    if (dot(tangent, tangent) < 1.0e-5f)
    {
        tangent = float3(1.0f, 0.0f, 0.0f);
    }
    tangent = normalize(tangent);
    const float tangentSign = abs(tangentWS.w) > 0.5f ? tangentWS.w : 1.0f;
    bitangent = normalize(cross(normalWS, tangent) * tangentSign);
}

float HikariResolveAnalyticIrisMask(float2 uv, float4 iris)
{
    const float irisRadius = max(abs(iris.z), 0.001f);
    const float irisSoftness = clamp(abs(iris.w), 0.001f, 0.95f);
    const float radialDistance = length((uv - iris.xy) / irisRadius);
    return 1.0f - smoothstep(
        1.0f - irisSoftness,
        1.0f,
        radialDistance);
}

// Eye Layer is a bounded material feature, not a post-lighting toggle.  The
// connected RGBA control map uses A to limit parallax to the iris and moves
// the same UV used by the eye base-color sample. Without a control map a
// conservative radial mask makes the node useful for ordinary eye textures.
// Keeping the calculation in tangent space makes the authored depth
// independent of model orientation.
float2 HikariResolveEyeLayerUv(
    HikariMeshMaterialData materialData,
    float2 uv,
    float3 normalWS,
    float4 tangentWS,
    float3 viewWS)
{
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_EYE_LIGHTING) == 0u)
    {
        return uv;
    }

    float3 tangent;
    float3 bitangent;
    HikariResolveEyeTangentFrame(
        normalWS,
        tangentWS,
        tangent,
        bitangent);
    const float3 viewTS = float3(
        dot(viewWS, tangent),
        dot(viewWS, bitangent),
        dot(viewWS, normalWS));
    const float4 eye = HikariLoadMaterialProgramParameter(
        materialData,
        11u,
        float4(0.12f, 0.35f, 64.0f, 1.0f));
    const float4 iris = HikariLoadMaterialProgramParameter(
        materialData,
        22u,
        float4(0.50f, 0.50f, 0.48f, 0.08f));
    const float4 appearance = HikariLoadMaterialProgramParameter(
        materialData,
        24u,
        float4(0.10f, 2.0f, 0.18f, 0.25f));
    float parallaxMask = HikariResolveAnalyticIrisMask(uv, iris);
    if ((materialData.instanceFlags &
         HIKARI_PROGRAM_FLAG_EYE_CONTROL_MAP) != 0u)
    {
        const uint eyeDescriptor = HikariLoadMaterialProgramResource(
            materialData,
            HIKARI_PROGRAM_RESOURCE_EYE);
        if (eyeDescriptor != HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX)
        {
            parallaxMask = HikariSampleMaterialTexture(
                eyeDescriptor,
                gLinearClamp,
                uv,
                0.0f.xxxx).a;
        }
    }

    // Clamp the tangent-space slope near grazing angles. This prevents a
    // shallow eye plane from jumping across the atlas while preserving the
    // view-following displacement demonstrated by the reference workflow.
    const float2 viewSlope = clamp(
        viewTS.xy / max(abs(viewTS.z), 0.25f),
        -2.0f.xx,
        2.0f.xx);
    const float2 offset = viewSlope * max(eye.x, 0.0f) *
        max(appearance.x, 0.0f) *
        saturate(parallaxMask);
    return saturate(uv - offset);
}

// Transparent eye-highlight art is commonly authored over black. Feeding
// that RGB through ordinary source-over with a non-zero black alpha darkens
// the iris instead of layering the highlight. Eye Layer keeps the standard
// transparent PSO contract and derives a bounded coverage mask from highlight
// luminance; it does not turn the whole material into additive blending.
float4 HikariResolveEyeLayerSample(
    HikariMeshMaterialData materialData,
    float4 sampleColor)
{
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_EYE_LIGHTING) == 0u)
    {
        return sampleColor;
    }
    const float4 layer = HikariLoadMaterialProgramParameter(
        materialData,
        23u,
        float4(0.0f, 1.0f, 0.02f, 0.04f));
    if (layer.x < 0.5f &&
        (materialData.instanceFlags &
         HIKARI_PROGRAM_FLAG_CORNEA_HIGHLIGHT) == 0u)
    {
        return sampleColor;
    }
    const float highlightLuminance = max(
        sampleColor.r,
        max(sampleColor.g, sampleColor.b));
    const float threshold = saturate(layer.z);
    const float softness = max(abs(layer.w), 0.001f);
    const float inferredCoverage = smoothstep(
        threshold,
        threshold + softness,
        highlightLuminance);
    const float4 appearance = HikariLoadMaterialProgramParameter(
        materialData,
        24u,
        float4(0.10f, 2.0f, 0.18f, 0.25f));
    // Eye Highlight is a positive-radiance layer. Authored alpha still shapes
    // the art, while the luminance gate prevents dark fractional-alpha texels
    // from covering and greying the iris.
    const float authoredCoverage = saturate(sampleColor.a);
    const float coverage = authoredCoverage * inferredCoverage;
    const float highlightSignal = max(
        highlightLuminance - threshold,
        0.001f);
    sampleColor.rgb = saturate(
        (sampleColor.rgb - threshold.xxx) /
        highlightSignal) * max(appearance.y, 0.0f);
    sampleColor.a = saturate(layer.y) * coverage;
    return sampleColor;
}

bool HikariIsTransparentEyeHighlight(
    HikariMeshMaterialData materialData)
{
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_EYE_LIGHTING) == 0u)
    {
        return false;
    }
    const float4 layer = HikariLoadMaterialProgramParameter(
        materialData,
        23u,
        float4(0.0f, 1.0f, 0.02f, 0.04f));
    return layer.x >= 0.5f ||
        (materialData.instanceFlags &
         HIKARI_PROGRAM_FLAG_CORNEA_HIGHLIGHT) != 0u;
}

float3 HikariResolveCorneaHighlightDriver(
    float3 normalWS,
    float4 tangentWS,
    float3 viewWS,
    float3 lightWS,
    float viewInfluence,
    float lightInfluence)
{
    float3 tangent;
    float3 bitangent;
    HikariResolveEyeTangentFrame(
        normalWS,
        tangentWS,
        tangent,
        bitangent);
    if (dot(lightWS, lightWS) < 1.0e-5f)
    {
        lightWS = normalWS;
    }
    const float3 weightedDirection =
        viewWS * max(viewInfluence, 0.0f) +
        normalize(lightWS) * max(lightInfluence, 0.0f);
    const float3 driverWS = dot(weightedDirection, weightedDirection) < 1.0e-5f
        ? normalWS
        : normalize(weightedDirection);
    return float3(
        dot(driverWS, tangent),
        -dot(driverWS, bitangent),
        dot(driverWS, normalWS));
}

// Cornea Highlight projects the current material's authored Base Color
// artwork. Eye Layer retains view-parallax ownership; this optional second
// offset follows the weighted view/light half-vector and is active only in
// the explicit Light Projection mode.
float2 HikariResolveCorneaHighlightUv(
    HikariMeshMaterialData materialData,
    float2 uv,
    float3 normalWS,
    float4 tangentWS,
    float3 viewWS,
    float3 lightWS)
{
    if ((materialData.instanceFlags &
         HIKARI_PROGRAM_FLAG_CORNEA_HIGHLIGHT) == 0u)
    {
        return uv;
    }
    const float4 projection = HikariLoadMaterialProgramParameter(
        materialData,
        27u,
        float4(2.0f, 0.10f, 0.35f, 0.65f));
    if (projection.x < 1.5f)
    {
        return uv;
    }
    const float4 iris = HikariLoadMaterialProgramParameter(
        materialData,
        22u,
        float4(0.50f, 0.50f, 0.48f, 0.08f));
    const float4 response = HikariLoadMaterialProgramParameter(
        materialData,
        28u,
        float4(0.75f, 0.45f, 0.65f, 0.0f));
    const float3 driverTS = HikariResolveCorneaHighlightDriver(
        normalWS,
        tangentWS,
        viewWS,
        lightWS,
        projection.z,
        projection.w);
    const float2 sphericalDriver = driverTS.xy /
        max(abs(driverTS.z), 0.35f);
    const float2 curvedDriver = lerp(
        driverTS.xy,
        clamp(sphericalDriver, -2.0f.xx, 2.0f.xx),
        saturate(response.x));
    const float2 offset = curvedDriver *
        max(projection.y, 0.0f) * max(abs(iris.z), 0.001f);
    return saturate(uv - offset);
}

float4 HikariResolveCorneaHighlightSample(
    HikariMeshMaterialData materialData,
    float4 sampleColor,
    float2 surfaceUv,
    float3 normalWS,
    float4 tangentWS,
    float3 viewWS,
    float3 lightWS,
    float3 lightRadiance)
{
    if ((materialData.instanceFlags &
         HIKARI_PROGRAM_FLAG_CORNEA_HIGHLIGHT) == 0u)
    {
        return sampleColor;
    }
    const float4 projection = HikariLoadMaterialProgramParameter(
        materialData,
        27u,
        float4(2.0f, 0.10f, 0.35f, 0.65f));
    if (projection.x < 0.5f)
    {
        return sampleColor;
    }
    const float4 response = HikariLoadMaterialProgramParameter(
        materialData,
        28u,
        float4(0.75f, 0.45f, 0.65f, 0.0f));
    const float4 appearance = HikariLoadMaterialProgramParameter(
        materialData,
        29u,
        float4(12.0f, 0.25f, 0.0f, 0.0f));
    const float4 iris = HikariLoadMaterialProgramParameter(
        materialData,
        22u,
        float4(0.50f, 0.50f, 0.48f, 0.08f));

    float3 tangent;
    float3 bitangent;
    HikariResolveEyeTangentFrame(
        normalWS,
        tangentWS,
        tangent,
        bitangent);
    const float irisRadius = max(abs(iris.z), 0.001f);
    float2 irisPosition = (surfaceUv - iris.xy) / irisRadius;
    irisPosition.y = -irisPosition.y;
    irisPosition = clamp(irisPosition, -0.999f.xx, 0.999f.xx);
    const float radialSquared = saturate(dot(
        irisPosition,
        irisPosition));
    const float3 eyeNormal = normalize(
        tangent * irisPosition.x +
        bitangent * irisPosition.y +
        normalWS * sqrt(1.0f - radialSquared));

    if (dot(lightWS, lightWS) < 1.0e-5f)
    {
        lightWS = normalWS;
    }
    const float3 driverTS = HikariResolveCorneaHighlightDriver(
        normalWS,
        tangentWS,
        viewWS,
        lightWS,
        projection.z,
        projection.w);
    const float3 driverWS = normalize(
        tangent * driverTS.x -
        bitangent * driverTS.y +
        normalWS * driverTS.z);
    const float alignment = pow(
        saturate(dot(eyeNormal, driverWS)),
        max(1.0f, appearance.x));
    const float lightEnergy = max(
        dot(max(lightRadiance, 0.0f.xxx),
            float3(0.2126f, 0.7152f, 0.0722f)),
        0.0f);
    const float reactiveGain = max(
        saturate(response.y),
        alignment * min(lightEnergy, 4.0f));
    const float gain = lerp(
        1.0f,
        reactiveGain,
        saturate(response.z));
    const float maximumLightChannel = max(
        max(lightRadiance.r, lightRadiance.g),
        lightRadiance.b);
    const float3 lightChroma = maximumLightChannel > 1.0e-4f
        ? saturate(lightRadiance / maximumLightChannel)
        : 1.0f.xxx;
    sampleColor.rgb *= gain * lerp(
        1.0f.xxx,
        lightChroma,
        saturate(appearance.y));

    const float edgeFade = saturate(response.w);
    if (edgeFade > 1.0e-4f)
    {
        const float radialDistance = length(
            (surfaceUv - iris.xy) / irisRadius);
        sampleColor.a *= 1.0f - smoothstep(
            1.0f - edgeFade,
            1.0f,
            radialDistance);
    }
    return sampleColor;
}

#endif
