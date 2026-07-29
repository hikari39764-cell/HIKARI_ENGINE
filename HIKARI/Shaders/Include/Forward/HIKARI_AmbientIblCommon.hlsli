#ifndef HIKARI_AMBIENT_IBL_COMMON_HLSLI
#define HIKARI_AMBIENT_IBL_COMMON_HLSLI

float3 HikariEvaluateAmbientIblApprox(
    HikariPbrShadingContext context,
    float occlusion,
    float screenAo,
    float3 worldPos)
{
    const float3 baseColor = context.baseColor;
    const float metallic = context.metallic;
    const float roughness = context.roughness;
    const float3 n = context.normal;
    const float3 v = context.view;
    const float3 F0 = context.specularF0;
    const float ndotv = context.ndotView;
    float3 F =
        HikariFresnelSchlickRoughness(ndotv, F0, roughness);

    float3 kS = F;
    float3 kD = (1.0f.xxx - kS) * (1.0f - metallic);
    float3 diffuseAmbient =
        gAmbientColor.rgb * gAmbientIntensity;

    if (gSkyAmbientFromSky > 0.0001f)
    {
        float3 skyDiffuse = lerp(
            gSkyGroundColor,
            gSkyHorizonColor,
            saturate(n.y * 0.5f + 0.5f));
        skyDiffuse *=
            gSkyExposure * gSkyAmbientFromSky;
        diffuseAmbient = lerp(
            diffuseAmbient,
            skyDiffuse,
            saturate(gSkyAmbientFromSky));
    }

    diffuseAmbient = HikariBlendLightProbeDiffuse(
        diffuseAmbient,
        worldPos,
        n);
    float3 diffuse =
        kD * baseColor * diffuseAmbient;

    float3 r = normalize(reflect(-v, n));
    uint skyMode = (uint)(gSkyMode + 0.5f);
    if (!(skyMode == 2u && gSkyHasCubemap > 0.5f))
    {
        r.y = abs(r.y);
    }

    float3 specEnv = HikariSampleSkyEnvironment(r);
    specEnv *= max(0.0f, gSkyReflectionIntensity);

    float roughnessFade =
        1.0f - saturate(roughness * 0.85f);
    float3 specular = specEnv * F * roughnessFade;

    float materialAo = saturate(occlusion);
    float ssao = saturate(screenAo);
    float diffuseAo =
        materialAo *
        lerp(
            1.0f,
            ssao,
            saturate(gSsaoDiffuseStrength));
    float specularAo = lerp(
        1.0f,
        materialAo * ssao,
        saturate(roughness * roughness) *
            saturate(gSsaoSpecularStrength));
    return diffuse * diffuseAo +
        specular * specularAo;
}

float3 HikariSampleReflectionProbePrefiltered(
    uint providerSlot,
    float3 direction,
    float mip)
{
    return providerSlot == 0u
        ? gReflectionProbePrefilteredTex0.SampleLevel(
            gLinearWrap,
            direction,
            mip).rgb
        : gReflectionProbePrefilteredTex1.SampleLevel(
            gLinearWrap,
            direction,
            mip).rgb;
}

float3 HikariEvaluateAmbientIbl(
    HikariPbrShadingContext context,
    float occlusion,
    float screenAo,
    float3 worldPos)
{
    const float3 baseColor = context.baseColor;
    const float metallic = context.metallic;
    const float roughness = context.roughness;
    const float3 n = context.normal;
    const float3 v = context.view;
    bool hasLocalProbe =
        (gReflectionProbes[0].params.x > 0.5f &&
            gReflectionProbes[0].params.y > 0.5f) ||
        (gReflectionProbes[1].params.x > 0.5f &&
            gReflectionProbes[1].params.y > 0.5f);
    if (gIblHasIrradiance < 0.5f &&
        gIblHasPrefiltered < 0.5f &&
        !hasLocalProbe)
    {
        return HikariEvaluateAmbientIblApprox(
            context,
            occlusion,
            screenAo,
            worldPos);
    }

    const float3 F0 = context.specularF0;
    const float ndotv = context.ndotView;
    float3 F =
        HikariFresnelSchlickRoughness(ndotv, F0, roughness);
    float3 kS = F;
    float3 kD =
        (1.0f.xxx - kS) * (1.0f - metallic);

    float3 diffuseIbl =
        gAmbientColor.rgb * gAmbientIntensity;
    if (gIblHasIrradiance > 0.5f &&
        gSkyAmbientFromSky > 0.0001f)
    {
        float3 skyDiffuse =
            gIblIrradianceTex.Sample(
                gLinearWrap,
                normalize(
                    HikariRotateSkyYaw(
                        n,
                        gSkyYaw))).rgb *
            max(gSkyTint, 0.0f.xxx) *
            max(0.0f, gSkyExposure) *
            max(0.0f, gSkyAmbientFromSky);
        diffuseIbl = lerp(
            diffuseIbl,
            skyDiffuse,
            saturate(gSkyAmbientFromSky));
    }
    diffuseIbl = HikariBlendLightProbeDiffuse(
        diffuseIbl,
        worldPos,
        n);
    float3 diffuse =
        kD * baseColor * diffuseIbl;

    float2 envBrdf = float2(1.0f, 0.0f);
    if (gIblHasBrdfLut > 0.5f ||
        gReflectionProbes[0].params.z > 0.5f ||
        gReflectionProbes[1].params.z > 0.5f)
    {
        // BRDF LUT は (NdotV, roughness) の有限領域であり、Wrap すると
        // 1.0 の端が 0.0 側へ混ざって粗い面に鋭い白斑が発生する。
        envBrdf = gIblBrdfLutTex.SampleLevel(
            gLinearClamp,
            saturate(float2(ndotv, roughness)),
            0.0f).rg;
    }

    float3 r = normalize(reflect(-v, n));
    float3 skySpecular = 0.0f.xxx;
    if (gIblHasPrefiltered > 0.5f)
    {
        float mipCount =
            max(1.0f, gIblPrefilteredMipCount);
        float mip =
            roughness * (mipCount - 1.0f);
        float3 prefiltered =
            gIblPrefilteredTex.SampleLevel(
                gLinearWrap,
                normalize(
                    HikariRotateSkyYaw(r, gSkyYaw)),
                mip).rgb *
            max(gSkyTint, 0.0f.xxx) *
            max(0.0f, gSkyExposure);
        skySpecular = gIblHasBrdfLut > 0.5f
            ? prefiltered *
                (F * envBrdf.x + envBrdf.y)
            : prefiltered * F;
    }
    else
    {
        float roughnessFade =
            1.0f - saturate(roughness * 0.85f);
        skySpecular =
            HikariSampleSkyEnvironment(r) *
            F *
            roughnessFade;
    }
    skySpecular *=
        max(0.0f, gSkyReflectionIntensity);

    float3 probeSpecular[2];
    float probeWeights[2];
    [unroll]
    for (uint slot = 0u; slot < 2u; ++slot)
    {
        HikariReflectionProbeData probe =
            gReflectionProbes[slot];
        if (probe.params.x < 0.5f ||
            probe.params.y < 0.5f)
        {
            probeWeights[slot] = 0.0f;
            probeSpecular[slot] = 0.0f.xxx;
            continue;
        }
        probeWeights[slot] =
            HikariEvaluateReflectionProbeInfluence(
                probe,
                worldPos);
        if (probeWeights[slot] <= 0.0001f)
        {
            probeWeights[slot] = 0.0f;
            probeSpecular[slot] = 0.0f.xxx;
            continue;
        }

        float3 probeDirection =
            HikariComputeReflectionProbeSampleDirection(
                probe,
                worldPos,
                r);
        float mip =
            roughness *
            (max(1.0f, probe.params.w) - 1.0f);
        float3 prefiltered =
            HikariSampleReflectionProbePrefiltered(
                (uint)slot,
                probeDirection,
                mip);
        probeSpecular[slot] =
            probe.params.z > 0.5f
            ? prefiltered *
                (F * envBrdf.x + envBrdf.y)
            : prefiltered * F;
        probeSpecular[slot] *=
            max(0.0f, probe.intensity.x);
    }

    float3 specular = skySpecular;
    float priority0 = gReflectionProbes[0].shapeParams.w;
    float priority1 = gReflectionProbes[1].shapeParams.w;
    if (priority0 > priority1)
    {
        float3 lower = lerp(
            skySpecular,
            probeSpecular[1],
            probeWeights[1]);
        specular = lerp(
            lower,
            probeSpecular[0],
            probeWeights[0]);
    }
    else if (priority1 > priority0)
    {
        float3 lower = lerp(
            skySpecular,
            probeSpecular[0],
            probeWeights[0]);
        specular = lerp(
            lower,
            probeSpecular[1],
            probeWeights[1]);
    }
    else
    {
        float totalWeight =
            probeWeights[0] + probeWeights[1];
        if (totalWeight > 0.0001f)
        {
            float3 localSpecular =
                (probeSpecular[0] * probeWeights[0] +
                    probeSpecular[1] * probeWeights[1]) /
                totalWeight;
            float localCoverage =
                1.0f -
                (1.0f - probeWeights[0]) *
                (1.0f - probeWeights[1]);
            specular = lerp(
                skySpecular,
                localSpecular,
                saturate(localCoverage));
        }
    }

    float materialAo = saturate(occlusion);
    float ssao = saturate(screenAo);
    float diffuseAo =
        materialAo *
        lerp(
            1.0f,
            ssao,
            saturate(gSsaoDiffuseStrength));
    float specularAo = lerp(
        1.0f,
        materialAo * ssao,
        saturate(roughness * roughness) *
            saturate(gSsaoSpecularStrength));
    return diffuse * diffuseAo +
        specular * specularAo;
}

#endif
