#ifndef HIKARI_PAINTERLY_SHELL_INCLUDED
#define HIKARI_PAINTERLY_SHELL_INCLUDED

#include "HIKARI_SurfaceTangentFrame.hlsli"

// 筆触の殻（Painterly Shell）の固定経路。forward と GeometryAux が同じ関数で殻の被覆・筆触法線・色差を決め、
// 画素の破棄が両パスで一致する。材質の uniform は HikariMeshMaterialData::shellStroke0/1、テクスチャは
// strokeMask / strokeNormal の descriptor。層は HikariShellLayer（x 深さ, y 基底面, z 層 ID, w フェード）。
//   基底面: 筆触の色差と筆触法線だけ掛かり、常に見える。
//   殻:     輪郭（1 - N·V の冪）× 筆触マスク × 被覆率を層ごとのディザで被覆にする。拡散のみで高光は持たない。

bool HikariIsPainterlyShellMaterial(HikariMeshMaterialData materialData)
{
    return (materialData.materialFlags & HIKARI_MATERIAL_FLAG_PAINTERLY_SHELL) != 0u;
}

struct HikariPainterlyStroke
{
    float mask;      // 筆触マスク 0..1（平均 0.5 付近）
    float3 normalTS; // 筆触の接空間法線（強さ適用済み）
};

// 層ごとに UV を回してずらす（UV の段差）。同じマスクでも層ごとに違う筆触が重なる。
float2 HikariPainterlyStrokeUv(HikariMeshMaterialData materialData, float2 uv, float4 layer, out float rotation)
{
    rotation = materialData.shellStroke0.y + layer.z * 0.61f;
    float s, c;
    sincos(rotation, s, c);
    const float2 p = uv * materialData.shellStroke0.x;
    return float2(p.x * c - p.y * s, p.x * s + p.y * c) + layer.z * float2(0.173f, 0.419f);
}

HikariPainterlyStroke HikariSamplePainterlyStroke(
    HikariMeshMaterialData materialData, SamplerState samplerState, float2 uv, float4 layer)
{
    HikariPainterlyStroke stroke;
    float rotation;
    const float2 strokeUv = HikariPainterlyStrokeUv(materialData, uv, layer, rotation);
    stroke.mask = HikariSampleMaterialTexture(
        materialData.strokeMaskTextureDescriptorIndex, samplerState, strokeUv, 0.5f.xxxx).r;
    float2 normalXY = HikariSampleMaterialTexture(
        materialData.strokeNormalTextureDescriptorIndex, samplerState, strokeUv,
        float4(0.5f, 0.5f, 1.0f, 1.0f)).xy * 2.0f - 1.0f;
    // UV を回した分だけ法線の向きを戻す（BC5 は Z を持たないので再構成）。
    float s, c;
    sincos(-rotation, s, c);
    normalXY = float2(normalXY.x * c - normalXY.y * s, normalXY.x * s + normalXY.y * c) * materialData.shellStroke0.w;
    stroke.normalTS = normalize(float3(normalXY, sqrt(saturate(1.0f - dot(normalXY, normalXY)))));
    return stroke;
}

// 殻の可視性。輪郭ほど濃く、外の層ほど薄い。筆触マスクはコントラストを上げ、筆の跡が殻の抜けとして見えるようにする。
// coverage 1 で輪郭の筆触がほぼ全部残る。
float HikariPainterlyShellVisibility(
    HikariMeshMaterialData materialData, float4 layer, float3 geometricNormalWS, float3 viewWS, float mask)
{
    if (layer.y >= 0.5f)
        return 1.0f;
    const float edge = pow(1.0f - saturate(dot(geometricNormalWS, viewWS)), materialData.shellStroke1.x);
    const float thinning = 1.0f - 0.6f * layer.x;
    const float strokes = saturate((mask - 0.2f) * 2.2f);
    return saturate(edge * strokes * materialData.shellStroke1.y * 1.6f * thinning);
}

// 層ごと・フレームごとに違う閾値のディザ。重ねた殻が同じ画素で同時に消えず、TAA が被覆率どおりの半透明へ収束する。
// forward と GeometryAux は同じ frame index を受け取るので画素の判定は一致する。
float HikariPainterlyDither(float2 pixelPosition, float layerId, float frameIndex)
{
    const uint2 p = (uint2)pixelPosition;
    uint hash = p.x * 1597334677u ^ p.y * 3812015801u ^ (uint)layerId * 2654435761u ^
        ((uint)frameIndex & 7u) * 0x9e3779b9u;
    hash ^= hash >> 16u;
    hash *= 0x7feb352du;
    hash ^= hash >> 15u;
    return (float(hash & 255u) + 0.5f) / 256.0f;
}

// 殻の被覆を確定し、見えない画素を破棄する。forward と GeometryAux が同じ入力で呼ぶ。
void HikariApplyPainterlyShellCoverage(HikariMeshMaterialData materialData, float4 layer, float2 pixelPosition,
    float frameIndex, float3 geometricNormalWS, float3 viewWS, float mask)
{
    if (layer.y >= 0.5f)
        return;
    clip(HikariPainterlyShellVisibility(materialData, layer, geometricNormalWS, viewWS, mask) -
        HikariPainterlyDither(pixelPosition, layer.z, frameIndex));
}

// 筆触の明暗。マスクの平均 0.5 を中心に色差だけ揺らす。
float3 HikariApplyPainterlyStrokeColor(HikariMeshMaterialData materialData, float3 albedo, float mask)
{
    return albedo * max(1.0f + materialData.shellStroke0.z * (mask - 0.5f) * 2.0f, 0.0f);
}

// 筆触法線を材質の法線へ重ねる。筆触の法線を世界へ変換し、幾何法線からの差分を足す。
float3 HikariApplyPainterlyStrokeNormal(
    float3 shadingNormalWS, float3 geometricNormalWS, float4 tangentWS, float3 strokeNormalTS)
{
    const float3 strokeWS = HikariTransformTangentNormalToWorld(strokeNormalTS, geometricNormalWS, tangentWS);
    return normalize(shadingNormalWS + (strokeWS - geometricNormalWS));
}

// 殻は絵の具の層。拡散だけで、鏡面反射と金属感は持たない。
void HikariApplyPainterlyShellPbr(float4 layer, inout float metallic, inout float roughness, inout float specularFactor)
{
    if (layer.y < 0.5f)
    {
        metallic = 0.0f;
        roughness = 1.0f;
        specularFactor = 0.0f;
    }
}

#endif
