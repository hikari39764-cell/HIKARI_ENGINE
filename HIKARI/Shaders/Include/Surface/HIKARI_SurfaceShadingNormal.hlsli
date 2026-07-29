#ifndef HIKARI_SURFACE_SHADING_NORMAL_INCLUDED
#define HIKARI_SURFACE_SHADING_NORMAL_INCLUDED

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
    float3 n = normalize(normalWS);
    if (materialData.hasNormalTexture == 0u)
    {
        return n;
    }

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
    const float2 normalUv =
        HikariResolveMaterialUv(
            materialData,
            HIKARI_MATERIAL_UV_NORMAL,
            uv0,
            uv1);
    float3 normalTS =
        HikariSampleMaterialTexture(
            materialData.normalTextureDescriptorIndex,
            materialSampler,
            normalUv,
            float4(0.5f, 0.5f, 1.0f, 1.0f)).xyz * 2.0f - 1.0f;
    normalTS.xy *= materialData.normalScale;
    normalTS = normalize(normalTS);
    return normalize(normalTS.x * t + normalTS.y * b + normalTS.z * n);
}

#endif
