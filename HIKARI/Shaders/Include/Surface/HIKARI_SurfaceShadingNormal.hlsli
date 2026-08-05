#ifndef HIKARI_SURFACE_SHADING_NORMAL_INCLUDED
#define HIKARI_SURFACE_SHADING_NORMAL_INCLUDED

float3 HikariTransformTangentNormalToWorld(
    float3 normalTS,
    float3 normalWS,
    float4 tangentWS)
{
    const float3 n = normalize(normalWS);
    float3 t = tangentWS.xyz;
    if (dot(t, t) < 1.0e-5f)
    {
        return n;
    }
    t = normalize(t);
    t -= n * dot(n, t);
    if (dot(t, t) < 1.0e-5f)
    {
        return n;
    }
    t = normalize(t);
    const float3 b = normalize(cross(n, t) * tangentWS.w);
    return normalize(normalTS.x * t + normalTS.y * b + normalTS.z * n);
}

// Resolves the world-space shading normal used by both forward lighting and the
// shared SceneSurface pass. Keeping this path common prevents SSR/AO from
// tracing against a different normal than the visible material.
float3 HikariResolveSurfaceShadingNormal(
    HikariMeshMaterialData materialData,
    float3 normalWS,
    float4 tangentWS,
    float2 uv0,
    float2 uv1,
    SamplerState materialSampler)
{
    const float3 n = normalize(normalWS);
    if (materialData.hasNormalTexture == 0u)
    {
        return n;
    }

    const float2 normalUv =
        HikariResolveMaterialUv(
            materialData,
            HIKARI_MATERIAL_UV_NORMAL,
            uv0,
            uv1);
    float2 normalXY =
        HikariSampleMaterialTexture(
            materialData.normalTextureDescriptorIndex,
            materialSampler,
            normalUv,
            float4(0.5f, 0.5f, 1.0f, 1.0f)).xy * 2.0f - 1.0f;
    normalXY *= materialData.normalScale;
    // Tangent-space normals belong to the positive hemisphere. Reconstructing
    // Z keeps RGB normal maps and two-channel BC5/ATI2 sources on one contract;
    // BC5 returns no authored blue channel and must not be decoded as Z = -1.
    const float normalZ =
        sqrt(saturate(1.0f - dot(normalXY, normalXY)));
    float3 normalTS = float3(normalXY, normalZ);
    normalTS = normalize(normalTS);
    return HikariTransformTangentNormalToWorld(
        normalTS, normalWS, tangentWS);
}

#endif
