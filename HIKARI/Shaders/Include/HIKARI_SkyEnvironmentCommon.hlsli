#ifndef HIKARI_SKY_ENVIRONMENT_COMMON_HLSLI
#define HIKARI_SKY_ENVIRONMENT_COMMON_HLSLI

float3 HikariRotateSkyYaw(float3 dir, float yaw)
{
    float s = sin(yaw);
    float c = cos(yaw);

    return float3(
        dir.x * c - dir.z * s,
        dir.y,
        dir.x * s + dir.z * c
    );
}

float3 HikariEvaluateSkyApprox(float3 dir)
{
    dir = normalize(HikariRotateSkyYaw(dir, gSkyYaw));

    float y = saturate(dir.y * 0.5f + 0.5f);

    float3 upper = lerp(gSkyHorizonColor, gSkyZenithColor, y);
    float3 lower = lerp(gSkyGroundColor, gSkyHorizonColor, y);
    float3 color = (dir.y >= 0.0f) ? upper : lower;

    float horizon = pow(
        saturate(1.0f - abs(dir.y)),
        max(0.01f, gSkyHorizonPower)
    );

    color = lerp(color, gSkyHorizonColor, horizon * 0.25f);
    color *= max(0.0f, gSkyExposure);

    return color;
}

float3 HikariSampleSkyEnvironment(float3 dir)
{
    float3 sky = HikariEvaluateSkyApprox(dir);

    uint mode = (uint)(gSkyMode + 0.5f);

    if (mode == 2u && gSkyHasCubemap > 0.5f)
    {
        float3 cubeDir = normalize(HikariRotateSkyYaw(dir, gSkyYaw));
        sky = gSkyCube.Sample(gLinearWrap, cubeDir).rgb;
        sky *= max(0.0f, gSkyExposure);
    }

    return sky;
}

// Light probe volume: SH9 係数は 9 枚の Texture3D (RGB=係数, texel=probe) に
// 焼いてあり、hardware trilinear で係数を補間してから SH を評価する。
// SH は線形なので「係数を補間してから評価」は「各 probe を評価してから補間」
// と数学的に等価。旧 StructuredBuffer 経路 (画素あたり 36~72 読み) と
// FastSmooth の Y 最近傍アーティファクトの両方をこれで置き換えた。
float3 HikariEvaluateLightProbeVolumeDiffuse(float3 worldPos, float3 n)
{
    if (gLightProbeEnabled < 0.5f ||
        gLightProbeProbeCount == 0u ||
        gLightProbeCountX < 1u ||
        gLightProbeCountY < 1u ||
        gLightProbeCountZ < 1u)
    {
        return 0.0f.xxx;
    }

    float3 counts = float3(
        (float)gLightProbeCountX,
        (float)gLightProbeCountY,
        (float)gLightProbeCountZ);
    float3 gridCoord =
        (worldPos - gLightProbeOrigin) /
        max(gLightProbeSpacing, float3(0.0001f, 0.0001f, 0.0001f));
    gridCoord = clamp(gridCoord, 0.0f.xxx, counts - 1.0f);
    // texel 中心 = probe 位置。clamp 済みで UVW は常に内側に収まるため、
    // wrap sampler でも境界は安全。
    float3 uvw = (gridCoord + 0.5f) / counts;

    float x = n.x;
    float y = n.y;
    float z = n.z;

    float b0 = 0.282095f;
    float b1 = 0.488603f * y;
    float b2 = 0.488603f * z;
    float b3 = 0.488603f * x;
    float b4 = 1.092548f * x * y;
    float b5 = 1.092548f * y * z;
    float b6 = 0.315392f * (3.0f * z * z - 1.0f);
    float b7 = 1.092548f * x * z;
    float b8 = 0.546274f * (x * x - y * y);

    const float c0 = 3.14159265f;
    const float c1 = 2.09439510f;
    const float c2 = 0.78539816f;

    float3 result = 0.0f.xxx;
    result += gLightProbeShVolume[0].SampleLevel(gLinearWrap, uvw, 0.0f).rgb * (b0 * c0);
    result += gLightProbeShVolume[1].SampleLevel(gLinearWrap, uvw, 0.0f).rgb * (b1 * c1);
    result += gLightProbeShVolume[2].SampleLevel(gLinearWrap, uvw, 0.0f).rgb * (b2 * c1);
    result += gLightProbeShVolume[3].SampleLevel(gLinearWrap, uvw, 0.0f).rgb * (b3 * c1);
    result += gLightProbeShVolume[4].SampleLevel(gLinearWrap, uvw, 0.0f).rgb * (b4 * c2);
    result += gLightProbeShVolume[5].SampleLevel(gLinearWrap, uvw, 0.0f).rgb * (b5 * c2);
    result += gLightProbeShVolume[6].SampleLevel(gLinearWrap, uvw, 0.0f).rgb * (b6 * c2);
    result += gLightProbeShVolume[7].SampleLevel(gLinearWrap, uvw, 0.0f).rgb * (b7 * c2);
    result += gLightProbeShVolume[8].SampleLevel(gLinearWrap, uvw, 0.0f).rgb * (b8 * c2);
    return max(result, 0.0f.xxx);
}

float3 HikariBlendLightProbeDiffuse(float3 fallbackDiffuse, float3 worldPos, float3 n)
{
    if (gLightProbeEnabled < 0.5f || gLightProbeIntensity <= 0.0001f)
    {
        return fallbackDiffuse;
    }

    // Hardware trilinear 化により FastSmooth / FullTrilinear の区別は消えた。
    // sampling mode は Off (gLightProbeEnabled) の判定にだけ使われる。
    float3 localDiffuse = HikariEvaluateLightProbeVolumeDiffuse(worldPos, n);
    return lerp(fallbackDiffuse, localDiffuse, saturate(gLightProbeIntensity));
}

float HikariSmoothProbeWeight(float value)
{
    value = saturate(value);
    return value * value * (3.0f - 2.0f * value);
}

float HikariEvaluateSphereReflectionProbeInfluence(float3 worldPos)
{
    float radius = max(0.001f, gReflectionProbeRadius);
    float blendDistance = min(max(0.001f, gReflectionProbeBlendDistance), radius);
    float dist = length(worldPos - gReflectionProbePosition);
    return HikariSmoothProbeWeight((radius - dist) / blendDistance);
}

float HikariEvaluateBoxReflectionProbeInfluence(float3 worldPos)
{
    if (any(worldPos < gReflectionProbeInfluenceBoxMin.xyz) ||
        any(worldPos > gReflectionProbeInfluenceBoxMax.xyz))
    {
        return 0.0f;
    }

    float3 toMin = worldPos - gReflectionProbeInfluenceBoxMin.xyz;
    float3 toMax = gReflectionProbeInfluenceBoxMax.xyz - worldPos;
    float edgeDistance = min(min(toMin.x, toMin.y), toMin.z);
    edgeDistance = min(edgeDistance, min(min(toMax.x, toMax.y), toMax.z));

    float3 boxSize = max(
        gReflectionProbeInfluenceBoxMax.xyz - gReflectionProbeInfluenceBoxMin.xyz,
        0.001f.xxx);
    float halfMinSize = max(0.001f, min(min(boxSize.x, boxSize.y), boxSize.z) * 0.5f);
    float blendDistance = min(max(0.001f, gReflectionProbeBlendDistance), halfMinSize);
    return HikariSmoothProbeWeight(edgeDistance / blendDistance);
}

float HikariEvaluateReflectionProbeInfluence(float3 worldPos)
{
    float influence = (gReflectionProbeInfluenceShape > 0.5f)
        ? HikariEvaluateBoxReflectionProbeInfluence(worldPos)
        : HikariEvaluateSphereReflectionProbeInfluence(worldPos);
    return saturate(influence * max(0.0f, gReflectionProbeSpecularIntensity));
}

float3 HikariSafeProbeDirection(float3 dir)
{
    float lenSq = dot(dir, dir);
    return (lenSq > 1e-8f) ? (dir * rsqrt(lenSq)) : float3(0.0f, 1.0f, 0.0f);
}

float3 HikariSafeRayDirection(float3 dir)
{
    return float3(
        (abs(dir.x) > 1e-5f) ? dir.x : ((dir.x < 0.0f) ? -1e-5f : 1e-5f),
        (abs(dir.y) > 1e-5f) ? dir.y : ((dir.y < 0.0f) ? -1e-5f : 1e-5f),
        (abs(dir.z) > 1e-5f) ? dir.z : ((dir.z < 0.0f) ? -1e-5f : 1e-5f)
    );
}

bool HikariRayIntersectsBox(
    float3 origin,
    float3 dir,
    float3 boxMin,
    float3 boxMax,
    out float hitT)
{
    float3 safeDir = HikariSafeRayDirection(dir);
    float3 t0 = (boxMin - origin) / safeDir;
    float3 t1 = (boxMax - origin) / safeDir;
    float3 tNear3 = min(t0, t1);
    float3 tFar3 = max(t0, t1);

    float tNear = max(max(tNear3.x, tNear3.y), tNear3.z);
    float tFar = min(min(tFar3.x, tFar3.y), tFar3.z);
    bool inside = all(origin >= boxMin) && all(origin <= boxMax);
    hitT = inside ? tFar : tNear;
    return tFar >= max(tNear, 0.0f) && hitT >= 0.0f;
}

float3 HikariComputeReflectionProbeSampleDirection(float3 worldPos, float3 reflectionDir)
{
    if (gReflectionProbeProjectionShape < 0.5f)
    {
        return reflectionDir;
    }

    float3 boxSize = gReflectionProbeProjectionBoxMax.xyz - gReflectionProbeProjectionBoxMin.xyz;
    if (any(boxSize <= 0.001f.xxx))
    {
        return reflectionDir;
    }

    float hitT = 0.0f;
    if (!HikariRayIntersectsBox(
        worldPos,
        reflectionDir,
        gReflectionProbeProjectionBoxMin.xyz,
        gReflectionProbeProjectionBoxMax.xyz,
        hitT))
    {
        return reflectionDir;
    }

    // Box Projection は cubemap の捕獲位置から hit 点へ向けて補正する。
    float3 hitPos = worldPos + reflectionDir * hitT;
    return HikariSafeProbeDirection(hitPos - gReflectionProbePosition);
}

float3 HikariEvaluateAmbientIblApprox(
    float3 baseColor,
    float metallic,
    float roughness,
    float3 specularColor,
    float specularFactor,
    float occlusion,
    float screenAo,
    float3 n,
    float3 v,
    float3 worldPos)
{
    float3 F0 = HikariSpecularF0(baseColor, metallic, specularColor, specularFactor);
    float ndotv = saturate(dot(n, v));
    float3 F = HikariFresnelSchlickRoughness(ndotv, F0, roughness);

    float3 kS = F;
    float3 kD = (1.0f.xxx - kS) * (1.0f - metallic);

    float3 diffuseAmbient = gAmbientColor.rgb * gAmbientIntensity;

    if (gSkyAmbientFromSky > 0.0001f)
    {
        float3 skyDiffuse =
            lerp(gSkyGroundColor, gSkyHorizonColor, saturate(n.y * 0.5f + 0.5f));
        skyDiffuse *= gSkyExposure * gSkyAmbientFromSky;
        diffuseAmbient = lerp(diffuseAmbient, skyDiffuse, saturate(gSkyAmbientFromSky));
    }

    diffuseAmbient = HikariBlendLightProbeDiffuse(diffuseAmbient, worldPos, n);
    float3 diffuse = kD * baseColor * diffuseAmbient;

    float3 r = normalize(reflect(-v, n));
    uint skyMode = (uint)(gSkyMode + 0.5f);
    if (!(skyMode == 2u && gSkyHasCubemap > 0.5f))
    {
        // Gradient fallback だけ上下反転を抑える。
        r.y = abs(r.y);
    }

    float3 specEnv = HikariSampleSkyEnvironment(r);
    specEnv *= max(0.0f, gSkyReflectionIntensity);

    // Approximation until prefiltered specular IBL is implemented.
    // Use the same factor in all material shaders to keep visuals consistent.
    float roughnessFade = 1.0f - saturate(roughness * 0.85f);
    float3 specular = specEnv * F * roughnessFade;

    float materialAo = saturate(occlusion);
    float ssao = saturate(screenAo);
    float diffuseAo = materialAo * lerp(1.0f, ssao, saturate(gSsaoDiffuseStrength));
    float specularAo = lerp(1.0f, materialAo * ssao, saturate(roughness * roughness) * saturate(gSsaoSpecularStrength));
    return diffuse * diffuseAo + specular * specularAo;
}

float3 HikariEvaluateAmbientIbl(
    float3 baseColor,
    float metallic,
    float roughness,
    float3 specularColor,
    float specularFactor,
    float occlusion,
    float screenAo,
    float3 n,
    float3 v,
    float3 worldPos)
{
    float3 result = 0.0f.xxx;

    bool hasLocalProbe = (gReflectionProbeEnabled > 0.5f && gReflectionProbeHasPrefiltered > 0.5f);
    if (gIblHasIrradiance < 0.5f && gIblHasPrefiltered < 0.5f && !hasLocalProbe)
    {
        result = HikariEvaluateAmbientIblApprox(
            baseColor,
            metallic,
            roughness,
            specularColor,
            specularFactor,
            occlusion,
            screenAo,
            n,
            v,
            worldPos);
    }
    else
    {
        float3 F0 = HikariSpecularF0(baseColor, metallic, specularColor, specularFactor);
        float ndotv = saturate(dot(n, v));
        float3 F = HikariFresnelSchlickRoughness(ndotv, F0, roughness);

        float3 kS = F;
        float3 kD = (1.0f.xxx - kS) * (1.0f - metallic);

        float3 diffuseIbl = 0.0f.xxx;
        if (gIblHasIrradiance > 0.5f)
        {
            diffuseIbl = gIblIrradianceTex.Sample(gLinearWrap, n).rgb;
        }
        else
        {
            diffuseIbl = gAmbientColor.rgb * gAmbientIntensity;
        }

        diffuseIbl = HikariBlendLightProbeDiffuse(diffuseIbl, worldPos, n);
        float3 diffuse = kD * baseColor * diffuseIbl;

        // Sky IBL と reflection probe は同じ (ndotv, roughness) で BRDF LUT を
        // 引くため、必要なら一度だけ採ってどちらの経路でも再利用する。
        float2 envBrdf = float2(1.0f, 0.0f);
        if (gIblHasBrdfLut > 0.5f || gReflectionProbeHasBrdfLut > 0.5f)
        {
            envBrdf = gIblBrdfLutTex.Sample(gLinearWrap, float2(ndotv, roughness)).rg;
        }

        float3 skySpecular = 0.0f.xxx;
        if (gIblHasPrefiltered > 0.5f)
        {
            float3 r = normalize(reflect(-v, n));
            float mipCount = max(1.0f, gIblPrefilteredMipCount);
            float mip = roughness * (mipCount - 1.0f);
            float3 prefiltered = gIblPrefilteredTex.SampleLevel(gLinearWrap, r, mip).rgb;

            if (gIblHasBrdfLut > 0.5f)
            {
                skySpecular = prefiltered * (F * envBrdf.x + envBrdf.y);
            }
            else
            {
                skySpecular = prefiltered * F;
            }
        }
        else
        {
            float3 r = normalize(reflect(-v, n));
            float roughnessFade = 1.0f - saturate(roughness * 0.85f);
            skySpecular = HikariSampleSkyEnvironment(r) * F * roughnessFade * max(0.0f, gSkyReflectionIntensity);
        }

        float3 specular = skySpecular;
        if (hasLocalProbe)
        {
            // 影響 0 (probe 範囲外 = 画面の大半) の画素は box projection と
            // cubemap 採様を丸ごと省く。lerp(sky, probe, 0) == sky なので等価。
            float probeInfluence = HikariEvaluateReflectionProbeInfluence(worldPos);
            if (probeInfluence > 0.0001f)
            {
                float3 r = HikariSafeProbeDirection(reflect(-v, n));
                float3 probeDir = HikariComputeReflectionProbeSampleDirection(worldPos, r);

                float probeMipCount = max(1.0f, gReflectionProbeMipCount);
                float probeMip = roughness * (probeMipCount - 1.0f);
                float3 probePrefiltered = gReflectionProbePrefilteredTex.SampleLevel(gLinearWrap, probeDir, probeMip).rgb;
                float3 probeSpecular = probePrefiltered * F;
                if (gReflectionProbeHasBrdfLut > 0.5f)
                {
                    probeSpecular = probePrefiltered * (F * envBrdf.x + envBrdf.y);
                }

                // Local probe は加算や平均ではなく、影響範囲内で Sky IBL を置き換える。
                specular = lerp(skySpecular, probeSpecular, probeInfluence);
            }
        }

        float materialAo = saturate(occlusion);
        float ssao = saturate(screenAo);
        float diffuseAo = materialAo * lerp(1.0f, ssao, saturate(gSsaoDiffuseStrength));
        float specularAo = lerp(1.0f, materialAo * ssao, saturate(roughness * roughness) * saturate(gSsaoSpecularStrength));
        result = diffuse * diffuseAo + specular * specularAo;
    }

    return result;
}

#endif
