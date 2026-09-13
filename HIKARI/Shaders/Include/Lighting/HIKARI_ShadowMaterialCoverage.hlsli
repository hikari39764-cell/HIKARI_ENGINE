#ifndef HIKARI_SHADOW_MATERIAL_COVERAGE_INCLUDED
#define HIKARI_SHADOW_MATERIAL_COVERAGE_INCLUDED

#define HIKARI_CAMERA_CBUFFER_REGISTER b10
#include "../Render3D/HIKARI_MeshCameraData.hlsli"
#undef HIKARI_CAMERA_CBUFFER_REGISTER
#include "../Material/HIKARI_MaterialBaseColorInputs.hlsli"
#include "../Material/HIKARI_MaterialExpressionSurface.hlsli"

void HikariClipShadowMaterial(
    HikariMeshMaterialData material, float2 uv0, float2 uv1,
    float3 worldPosition, float3 normalWS, float4 tangentWS,
    SamplerState materialSampler)
{
    if ((material.materialFlags & HIKARI_MATERIAL_FLAG_ALPHA_MASK) == 0u) return;
    float4 baseColor = HikariResolveMaterialBaseColor(material, uv0, uv1, materialSampler);
    float threshold = material.pbrParams.w;
    if (material.materialProgramIndex != HIKARI_STANDARD_PBR_MATERIAL_PROGRAM)
    {
        // 画面依存入力も観察カメラへ射影する。ライトのタイル座標は材質入力にしない。
        const float4 clipPosition = mul(gViewProj, float4(worldPosition, 1.0f));
        const float inverseW = abs(clipPosition.w) > 1.0e-6f ? rcp(clipPosition.w) : 0.0f;
        const float2 screenUv = clipPosition.xy * inverseW * float2(0.5f, -0.5f) + 0.5f;
        const float4 pixelPosition = float4(screenUv * gScreenParams.xy, clipPosition.z * inverseW, inverseW);
        const float normalLength = dot(normalWS, normalWS);
        float3 normal = normalLength > 1.0e-12f ? normalWS * rsqrt(normalLength) : float3(0.0f, 1.0f, 0.0f);
        if ((material.instanceFlags & HIKARI_PROGRAM_FLAG_DOUBLE_SIDED) != 0u)
        {
            // 三角形の表裏を観察方向で判定し、ライトからの表裏に依存させない。
            const float3 faceNormal = cross(ddx(worldPosition), ddy(worldPosition));
            if (dot(faceNormal, normal) * dot(faceNormal, gCameraPos.xyz - worldPosition) < 0.0f)
                normal = -normal;
        }
        const HikariMaterialExpressionContext context = HikariBuildMaterialExpressionContext(
            uv0, uv1, gTimeParams, worldPosition, normal, tangentWS,
            gCameraPos.xyz, pixelPosition, gScreenParams.zw);
        const HikariMaterialExpressionResult expression =
            HikariEvaluateMaterialExpression(material, context, materialSampler);
        HikariApplyMaterialExpressionCoverage(expression, baseColor, threshold);
    }
    clip(baseColor.a - threshold);
}

#endif
