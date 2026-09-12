#ifndef HIKARI_STYLIZED_MATERIAL_INCLUDED
#define HIKARI_STYLIZED_MATERIAL_INCLUDED

struct HikariStylizedDirectLobes
{
    float3 diffuse;
    float3 specular;
};

#include "HIKARI_StylizedEyeMaterial.hlsli"

HikariStylizedDirectLobes HikariEvaluatePbrDirectLobes(
    HikariPbrShadingContext context,
    float3 lightDirection,
    float3 radiance)
{
    const HikariDirectPbrLobes physical = HikariEvaluateDirectPbrLobes(context, lightDirection, radiance);
    HikariStylizedDirectLobes lobes;
    lobes.diffuse = physical.diffuse;
    lobes.specular = physical.specular;
    return lobes;
}

float3 HikariEvaluateAnalyticRamp(
    HikariMeshMaterialData materialData,
    float ndotl)
{
    const float4 ramp = HikariLoadMaterialProgramParameter(
        materialData,
        0u,
        float4(0.50f, 0.08f, 0.72f, 0.50f));
    const float3 shadowTint = HikariLoadMaterialProgramParameter(
        materialData,
        1u,
        float4(0.56f, 0.62f, 0.78f, 1.0f)).rgb;
    const float3 midTint = HikariLoadMaterialProgramParameter(
        materialData,
        2u,
        float4(0.82f, 0.86f, 0.94f, 1.0f)).rgb;
    const float3 litTint = HikariLoadMaterialProgramParameter(
        materialData,
        3u,
        1.0f.xxxx).rgb;
    const float softness = max(0.0001f, abs(ramp.y));
    const float shadowToMid = smoothstep(
        ramp.x - softness,
        ramp.x + softness,
        ndotl);
    const float midToLit = smoothstep(
        ramp.z - softness,
        ramp.z + softness,
        ndotl);
    return lerp(lerp(shadowTint, midTint, shadowToMid), litTint, midToLit);
}

float HikariEvaluateKajiyaKayLobe(
    float3 strandTangent,
    float3 lightDirection,
    float3 viewDirection,
    float exponent)
{
    const float tangentLight = dot(strandTangent, lightDirection);
    const float tangentView = dot(strandTangent, viewDirection);
    const float sineLight = sqrt(saturate(
        1.0f - tangentLight * tangentLight));
    const float sineView = sqrt(saturate(
        1.0f - tangentView * tangentView));
    const float longitudinal = saturate(
        tangentLight * tangentView + sineLight * sineView);
    return pow(longitudinal, max(1.0f, exponent));
}

float HikariEvaluateFaceSdf(
    HikariMeshMaterialData materialData,
    float2 uv,
    float3 lightDirection,
    float3 normal,
    float4 tangentWS,
    float3 characterRightWS,
    float3 characterForwardWS,
    float analyticFallback,
    bool frontFace)
{
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_FACE_SDF) == 0u)
    {
        return analyticFallback;
    }
    const uint descriptorIndex = HikariLoadMaterialProgramResource(
        materialData,
        HIKARI_PROGRAM_RESOURCE_FACE_SDF);
    if (descriptorIndex == HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX)
    {
        return analyticFallback;
    }
    const float3 localRight = HikariLoadMaterialProgramParameter(
        materialData,
        17u,
        float4(1.0f, 0.0f, 0.0f, 0.0f)).xyz;
    const float3 localForward = HikariLoadMaterialProgramParameter(
        materialData,
        18u,
        float4(0.0f, 0.0f, 1.0f, 0.0f)).xyz;
    float3 referenceRight = characterRightWS;
    float3 referenceForward = characterForwardWS;
    if (dot(referenceRight, referenceRight) < 1.0e-5f ||
        dot(referenceForward, referenceForward) < 1.0e-5f)
    {
        // Face SDF orientation is a per-draw head basis. Falling back to a
        // pixel normal/tangent makes adjacent texels select opposite sides.
        return analyticFallback;
    }
    referenceRight = normalize(referenceRight);
    referenceForward -=
        referenceRight * dot(referenceRight, referenceForward);
    if (dot(referenceForward, referenceForward) < 1.0e-5f)
    {
        referenceForward = normal;
    }
    referenceForward = normalize(referenceForward);
    float3 referenceUp = cross(referenceForward, referenceRight);
    if (dot(referenceUp, referenceUp) < 1.0e-5f)
    {
        referenceUp = float3(0.0f, 1.0f, 0.0f);
    }
    referenceUp = normalize(referenceUp);
    float3 tangent =
        referenceRight * localRight.x +
        referenceUp * localRight.y +
        referenceForward * localRight.z;
    float3 forward =
        referenceRight * localForward.x +
        referenceUp * localForward.y +
        referenceForward * localForward.z;
    if (dot(tangent, tangent) < 1.0e-5f ||
        dot(forward, forward) < 1.0e-5f)
    {
        tangent = referenceRight;
        forward = referenceForward;
    }
    // The authored character basis can point away from the actual face
    // front (for example models authored facing -Z). Correct the basis from
    // the geometric surface normal only when the direction is clearly
    // opposed, so silhouette texels keep a stable side selection.
    if (dot(normal, forward) < -0.25f)
    {
        tangent = -tangent;
        forward = -forward;
    }
    // Double-sided back faces mirror the face basis so the SDF answers the
    // same light that the flipped shading normal sees.
    if (!frontFace)
    {
        tangent = -tangent;
        forward = -forward;
    }
    tangent = normalize(tangent);
    forward -= tangent * dot(tangent, forward);
    if (dot(forward, forward) < 1.0e-5f)
    {
        forward = referenceForward;
    }
    forward = normalize(forward);
    float3 headUp = cross(forward, tangent);
    if (dot(headUp, headUp) < 1.0e-5f)
    {
        headUp = referenceUp;
    }
    headUp = normalize(headUp);
    float3 planarLight = lightDirection -
        headUp * dot(lightDirection, headUp);
    if (dot(planarLight, planarLight) < 1.0e-5f)
    {
        planarLight = forward;
    }
    planarLight = normalize(planarLight);
    const float side = dot(planarLight, tangent);
    const float forwardLight = dot(planarLight, forward);
    float2 faceUv = uv;
    faceUv.x = side < 0.0f ? 1.0f - faceUv.x : faceUv.x;
    const float sdf = HikariSampleMaterialTexture(
        descriptorIndex,
        gLinearClamp,
        saturate(faceUv),
        0.5f.xxxx).r;
    const float4 control = HikariLoadMaterialProgramParameter(
        materialData,
        10u,
        float4(0.04f, 0.0f, 1.0f, 1.0f));
    const float threshold =
        saturate(0.5f + control.y - forwardLight * 0.5f);
    return smoothstep(
        threshold - max(0.0001f, control.x),
        threshold + max(0.0001f, control.x),
        sdf);
}

float3 HikariResolveRampColor(
    HikariMeshMaterialData materialData,
    float2 uv,
    float ndotl,
    float faceLight,
    float rampMask)
{
    const float3 analytic = HikariEvaluateAnalyticRamp(
        materialData,
        lerp(
            ndotl,
            faceLight,
            (materialData.instanceFlags & HIKARI_PROGRAM_FLAG_FACE_SDF) != 0u
                ? 1.0f
                : 0.0f));
    const float rampCoordinate = saturate(
        lerp(ndotl, faceLight,
            (materialData.instanceFlags & HIKARI_PROGRAM_FLAG_FACE_SDF) != 0u));
    const float4 rampControl = HikariLoadMaterialProgramParameter(
        materialData,
        16u,
        float4(0.50f, 0.0f, 0.50f, 1.0f));
    const uint ramp0 = HikariLoadMaterialProgramResource(
        materialData,
        HIKARI_PROGRAM_RESOURCE_RAMP0);
    float3 color = analytic;
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_MULTI_RAMP) == 0u)
    {
        color = HikariSampleMaterialTexture(
            ramp0,
            gLinearClamp,
            float2(rampCoordinate, saturate(rampControl.x)),
            float4(analytic, 1.0f)).rgb;
    }
    else
    {
        const uint rampDescriptors[4] =
        {
            ramp0,
            HikariLoadMaterialProgramResource(
                materialData, HIKARI_PROGRAM_RESOURCE_RAMP1),
            HikariLoadMaterialProgramResource(
                materialData, HIKARI_PROGRAM_RESOURCE_RAMP2),
            HikariLoadMaterialProgramResource(
                materialData, HIKARI_PROGRAM_RESOURCE_RAMP3)
        };
        float4 weights = max(HikariLoadMaterialProgramParameter(
            materialData,
            20u,
            float4(0.0f, 0.0f, 0.0f, 0.0f)), 0.0f.xxxx);
        // Material blocks authored before the four-layer stack only contain
        // RampBlend. Preserve that two-layer result until the new weights are
        // edited or initialized by the authoring factory.
        if (dot(weights, 1.0f.xxxx) <= 1.0e-5f)
        {
            weights = float4(
                1.0f - saturate(rampControl.z),
                saturate(rampControl.z),
                0.0f,
                0.0f);
        }
        float3 accumulated = 0.0f.xxx;
        float accumulatedWeight = 0.0f;
        [unroll]
        for (uint layer = 0u; layer < 4u; ++layer)
        {
            if (rampDescriptors[layer] ==
                    HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX ||
                weights[layer] <= 0.0f)
            {
                continue;
            }
            accumulated += HikariSampleMaterialTexture(
                rampDescriptors[layer],
                gLinearClamp,
                float2(rampCoordinate, saturate(rampControl.x)),
                float4(analytic, 1.0f)).rgb * weights[layer];
            accumulatedWeight += weights[layer];
        }
        color = accumulatedWeight > 1.0e-5f
            ? accumulated / accumulatedWeight
            : analytic;
    }
    const float maskWeight = lerp(1.0f, saturate(rampMask),
                                  saturate(rampControl.y));
    return lerp(1.0f.xxx, color, saturate(rampControl.w) * maskWeight);
}

HikariStylizedDirectLobes HikariEvaluateStylizedDirectLobes(
    HikariMeshMaterialData materialData,
    HikariMaterialSurfaceData surface,
    HikariPbrShadingContext pbrContext,
    float3 lightDirection,
    float3 radiance,
    float4 tangentWS,
    float3 characterRightWS,
    float3 characterForwardWS,
    float2 uv,
    float4 mask,
    bool frontFace)
{
    const HikariStylizedDirectLobes pbr = HikariEvaluatePbrDirectLobes(
        pbrContext,
        lightDirection,
        radiance);
    HikariStylizedDirectLobes lobes = pbr;
    const bool usesStylizedDiffuse =
        (materialData.instanceFlags &
            (HIKARI_PROGRAM_FLAG_RAMP_DIFFUSE |
             HIKARI_PROGRAM_FLAG_MULTI_RAMP |
             HIKARI_PROGRAM_FLAG_FACE_SDF)) != 0u;
    const bool usesStylizedSpecular =
        (materialData.instanceFlags &
            (HIKARI_PROGRAM_FLAG_SPECULAR_BAND |
             HIKARI_PROGRAM_FLAG_ANISOTROPIC_HAIR)) != 0u;
    float ndotl = saturate(dot(surface.normalWS, lightDirection));
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_TEMPLATE_SKIN) != 0u)
    {
        const float4 skin = HikariLoadMaterialProgramParameter(
            materialData,
            14u,
            float4(0.18f, 0.12f, 0.0f, 0.0f));
        ndotl = saturate((dot(surface.normalWS, lightDirection) + skin.x) /
                         max(1.0f + skin.x, 0.0001f));
    }
    if (usesStylizedDiffuse)
    {
        const float faceLight = HikariEvaluateFaceSdf(
            materialData,
            uv,
            lightDirection,
            surface.geometricNormalWS,
            tangentWS,
            characterRightWS,
            characterForwardWS,
            ndotl,
            frontFace);
        const float3 rampColor = HikariResolveRampColor(
            materialData,
            uv,
            ndotl,
            faceLight,
            mask.r);
        const float directGain = HikariLoadMaterialProgramParameter(
            materialData,
            0u,
            float4(0.50f, 0.08f, 0.72f, 1.0f)).w;
        lobes.diffuse = surface.baseColor * rampColor * radiance *
            max(0.0f, directGain) / HIKARI_PI;
    }
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_TEMPLATE_SKIN) != 0u)
    {
        const float4 skin = HikariLoadMaterialProgramParameter(
            materialData,
            14u,
            float4(0.18f, 0.12f, 0.0f, 0.0f));
        const float3 scatterTint = HikariLoadMaterialProgramParameter(
            materialData,
            15u,
            float4(1.0f, 0.42f, 0.32f, 1.0f)).rgb;
        const float backLight = saturate(
            -dot(surface.normalWS, lightDirection));
        lobes.diffuse += surface.baseColor * scatterTint * radiance *
            (backLight * skin.y * mask.g);
    }
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_HAIR_SHADOW) != 0u)
    {
        const uint hairShadowDescriptor = HikariLoadMaterialProgramResource(
            materialData,
            HIKARI_PROGRAM_RESOURCE_HAIR_SHADOW);
        if (hairShadowDescriptor != HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX)
        {
            // R is light visibility: 1 keeps the upstream diffuse response,
            // 0 applies the authored hair shadow. The compiler guarantees the
            // texture exists whenever this connected node is active.
            const float visibility = HikariSampleMaterialTexture(
                hairShadowDescriptor,
                gLinearClamp,
                uv,
                1.0f.xxxx).r;
            const float strength = saturate(HikariLoadMaterialProgramParameter(
                materialData,
                10u,
                float4(0.04f, 0.0f, 0.65f, 0.0f)).z);
            lobes.diffuse *= lerp(1.0f, visibility, strength);
        }
    }

    if (usesStylizedSpecular)
    {
        lobes.specular = 0.0f.xxx;
    }
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_SPECULAR_BAND) != 0u)
    {
        const float4 control = HikariLoadMaterialProgramParameter(
            materialData,
            4u,
            float4(0.10f, 0.025f, 1.0f, 64.0f));
        const float3 halfVector = normalize(surface.viewWS + lightDirection);
        const float highlight = pow(
            saturate(dot(surface.normalWS, halfVector)),
            max(1.0f, control.w));
        const float threshold = saturate(1.0f - control.x);
        const float band = smoothstep(
            threshold - max(0.0001f, control.y),
            threshold + max(0.0001f, control.y),
            highlight);
        const float4 tint = HikariLoadMaterialProgramParameter(
            materialData,
            5u,
            1.0f.xxxx);
        lobes.specular = tint.rgb * tint.w * control.z *
            band * radiance * mask.g;
    }

    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_ANISOTROPIC_HAIR) != 0u)
    {
        const float4 hair = HikariLoadMaterialProgramParameter(
            materialData,
            9u,
            float4(0.08f, 0.18f, -0.10f, 1.0f));
        const float4 hairLobes = HikariLoadMaterialProgramParameter(
            materialData,
            21u,
            float4(96.0f, 48.0f, 0.45f, 1.0f));
        float3 tangent = tangentWS.xyz;
        if (dot(tangent, tangent) < 1.0e-5f)
        {
            tangent = cross(float3(0.0f, 1.0f, 0.0f), surface.normalWS);
        }
        if (dot(tangent, tangent) < 1.0e-5f)
        {
            tangent = float3(1.0f, 0.0f, 0.0f);
        }
        tangent = normalize(tangent);
        const float3 bitangent = normalize(
            cross(surface.normalWS, tangent) * tangentWS.w);
        const uint hairDescriptor = HikariLoadMaterialProgramResource(
            materialData,
            HIKARI_PROGRAM_RESOURCE_HAIR);
        float hairMask = 1.0f;
        float mapShift = 0.0f;
        if (hairDescriptor != HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX)
        {
            const float4 hairControl = HikariSampleMaterialTexture(
                hairDescriptor,
                gLinearWrap,
                uv,
                float4(0.5f, 0.5f, 1.0f, 0.5f));
            if ((materialData.instanceFlags &
                 HIKARI_PROGRAM_FLAG_HAIR_CONTROL_MAP) != 0u)
            {
                const float2 flow = hairControl.rg * 2.0f - 1.0f;
                if (dot(flow, flow) > 1.0e-4f)
                {
                    const float3 mappedTangent = normalize(
                        tangent * flow.x + bitangent * flow.y);
                    tangent = normalize(lerp(
                        tangent,
                        mappedTangent,
                        saturate(hairLobes.w)));
                }
                hairMask = hairControl.b;
                mapShift = (hairControl.a * 2.0f - 1.0f) * 0.25f;
            }
            else
            {
                hairMask = hairControl.r;
            }
        }
        const float roughnessScale = lerp(
            1.35f,
            0.35f,
            saturate(hair.y));
        const float flowShift = mapShift * saturate(hairLobes.w);
        const float3 primaryTangent = normalize(
            tangent + surface.normalWS * (hair.x + flowShift));
        const float3 secondaryTangent = normalize(
            tangent + surface.normalWS * (hair.z + flowShift));
        // Classic Kajiya-Kay longitudinal response. Using T.L and T.V keeps
        // the result invariant when a mesh stores the same strand direction
        // with the opposite tangent sign.
        const float primary = HikariEvaluateKajiyaKayLobe(
            primaryTangent,
            lightDirection,
            surface.viewWS,
            hairLobes.x * roughnessScale);
        const float secondary = HikariEvaluateKajiyaKayLobe(
            secondaryTangent,
            lightDirection,
            surface.viewWS,
            hairLobes.y * roughnessScale);
        const float3 hairTint = HikariLoadMaterialProgramParameter(
            materialData,
            5u,
            1.0f.xxxx).rgb;
        lobes.specular += hairTint * radiance * hairMask * hair.w *
            (primary + max(0.0f, hairLobes.z) * secondary);
    }

    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_TEMPLATE_HYBRID) != 0u)
    {
        const float4 blend = HikariLoadMaterialProgramParameter(
            materialData,
            12u,
            float4(0.25f, 0.35f, 0.65f, 0.25f));
        lobes.diffuse = lerp(lobes.diffuse, pbr.diffuse, saturate(blend.x));
        lobes.specular = lerp(lobes.specular, pbr.specular, saturate(blend.y));
    }
    return lobes;
}

float3 HikariEvaluateStylizedViewAdditive(
    HikariMeshMaterialData materialData,
    HikariMaterialSurfaceData surface,
    float4 tangentWS,
    float2 uv,
    float4 mask,
    float3 directionalLight)
{
    float3 additive = 0.0f.xxx;
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_FRESNEL_RIM) != 0u)
    {
        const float4 color = HikariLoadMaterialProgramParameter(
            materialData,
            6u,
            float4(0.55f, 0.68f, 1.0f, 1.0f));
        const float4 control = HikariLoadMaterialProgramParameter(
            materialData,
            7u,
            float4(4.0f, 0.22f, 1.0f, 0.0f));
        const float viewEdge =
            1.0f - saturate(dot(surface.normalWS, surface.viewWS));
        const float width = max(saturate(control.y), 0.001f);
        // Width owns the visible silhouette band. Power shapes that already
        // selected band; applying Power before the threshold collapsed the
        // default 0.22 width into only a few edge pixels.
        const float band = smoothstep(1.0f - width, 1.0f, viewEdge);
        const float rim = pow(
            saturate(band),
            max(0.1f, control.x * 0.25f));
        const float lightAlignment = lerp(
            1.0f,
            saturate(dot(surface.normalWS, directionalLight)),
            saturate(control.z));
        additive += color.rgb * color.w * rim * lightAlignment * mask.b;
    }
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_MATCAP) != 0u)
    {
        float3 matCapNormal = surface.normalWS;
        const float4 matCapControl = HikariLoadMaterialProgramParameter(
            materialData,
            25u,
            float4(1.0f, 0.0f, 0.0f, 0.0f));
        if ((materialData.instanceFlags &
             HIKARI_PROGRAM_FLAG_EYE_LIGHTING) != 0u &&
            matCapControl.y > 0.0f)
        {
            const float4 iris = HikariLoadMaterialProgramParameter(
                materialData,
                22u,
                float4(0.50f, 0.50f, 0.48f, 0.08f));
            const float2 eyeUv = HikariResolveEyeLayerUv(
                materialData,
                uv,
                surface.normalWS,
                tangentWS,
                surface.viewWS);
            const float irisMask = HikariResolveAnalyticIrisMask(
                eyeUv,
                iris);
            float2 irisPosition = (eyeUv - iris.xy) /
                max(abs(iris.z), 0.001f);
            irisPosition.y = -irisPosition.y;
            irisPosition = clamp(irisPosition, -0.999f.xx, 0.999f.xx);
            const float radialSquared = saturate(dot(
                irisPosition,
                irisPosition));
            float3 eyeTangent;
            float3 eyeBitangent;
            HikariResolveEyeTangentFrame(
                surface.normalWS,
                tangentWS,
                eyeTangent,
                eyeBitangent);
            const float3 sphericalNormal = normalize(
                eyeTangent * irisPosition.x +
                eyeBitangent * irisPosition.y +
                surface.normalWS * sqrt(1.0f - radialSquared));
            matCapNormal = normalize(lerp(
                surface.normalWS,
                sphericalNormal,
                saturate(matCapControl.y) * irisMask));
        }

        float4 cameraOriginH = mul(
            gInvViewProj,
            float4(0.0f, 0.0f, 0.0f, 1.0f));
        float4 cameraRightH = mul(
            gInvViewProj,
            float4(1.0f, 0.0f, 0.0f, 1.0f));
        float4 cameraUpH = mul(
            gInvViewProj,
            float4(0.0f, 1.0f, 0.0f, 1.0f));
        float4 cameraFarH = mul(
            gInvViewProj,
            float4(0.0f, 0.0f, 1.0f, 1.0f));
        const float3 cameraOrigin = cameraOriginH.xyz /
            max(abs(cameraOriginH.w), 0.0001f);
        const float3 cameraRight = normalize(
            cameraRightH.xyz / max(abs(cameraRightH.w), 0.0001f) -
            cameraOrigin);
        const float3 cameraUp = normalize(
            cameraUpH.xyz / max(abs(cameraUpH.w), 0.0001f) -
            cameraOrigin);
        const float3 cameraForward = normalize(
            cameraFarH.xyz / max(abs(cameraFarH.w), 0.0001f) -
            cameraOrigin);
        const float3 viewNormal = float3(
            dot(matCapNormal, cameraRight),
            dot(matCapNormal, cameraUp),
            -dot(matCapNormal, cameraForward));
        const float3 viewDirection = float3(
            dot(surface.viewWS, cameraRight),
            dot(surface.viewWS, cameraUp),
            -dot(surface.viewWS, cameraForward));
        const float2 simpleUv = float2(
            viewNormal.x * 0.5f + 0.5f,
            0.5f - viewNormal.y * 0.5f);
        const float3 correctedDetail =
            viewNormal * float3(-1.0f, -1.0f, 1.0f);
        const float3 correctedBase =
            viewDirection * float3(-1.0f, -1.0f, 1.0f) +
            float3(0.0f, 0.0f, 1.0f);
        const float correctedDenominator =
            abs(correctedBase.z) < 0.001f
                ? (correctedBase.z < 0.0f ? -0.001f : 0.001f)
                : correctedBase.z;
        const float3 correctedNormal =
            correctedBase * dot(correctedBase, correctedDetail) /
            correctedDenominator - correctedDetail;
        const float2 correctedUv = float2(
            correctedNormal.x * 0.5f + 0.5f,
            0.5f - correctedNormal.y * 0.5f);
        const float2 matCapUv = saturate(lerp(
            simpleUv,
            correctedUv,
            saturate(matCapControl.x)));
        const float4 tint = HikariLoadMaterialProgramParameter(
            materialData,
            8u,
            1.0f.xxxx);
        additive += HikariSampleMaterialProgramTexture(
            materialData,
            HIKARI_PROGRAM_RESOURCE_MATCAP,
            gLinearClamp,
            matCapUv,
            0.0f.xxxx).rgb * tint.rgb * tint.w * mask.a;
    }
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_EYE_LIGHTING) != 0u)
    {
        const float4 eyeLayer = HikariLoadMaterialProgramParameter(
            materialData,
            23u,
            float4(0.0f, 1.0f, 0.02f, 0.04f));
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
        const float4 caustic = HikariLoadMaterialProgramParameter(
            materialData,
            26u,
            float4(1.0f, 0.72f, 0.38f, 0.28f));
        const uint eyeDescriptor = HikariLoadMaterialProgramResource(
            materialData,
            HIKARI_PROGRAM_RESOURCE_EYE);
        float3 catchlightLight = directionalLight;
        if (dot(catchlightLight, catchlightLight) < 1.0e-5f)
        {
            catchlightLight = surface.normalWS;
        }
        catchlightLight = normalize(catchlightLight);
        float3 catchlightHalf = surface.viewWS + catchlightLight;
        if (dot(catchlightHalf, catchlightHalf) < 1.0e-5f)
        {
            catchlightHalf = surface.normalWS;
        }
        catchlightHalf = normalize(catchlightHalf);
        const float2 eyeUv = HikariResolveEyeLayerUv(
            materialData,
            uv,
            surface.normalWS,
            tangentWS,
            surface.viewWS);
        float2 irisPosition = (eyeUv - iris.xy) /
            max(abs(iris.z), 0.001f);
        irisPosition.y = -irisPosition.y;
        irisPosition = clamp(irisPosition, -0.999f.xx, 0.999f.xx);
        const float radialSquared = saturate(dot(
            irisPosition,
            irisPosition));
        float3 eyeTangent;
        float3 eyeBitangent;
        HikariResolveEyeTangentFrame(
            surface.normalWS,
            tangentWS,
            eyeTangent,
            eyeBitangent);
        const float3 eyeNormal = normalize(
            eyeTangent * irisPosition.x +
            eyeBitangent * irisPosition.y +
            surface.normalWS * sqrt(1.0f - radialSquared));
        const float sparkle = pow(
            saturate(dot(eyeNormal, catchlightHalf)),
            max(1.0f, eye.z));
        if (eyeLayer.x >= 0.5f)
        {
            // Transparent Highlight mode uses the parallax-shifted base-color
            // artwork itself and must not add a second analytic catchlight.
        }
        else if (eyeDescriptor != HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX &&
            (materialData.instanceFlags &
             HIKARI_PROGRAM_FLAG_EYE_CONTROL_MAP) != 0u)
        {
            const float4 eyeControl = HikariSampleMaterialTexture(
                eyeDescriptor,
                gLinearClamp,
                eyeUv,
                0.0f.xxxx);
            const float analyticIris = HikariResolveAnalyticIrisMask(
                eyeUv,
                iris);
            const float irisMask = analyticIris * saturate(eyeControl.r);
            const float depthMask = lerp(0.35f, 1.0f, eyeControl.a);
            const float3 cornea = float3(0.72f, 0.84f, 1.0f) *
                eyeControl.g * sparkle * 0.12f;
            const float3 catchlight = 1.0f.xxx *
                eyeControl.b * sparkle;
            float2 lightUv = float2(
                dot(catchlightLight, eyeTangent),
                -dot(catchlightLight, eyeBitangent));
            lightUv /= max(length(lightUv), 0.001f);
            const float causticBand = smoothstep(
                1.0f - saturate(caustic.w),
                1.0f,
                saturate(dot(irisPosition, -lightUv))) * irisMask;
            const float3 irisLift = surface.baseColor * irisMask *
                max(appearance.z, 0.0f);
            const float3 causticLight = caustic.rgb * causticBand *
                max(appearance.w, 0.0f);
            additive += (irisLift + causticLight +
                         (cornea + catchlight) * eye.y) *
                eye.w * depthMask;
        }
        else
        {
            // Eye Catchlight, iris lift and caustic have deterministic
            // analytic defaults. A control texture specializes placement but
            // is not required to make the node visibly meaningful.
            const float irisMask = HikariResolveAnalyticIrisMask(
                eyeUv,
                iris);
            float2 lightUv = float2(
                dot(catchlightLight, eyeTangent),
                -dot(catchlightLight, eyeBitangent));
            lightUv /= max(length(lightUv), 0.001f);
            const float causticBand = smoothstep(
                1.0f - saturate(caustic.w),
                1.0f,
                saturate(dot(irisPosition, -lightUv))) * irisMask;
            additive += (surface.baseColor * irisMask *
                         max(appearance.z, 0.0f) +
                         caustic.rgb * causticBand *
                         max(appearance.w, 0.0f) +
                         1.0f.xxx * eye.y * sparkle) * eye.w;
        }
    }
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_TEMPLATE_CLOTH) != 0u)
    {
        const float4 cloth = HikariLoadMaterialProgramParameter(
            materialData,
            13u,
            float4(0.28f, 0.72f, 4.0f, 1.0f));
        const float sheenPower = max(
            0.25f,
            cloth.z * lerp(1.5f, 0.65f, saturate(cloth.y)));
        const float sheen = pow(
            1.0f - saturate(dot(surface.normalWS, surface.viewWS)),
            sheenPower);
        additive += surface.baseColor * cloth.x * cloth.w * sheen * mask.b;
    }
    return additive;
}

HikariMaterialLightingResponse HikariEvaluateStylizedMaterial(
    HikariMeshMaterialData materialData,
    HikariMaterialSurfaceData surface,
    HikariPbrShadingContext pbrContext,
    float3 worldPosition,
    float distanceToCamera,
    float2 pixelPosition,
    float4 tangentWS,
    float3 characterRightWS,
    float3 characterForwardWS,
    float2 uv,
    bool frontFace,
    float3 directionalLight,
    float3 directionalRadiance,
    float shadowFactor,
    float3 pbrIndirect)
{
    HikariMaterialLightingResponse response =
        (HikariMaterialLightingResponse)0;
    const float4 mask = HikariSampleMaterialProgramTexture(
        materialData,
        HIKARI_PROGRAM_RESOURCE_MASK,
        gLinearWrap,
        uv,
        1.0f.xxxx);
    HikariStylizedDirectLobes directional =
        HikariEvaluateStylizedDirectLobes(
            materialData,
            surface,
            pbrContext,
            directionalLight,
            directionalRadiance,
            tangentWS,
            characterRightWS,
            characterForwardWS,
            uv,
            mask,
            frontFace);
    response.directDiffuse = directional.diffuse * shadowFactor;
    response.directSpecular = directional.specular * shadowFactor;

    const uint2 cluster = HikariResolveLocalLightCluster(
        pixelPosition,
        distanceToCamera);
    [loop]
    for (uint slot = 0u; slot < cluster.y; ++slot)
    {
        const uint index =
            gHikariLocalLightClusterIndices[cluster.x + slot];
        HikariLocalLightSample lightSample;
        if (!HikariResolveLocalLightSample(
                gHikariLocalLights[index],
                worldPosition,
                lightSample))
        {
            continue;
        }
        const HikariStylizedDirectLobes local =
            HikariEvaluateStylizedDirectLobes(
                materialData,
                surface,
                pbrContext,
                lightSample.directionToLight,
                lightSample.radiance,
                tangentWS,
                characterRightWS,
                characterForwardWS,
                uv,
                mask,
                frontFace);
        response.directDiffuse += local.diffuse;
        response.directSpecular += local.specular;
    }

    const bool usesStylizedDiffuse =
        (materialData.instanceFlags &
            (HIKARI_PROGRAM_FLAG_RAMP_DIFFUSE |
             HIKARI_PROGRAM_FLAG_MULTI_RAMP |
             HIKARI_PROGRAM_FLAG_FACE_SDF)) != 0u;
    response.indirect = pbrIndirect;
    if (usesStylizedDiffuse)
    {
        const float3 indirectTint = HikariEvaluateAnalyticRamp(
            materialData,
            0.65f);
        response.indirect *= indirectTint;
    }
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_TEMPLATE_HYBRID) != 0u)
    {
        const float4 blend = HikariLoadMaterialProgramParameter(
            materialData,
            12u,
            float4(0.25f, 0.35f, 0.65f, 0.25f));
        response.indirect = lerp(
            response.indirect,
            pbrIndirect,
            saturate(blend.z));
    }
    response.additive = HikariEvaluateStylizedViewAdditive(
        materialData,
        surface,
        tangentWS,
        uv,
        mask,
        directionalLight);
    return response;
}

#endif
