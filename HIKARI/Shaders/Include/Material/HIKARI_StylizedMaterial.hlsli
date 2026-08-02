#ifndef HIKARI_STYLIZED_MATERIAL_INCLUDED
#define HIKARI_STYLIZED_MATERIAL_INCLUDED

struct HikariStylizedDirectLobes
{
    float3 diffuse;
    float3 specular;
};

HikariStylizedDirectLobes HikariEvaluatePbrDirectLobes(
    HikariPbrShadingContext context,
    float3 lightDirection,
    float3 radiance)
{
    HikariStylizedDirectLobes lobes = (HikariStylizedDirectLobes)0;
    const float ndotl = saturate(dot(context.normal, lightDirection));
    if (ndotl <= 0.0f)
    {
        return lobes;
    }
    const float3 halfVector = normalize(context.view + lightDirection);
    const float3 fresnel = HikariFresnelSchlick(
        saturate(dot(halfVector, context.view)),
        context.specularF0);
    const float distribution = HikariDistributionGGX(context, halfVector);
    const float geometry = context.geometryView *
        HikariGeometrySchlickGGX(ndotl, context.geometryK);
    lobes.specular = distribution * geometry * fresnel /
        max(4.0f * max(context.ndotView, 0.0001f) * ndotl, 0.0001f);
    lobes.diffuse = (1.0f.xxx - fresnel) *
        (1.0f - context.metallic) * context.baseColor / HIKARI_PI;
    lobes.diffuse *= radiance * ndotl;
    lobes.specular *= radiance * ndotl;
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

float HikariEvaluateFaceSdf(
    HikariMeshMaterialData materialData,
    float2 uv,
    float3 lightDirection,
    float3 normal,
    float4 tangentWS,
    float3 characterRightWS,
    float3 characterForwardWS,
    float analyticFallback)
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
        referenceRight = tangentWS.xyz;
        if (dot(referenceRight, referenceRight) < 1.0e-5f)
        {
            referenceRight = cross(
                float3(0.0f, 1.0f, 0.0f), normal);
        }
        referenceForward = cross(
            float3(0.0f, 1.0f, 0.0f), referenceRight);
        if (dot(referenceForward, referenceForward) < 1.0e-5f)
        {
            referenceForward = normal;
        }
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
    const uint ramp0 = HikariLoadMaterialProgramResource(
        materialData,
        HIKARI_PROGRAM_RESOURCE_RAMP0);
    if (ramp0 == HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX)
    {
        return analytic;
    }
    const float rampCoordinate = saturate(
        lerp(ndotl, faceLight,
            (materialData.instanceFlags & HIKARI_PROGRAM_FLAG_FACE_SDF) != 0u));
    const float4 rampControl = HikariLoadMaterialProgramParameter(
        materialData,
        16u,
        float4(0.50f, 0.0f, 0.50f, 1.0f));
    float3 color = HikariSampleMaterialTexture(
        ramp0,
        gLinearClamp,
        float2(rampCoordinate, saturate(rampControl.x)),
        float4(analytic, 1.0f)).rgb;
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_MULTI_RAMP) != 0u)
    {
        const float3 second = HikariSampleMaterialProgramTexture(
            materialData,
            HIKARI_PROGRAM_RESOURCE_RAMP1,
            gLinearClamp,
            float2(rampCoordinate, saturate(rampControl.x)),
            float4(color, 1.0f)).rgb;
        color = lerp(color, second, saturate(rampControl.z));
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
    float4 mask)
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
            surface.normalWS,
            tangentWS,
            characterRightWS,
            characterForwardWS,
            ndotl);
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
                    tangent = normalize(
                        tangent * flow.x + bitangent * flow.y);
                }
                hairMask = hairControl.b;
                mapShift = (hairControl.a * 2.0f - 1.0f) * 0.25f;
            }
            else
            {
                hairMask = hairControl.r;
            }
        }
        const float3 halfVector = normalize(surface.viewWS + lightDirection);
        const float strand = sqrt(saturate(
            1.0f - dot(tangent, halfVector) * dot(tangent, halfVector)));
        const float primary = smoothstep(
            1.0f - max(0.001f, hair.y),
            1.0f,
            saturate(strand + hair.x + mapShift));
        const float secondary = smoothstep(
            1.0f - max(0.001f, hair.y * 1.5f),
            1.0f,
            saturate(strand + hair.z + mapShift));
        const float3 hairTint = HikariLoadMaterialProgramParameter(
            materialData,
            5u,
            1.0f.xxxx).rgb;
        lobes.specular += hairTint * radiance * hairMask * hair.w *
            (primary + 0.45f * secondary);
    }

    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_TEMPLATE_HYBRID) != 0u)
    {
        const float4 blend = HikariLoadMaterialProgramParameter(
            materialData,
            12u,
            float4(0.55f, 0.55f, 1.0f, 0.25f));
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
        const float fresnel = pow(
            1.0f - saturate(dot(surface.normalWS, surface.viewWS)),
            max(0.1f, control.x));
        const float rim = smoothstep(
            1.0f - saturate(control.y),
            1.0f,
            fresnel);
        const float lightAlignment = lerp(
            1.0f,
            saturate(dot(surface.normalWS, directionalLight)),
            saturate(control.z));
        additive += color.rgb * color.w * rim * lightAlignment * mask.b;
    }
    if ((materialData.instanceFlags & HIKARI_PROGRAM_FLAG_MATCAP) != 0u)
    {
        float4 cameraOriginH = mul(gInvViewProj, float4(0.0f, 0.0f, 0.0f, 1.0f));
        float4 cameraRightH = mul(gInvViewProj, float4(1.0f, 0.0f, 0.0f, 1.0f));
        float4 cameraUpH = mul(gInvViewProj, float4(0.0f, 1.0f, 0.0f, 1.0f));
        const float3 cameraOrigin = cameraOriginH.xyz / max(abs(cameraOriginH.w), 0.0001f);
        const float3 cameraRight = normalize(
            cameraRightH.xyz / max(abs(cameraRightH.w), 0.0001f) - cameraOrigin);
        const float3 cameraUp = normalize(
            cameraUpH.xyz / max(abs(cameraUpH.w), 0.0001f) - cameraOrigin);
        const float2 matCapUv = saturate(float2(
            dot(surface.normalWS, cameraRight) * 0.5f + 0.5f,
            0.5f - dot(surface.normalWS, cameraUp) * 0.5f));
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
        const float4 eye = HikariLoadMaterialProgramParameter(
            materialData,
            11u,
            float4(0.12f, 0.35f, 64.0f, 1.0f));
        const uint eyeDescriptor = HikariLoadMaterialProgramResource(
            materialData,
            HIKARI_PROGRAM_RESOURCE_EYE);
        if (eyeDescriptor != HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX)
        {
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
        float3 bitangent = normalize(cross(surface.normalWS, tangent) * tangentWS.w);
        const float2 parallax = float2(
            dot(surface.viewWS, tangent),
            dot(surface.viewWS, bitangent)) * eye.x * 0.05f;
        const float4 eyeControl = HikariSampleMaterialTexture(
            eyeDescriptor,
            gLinearClamp,
            saturate(uv + parallax),
            0.0f.xxxx);
        const float facing = saturate(dot(surface.normalWS, surface.viewWS));
        const float sparkle = pow(facing, max(1.0f, eye.z));
        if ((materialData.instanceFlags &
             HIKARI_PROGRAM_FLAG_EYE_CONTROL_MAP) != 0u)
        {
            const float depthMask = lerp(0.35f, 1.0f, eyeControl.a);
            const float3 cornea = float3(0.72f, 0.84f, 1.0f) *
                eyeControl.g * sparkle * 0.12f;
            const float3 catchlight = 1.0f.xxx * eyeControl.b * sparkle;
            additive += (surface.baseColor * eyeControl.r * 0.08f +
                         cornea + catchlight) * eye.y * eye.w * depthMask;
        }
        else
        {
            additive += surface.baseColor * eye.y * eye.w *
                sparkle * eyeControl.r;
        }
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
            mask);
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
                mask);
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
            float4(0.55f, 0.55f, 1.0f, 0.25f));
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
