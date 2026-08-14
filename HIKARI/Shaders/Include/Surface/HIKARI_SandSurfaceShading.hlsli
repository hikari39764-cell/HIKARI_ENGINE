#ifndef HIKARI_SAND_SURFACE_SHADING_INCLUDED
#define HIKARI_SAND_SURFACE_SHADING_INCLUDED

#include "Include/Surface/HIKARI_SandSurfaceParams.hlsli"

float4 HikariShadeSandSurface(
    PSInput input,
    HikariMeshObjectData objectData,
    float3 sandSurfaceData)
{
    (void)objectData;
    HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(input.materialDataIndex);
    const HikariSurfaceGpuSceneInstance instance =
        HikariGetSurfaceGpuSceneInstanceAt(input.surfaceGpuSceneIndex);
    const HikariSandSurfaceParams p =
        HikariLoadSandSurfaceParamsFromDataIndex(
            instance.surfaceFeatureDataIndex);

    const float3 worldPos = input.worldPosWS;
    const float3 viewDir =
        normalize(gCameraPos.xyz - worldPos);
    const float3 lightDir = normalize(-gDirectionalDir.xyz);

    // アルベドは通常マテリアルの baseColor / baseColorTexture を再利用する。
    // テクスチャが無い場合は procedural な砂色へ落とす。
    float4 albedo = materialData.baseColor;
    if (materialData.hasBaseColorTexture != 0)
    {
        const float2 baseColorUv = HikariResolveMaterialUv(
            materialData,
            HIKARI_MATERIAL_UV_BASE_COLOR,
            input.uv,
            input.uv1);
        const float2 sandUv =
            baseColorUv +
            (HikariSandValueNoise(worldPos.xz * 0.35f) - 0.5f) *
                0.08f * float2(1.0f, 0.75f);
        albedo *= HikariSampleMaterialTexture(
            materialData.baseColorTextureDescriptorIndex,
            gLinearWrap,
            sandUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f));
    }
    else
    {
        albedo.rgb *= p.lightTint;
    }

    // 凹みは湿った深い色へ、滑らかに乾いた砂へ戻す。
    const float pitMask = saturate(-sandSurfaceData.z /
        max(p.extents.x * 0.02f, 0.05f));
    albedo.rgb = lerp(
        albedo.rgb,
        albedo.rgb * p.darkTint * 0.82f,
        pitMask * p.pitDarkening);

    // 法線マップ（NormalDX）は共有の normal resolver でワールド空間へ
    // 変換する。VS が変形勾配から再構築した normalWS を基準にするため、
    // 凹みの傾斜とテクスチャの細粒が食い違わない。
    float3 shadingNormal = HikariResolveSurfaceShadingNormal(
        materialData,
        input.normalWS,
        input.tangentWS,
        input.uv,
        input.uv1,
        gLinearWrap);
    // さらに穏やかなリップルを重ね、のっぺりした見た目を避ける。
    const float rippleX =
        HikariSandValueNoise(worldPos.xz * 1.7f + 3.1f) - 0.5f;
    const float rippleZ =
        HikariSandValueNoise(worldPos.xz * 1.9f + 9.7f) - 0.5f;
    shadingNormal = normalize(
        shadingNormal +
        float3(rippleX, 0.0f, rippleZ) * p.rippleAmount * 0.10f);

    const float ndotl = saturate(dot(shadingNormal, lightDir));
    const float ramp = smoothstep(0.10f, 0.58f, ndotl);
    const float3 diffuse = lerp(p.darkTint, p.lightTint, ramp);
    float3 color = albedo.rgb * diffuse *
        (0.42f + 0.58f * ndotl);

    // 砂粒のきらめき。高周波 hash で細かい輝点を作り、硬さを避ける。
    const float grain = HikariSandHash21(
        floor(worldPos.xz * 160.0f));
    const float sparkle = pow(grain, 12.0f) *
        p.sparkleStrength * ndotl;
    color += sparkle * 1.8f * float3(1.0f, 0.96f, 0.82f);

    // 簡易フレネルで明るい空の照り返しを足し、単調な土色を避ける。
    const float fresnel =
        pow(1.0f - saturate(dot(shadingNormal, viewDir)), 3.0f);
    color += fresnel * 0.06f * float3(0.72f, 0.82f, 1.0f);

    return float4(max(color, 0.0f.xxx), albedo.a);
}

#endif
