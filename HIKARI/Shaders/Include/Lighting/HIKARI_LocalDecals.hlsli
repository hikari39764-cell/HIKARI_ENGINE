#ifndef HIKARI_LOCAL_DECALS_INCLUDED
#define HIKARI_LOCAL_DECALS_INCLUDED

// 面へ投影するデカール。局所光と同じ記録バッファとクラスタを使い、クラスタの中で光源の後ろに並ぶ。
// include する側が gMaterialTexturePool、HIKARI_MATERIAL_TEXTURE_POOL_COUNT、gLinearClamp を宣言する。
// 記録（RENDER3D::LIGHTING::LocalDecalGpuData）:
//   positionRange / colorIntensity / directionInnerCos: ワールドから箱の空間 [-0.5, 0.5]^3 への三行
//   outerConeCos: 色 + 法線 * 4096 の記述子番号 + 1（整数値、0 は無し）、angularFalloff: 薄れ始めの cos
//   kind: 上位 24 bit が色（RGB 各 8 bit）、sourceRadius: 色の重み
//   shapeTangent: (法線の重み, 粗さ金属度の重み, 粗さ, 金属度)
// 投影はローカル +Z。テクスチャの右が -X、上が +Y（右手系で投影の向きに見て鏡像にならない）。
#include "HIKARI_LocalLightList.hlsli"

struct HikariDecalSurface
{
    float3 albedo;
    float3 normal;
    float roughness;
    float metallic;
};

float3 HikariDecalBoxVector(HikariLocalLightGpuData record, float3 value)
{
    return float3(dot(record.positionRange.xyz, value), dot(record.colorIntensity.xyz, value),
        dot(record.directionInnerCos.xyz, value));
}

float4 HikariSampleDecalTexture(uint encodedIndex, float2 uv, float2 uvDx, float2 uvDy, float4 fallback)
{
    const uint index = encodedIndex - 1u;
    if (encodedIndex == 0u || index >= HIKARI_MATERIAL_TEXTURE_POOL_COUNT)
        return fallback;
    return gMaterialTexturePool[NonUniformResourceIndex(index)].SampleGrad(gLinearClamp, uv, uvDx, uvDy);
}

// positionDx / positionDy は画素のワールド位置の微分（可変長のクラスタループの外で取る）。
void HikariApplyLocalDecals(float3 worldPosition, float2 pixelPosition, float viewDepth, float3 geometricNormal,
    float3 positionDx, float3 positionDy, inout HikariDecalSurface surface)
{
    const uint2 cluster = HikariResolveLocalDecalCluster(pixelPosition, viewDepth);
    [loop]
    for (uint slot = 0u; slot < cluster.y; ++slot)
    {
        const HikariLocalLightGpuData record = gHikariLocalLights[gHikariLocalLightClusterIndices[cluster.x + slot]];
        const float3 local = HikariDecalBoxVector(record, worldPosition) +
            float3(record.positionRange.w, record.colorIntensity.w, record.directionInnerCos.w);
        if (any(abs(local) > 0.5f))
            continue;
        // 箱の軸のワールドの向き（行は軸 / 長さ^2 なので正規化すれば軸の向き）。
        const float3 projection = normalize(record.directionInnerCos.xyz);
        const float facing = dot(geometricNormal, -projection);
        const float fade = saturate(facing / max(record.angularFalloff, 1e-3f)) *
            saturate((0.5f - abs(local.z)) * 8.0f);
        if (fade <= 0.0f)
            continue;
        const float2 uv = 0.5f - local.xy;
        const float2 uvDx = -HikariDecalBoxVector(record, positionDx).xy;
        const float2 uvDy = -HikariDecalBoxVector(record, positionDy).xy;
        const uint textures = (uint)record.outerConeCos;
        const float3 tint = float3((record.kind >> 8u) & 255u, (record.kind >> 16u) & 255u, record.kind >> 24u) / 255.0f;
        const float4 color = HikariSampleDecalTexture(textures & 4095u, uv, uvDx, uvDy, 1.0f.xxxx);
        const float coverage = color.a * fade;
        surface.albedo = lerp(surface.albedo, color.rgb * tint, saturate(coverage * record.sourceRadius));
        const uint normalTexture = textures >> 12u;
        if (normalTexture != 0u && record.shapeTangent.x > 0.0f)
        {
            // 接空間の X はテクスチャの右（箱の -X）、Y は上（箱の +Y）を面へ射影した向き。
            const float3 right = -record.positionRange.xyz;
            const float3 tangent = normalize(right - surface.normal * dot(right, surface.normal));
            const float3 crossed = cross(surface.normal, tangent);
            const float3 bitangent = dot(crossed, record.colorIntensity.xyz) < 0.0f ? -crossed : crossed;
            // 材質の法線と同じ契約: XY だけを読み、Z は正の半球に戻す（BC5 の二成分も同じ）。
            const float2 normalXY =
                HikariSampleDecalTexture(normalTexture, uv, uvDx, uvDy, float4(0.5f, 0.5f, 1.0f, 1.0f)).xy * 2.0f -
                1.0f;
            const float normalZ = sqrt(saturate(1.0f - dot(normalXY, normalXY)));
            const float3 decalNormal =
                normalize(tangent * normalXY.x + bitangent * normalXY.y + surface.normal * normalZ);
            surface.normal = normalize(lerp(surface.normal, decalNormal, saturate(coverage * record.shapeTangent.x)));
        }
        const float materialWeight = saturate(coverage * record.shapeTangent.y);
        surface.roughness = lerp(surface.roughness, record.shapeTangent.z, materialWeight);
        surface.metallic = lerp(surface.metallic, record.shapeTangent.w, materialWeight);
    }
}

#endif
