// 地形モジュール（TerrainKit）の面。CDLOD の格子を瓦片の高さ図で持ち上げ、前向きの標準 PBR と同じ光の経路で着色する。
// 通用の前向き PSO へ入れず専用の PSO で描く。root は MeshGraphics と同じ番号で、次の三つだけ別の型になる。
//   JointPalette -> t0, space10（実例）、ClusterGeometryPool -> t1..t4, space10（瓦片の図）、MeshletVisibleRanges -> b3
#include "Include/Forward/HIKARI_ForwardCommon.hlsli"
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/Surface/HIKARI_SurfaceShadingNormal.hlsli"
#include "Include/Surface/HIKARI_SurfacePartition.hlsli"
#include "Include/Forward/HIKARI_AmbientIblCommon.hlsli"
#include "Include/Lighting/HIKARI_SurfaceDirectLighting.hlsli"
#include "Include/Forward/HIKARI_ContactShadow.hlsli"
#include "Include/Forward/HIKARI_ForwardMaterialInputs.hlsli"
#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"

struct TerrainNode
{
    float2 originXZ;
    float nodeSize;
    float morphStart;
    float morphEnd;
    uint tileLayer;
    float2 tileOriginXZ;
};

StructuredBuffer<TerrainNode> gTerrainNodes : register(t0, space10);
Texture2DArray<float> gTerrainHeight : register(t1, space10);
Texture2DArray<float2> gTerrainNormal : register(t2, space10);
Texture2DArray<uint> gTerrainControl : register(t3, space10);
// 地表材質の配列：色（RGB）+ 高さ（A）、法線 xy + 粗さ + AO。
Texture2DArray<float4> gTerrainSurfaceColor : register(t4, space10);
Texture2DArray<float4> gTerrainSurfaceDetail : register(t5, space10);
// 材質の表（列 = 枠の番号）：繰り返しの長さ、高さ混合の幅、法線の強さ、図があるか。
Texture2DArray<float4> gTerrainSurfaceParams : register(t6, space10);

cbuffer RenderSurfaceConstants : register(b3)
{
    float4x4 gSurfaceViewProj;
    float3 gSurfaceCameraPosition;
    uint gSurfacePass;
    float gTerrainHeightMin;
    float gTerrainHeightRange;
    float gTerrainTileSize;
    float gTerrainTileCells;
    uint gTerrainGridCells;
    uint gTerrainDebugFlags;
    uint gTerrainSurfaceCount;
    float gTerrainReserved;
};

struct TerrainVertex
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float2 tileUv : TEXCOORD1;
    nointerpolation uint tileLayer : TEXCOORD2;
};

float2 TerrainTileUv(float2 worldXZ, float2 tileOriginXZ)
{
    // 標本は格子の角。tileCells + 1 枚の図の中で標本の中心に合わせる。
    const float samples = gTerrainTileCells + 1.0f;
    return ((worldXZ - tileOriginXZ) / gTerrainTileSize * gTerrainTileCells + 0.5f) / samples;
}

float TerrainHeightAt(float2 worldXZ, TerrainNode node)
{
    const float2 uv = TerrainTileUv(worldXZ, node.tileOriginXZ);
    const float raw = gTerrainHeight.SampleLevel(gLinearClamp, float3(uv, node.tileLayer), 0.0f);
    return gTerrainHeightMin + raw * gTerrainHeightRange;
}

TerrainVertex SurfaceVS(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const TerrainNode node = gTerrainNodes[instanceId];
    const uint columns = gTerrainGridCells + 1u;
    const uint2 grid = uint2(vertexId % columns, vertexId / columns);
    const float cells = (float)gTerrainGridCells;
    const float2 local = (float2)grid / cells;
    const float2 worldXZ = node.originXZ + local * node.nodeSize;

    // CDLOD（Strugar 2010）：遠くなるほど奇数番の頂点を親の格子へ寄せ、次の級へ継ぎ目なく移る。
    const float approximateHeight = TerrainHeightAt(worldXZ, node);
    const float distanceToCamera = length(float3(worldXZ.x, approximateHeight, worldXZ.y) - gSurfaceCameraPosition);
    const float morph = saturate((distanceToCamera - node.morphStart) / max(node.morphEnd - node.morphStart, 1.0e-3f));
    const float2 oddOffset = frac((float2)grid * 0.5f) * 2.0f / cells;
    const float2 morphedXZ = node.originXZ + (local - oddOffset * morph) * node.nodeSize;

    TerrainVertex output;
    const float height = TerrainHeightAt(morphedXZ, node);
    output.worldPosition = float3(morphedXZ.x, height, morphedXZ.y);
    output.position = mul(gSurfaceViewProj, float4(output.worldPosition, 1.0f));
    output.tileUv = TerrainTileUv(morphedXZ, node.tileOriginXZ);
    output.tileLayer = node.tileLayer;
    return output;
}

float3 TerrainNormalAt(TerrainVertex input)
{
    const float2 normalXZ = gTerrainNormal.Sample(gLinearClamp, float3(input.tileUv, input.tileLayer));
    return normalize(float3(normalXZ.x, sqrt(saturate(1.0f - dot(normalXZ, normalXZ))), normalXZ.y));
}

// 材質の段階までの仮の色。傾きと高さだけで草・土・岩・雪を分ける。
void TerrainPlaceholderSurface(float3 worldPosition, float3 normal, out float3 albedo, out float roughness)
{
    const float slope = 1.0f - normal.y;
    const float3 grass = float3(0.19f, 0.27f, 0.10f);
    const float3 soil = float3(0.27f, 0.21f, 0.15f);
    const float3 rock = float3(0.32f, 0.30f, 0.28f);
    const float3 snow = float3(0.82f, 0.84f, 0.88f);
    albedo = lerp(grass, soil, smoothstep(0.03f, 0.12f, slope));
    albedo = lerp(albedo, rock, smoothstep(0.14f, 0.32f, slope));
    albedo = lerp(albedo, snow, smoothstep(170.0f, 210.0f, worldPosition.y) * (1.0f - smoothstep(0.2f, 0.45f, slope)));
    roughness = lerp(0.92f, 0.78f, smoothstep(0.14f, 0.32f, slope));
}

// ---- 地表材質 ----
// 去重復：Mikkelsen 2022「Practical Real-Time Hex-Tiling」の六角タイル（3 標本を高さで鋭く混ぜる）。
// 崖：Quilez の biplanar の考え方で、主な側面 2 つだけを読む。法線は whiteout（Golus）で組む。
static const float kHexNearMeters = 140.0f;
static const float kHexFadeMeters = 60.0f;

struct TerrainSurfaceSample
{
    float3 albedo;
    float height;
    float3 normal;
    float roughness;
    float ao;
};

float2 TerrainHash2(int2 cell)
{
    const float2 p = float2(dot(float2(cell), float2(127.1f, 311.7f)), dot(float2(cell), float2(269.5f, 183.3f)));
    return frac(sin(p) * 43758.5453f);
}

float TerrainValueNoise(float2 position)
{
    const float2 base = floor(position);
    const float2 f = position - base;
    const float2 u = f * f * (3.0f - 2.0f * f);
    const int2 cell = int2(base);
    return lerp(lerp(TerrainHash2(cell).x, TerrainHash2(cell + int2(1, 0)).x, u.x),
                lerp(TerrainHash2(cell + int2(0, 1)).x, TerrainHash2(cell + int2(1, 1)).x, u.x), u.y);
}

float3 TerrainWhiteoutTop(float2 tangent, float strength, float3 macroNormal)
{
    const float2 xy = tangent * strength;
    const float z = sqrt(saturate(1.0f - dot(xy, xy)));
    return float3(xy.x + macroNormal.x, z * macroNormal.y, xy.y + macroNormal.z);
}

// 一つの投影面。近くは六角タイル、遠くは 2 段の縮尺を混ぜた 1 組。detail の xy は uv の向きの法線。
void TerrainSamplePlanar(float layer, float2 uv, float2 duvdx, float2 duvdy, float near, out float4 color, out float4 detail)
{
    color = 0.0f.xxxx;
    detail = 0.0f.xxxx;
    if (near < 1.0f)
    {
        // 遠くは六角の代わりに、低い周波数で uv をずらして格子の並びを崩し、縮尺の違う 2 組を混ぜる。
        const float2 warp = float2(TerrainValueNoise(uv * 0.13f), TerrainValueNoise(uv.yx * 0.11f + 17.0f)) * 2.5f;
        const float2 warped = uv + warp;
        const float kWide = 0.27f;
        const float4 c0 = gTerrainSurfaceColor.SampleGrad(gLinearWrap, float3(warped, layer), duvdx, duvdy);
        const float4 c1 = gTerrainSurfaceColor.SampleGrad(gLinearWrap, float3(warped.yx * kWide, layer), duvdx.yx * kWide, duvdy.yx * kWide);
        color = lerp(c0, c1, 0.5f);
        detail = gTerrainSurfaceDetail.SampleGrad(gLinearWrap, float3(warped, layer), duvdx, duvdy);
    }
    if (near > 0.0f)
    {
        // 三角格子（Heitz & Neyret の座標）。
        const float2x2 gridToSkewed = float2x2(1.0f, -0.57735027f, 0.0f, 1.15470054f);
        const float2 skewed = mul(gridToSkewed, uv * 3.46410162f);
        const int2 base = int2(floor(skewed));
        const float3 bary = float3(frac(skewed), 0.0f);
        const float third = 1.0f - bary.x - bary.y;
        const float side = third < 0.0f ? 1.0f : 0.0f;
        const float sign2 = 2.0f * side - 1.0f;
        float3 weights = float3(-third * sign2, side - bary.y * sign2, side - bary.x * sign2);
        const int sideInt = int(side);
        const int2 vertices[3] = {base + int2(sideInt, sideInt), base + int2(sideInt, 1 - sideInt), base + int2(1 - sideInt, sideInt)};
        float4 hexColor[3];
        float4 hexDetail[3];
        for (uint i = 0u; i < 3u; ++i)
        {
            const float2 hash = TerrainHash2(vertices[i]);
            const float angle = hash.x * 6.2831853f;
            const float2 axis = float2(cos(angle), sin(angle));
            const float2x2 rotation = float2x2(axis.x, -axis.y, axis.y, axis.x);
            const float2 tileUv = mul(rotation, uv) + hash;
            const float2 dx = mul(rotation, duvdx);
            const float2 dy = mul(rotation, duvdy);
            hexColor[i] = gTerrainSurfaceColor.SampleGrad(gLinearWrap, float3(tileUv, layer), dx, dy);
            hexDetail[i] = gTerrainSurfaceDetail.SampleGrad(gLinearWrap, float3(tileUv, layer), dx, dy);
            // 接線の法線を世界の向きへ戻す（回したぶんを逆に）。
            const float2 tangent = hexDetail[i].xy * 2.0f - 1.0f;
            hexDetail[i].xy = mul(tangent, rotation) * 0.5f + 0.5f;
        }
        // 高さで鋭くして、混ざって眠くなるのを防ぐ。
        weights = pow(max(weights * (0.4f + float3(hexColor[0].a, hexColor[1].a, hexColor[2].a)), 1.0e-4f), 7.0f);
        weights /= weights.x + weights.y + weights.z;
        const float4 nearColor = hexColor[0] * weights.x + hexColor[1] * weights.y + hexColor[2] * weights.z;
        const float4 nearDetail = hexDetail[0] * weights.x + hexDetail[1] * weights.y + hexDetail[2] * weights.z;
        color = near >= 1.0f ? nearColor : lerp(color, nearColor, near);
        detail = near >= 1.0f ? nearDetail : lerp(detail, nearDetail, near);
    }
}

TerrainSurfaceSample TerrainSampleSurface(uint surface, float3 worldPosition, float3 dpdx, float3 dpdy, float3 macroNormal, float near, float cliff)
{
    const float4 params = gTerrainSurfaceParams.Load(int4(surface, 0, 0, 0));
    const float tiling = max(params.x, 0.01f);
    const float layer = (float)surface;
    float4 color = 0.0f.xxxx;
    float3 normal = 0.0f.xxx;
    float roughness = 0.0f;
    float ao = 0.0f;
    if (cliff < 1.0f)
    {
        float4 topColor;
        float4 topDetail;
        TerrainSamplePlanar(layer, worldPosition.xz / tiling, dpdx.xz / tiling, dpdy.xz / tiling, near, topColor, topDetail);
        const float weight = 1.0f - cliff;
        color += topColor * weight;
        normal += TerrainWhiteoutTop(topDetail.xy * 2.0f - 1.0f, params.z, macroNormal) * weight;
        roughness += topDetail.z * weight;
        ao += topDetail.w * weight;
    }
    if (cliff > 0.0f)
    {
        // 主な側面 2 つ（x 向きと z 向き）を面の向きで混ぜる。図の v は世界の下向き。
        float2 sideWeights = pow(abs(macroNormal.xz), 4.0f);
        sideWeights /= max(sideWeights.x + sideWeights.y, 1.0e-4f);
        float3 sideNormal = 0.0f.xxx;
        float4 sideColor = 0.0f.xxxx;
        float2 sideDetail = 0.0f.xx;
        if (sideWeights.x > 0.02f)
        {
            float4 planeColor;
            float4 sampled;
            TerrainSamplePlanar(layer, float2(worldPosition.z, -worldPosition.y) / tiling, float2(dpdx.z, -dpdx.y) / tiling,
                                float2(dpdy.z, -dpdy.y) / tiling, near, planeColor, sampled);
            const float2 tangent = (sampled.xy * 2.0f - 1.0f) * params.z;
            const float z = sqrt(saturate(1.0f - dot(tangent, tangent)));
            sideNormal += float3(z * macroNormal.x, macroNormal.y - tangent.y, macroNormal.z + tangent.x) * sideWeights.x;
            sideColor += planeColor * sideWeights.x;
            sideDetail += sampled.zw * sideWeights.x;
        }
        if (sideWeights.y > 0.02f)
        {
            float4 planeColor;
            float4 sampled;
            TerrainSamplePlanar(layer, float2(worldPosition.x, -worldPosition.y) / tiling, float2(dpdx.x, -dpdx.y) / tiling,
                                float2(dpdy.x, -dpdy.y) / tiling, near, planeColor, sampled);
            const float2 tangent = (sampled.xy * 2.0f - 1.0f) * params.z;
            const float z = sqrt(saturate(1.0f - dot(tangent, tangent)));
            sideNormal += float3(macroNormal.x + tangent.x, macroNormal.y - tangent.y, z * macroNormal.z) * sideWeights.y;
            sideColor += planeColor * sideWeights.y;
            sideDetail += sampled.zw * sideWeights.y;
        }
        color += sideColor * cliff;
        normal += sideNormal * cliff;
        roughness += sideDetail.x * cliff;
        ao += sideDetail.y * cliff;
    }
    TerrainSurfaceSample result;
    result.albedo = color.rgb;
    result.height = color.a;
    result.normal = normal;
    result.roughness = roughness;
    result.ao = ao;
    return result;
}

// 周りの 4 標本の制御図から、この画素で一番効く 2 材質を選ぶ。
void TerrainPickSurfaces(float2 tileUv, uint tileLayer, out uint first, out uint second, out float blend)
{
    const float samples = gTerrainTileCells + 1.0f;
    const float2 texel = tileUv * samples - 0.5f;
    const float2 base = floor(texel);
    const float2 f = texel - base;
    const int last = int(samples) - 1;
    const int2 cell = int2(base);
    const float cornerWeights[4] = {(1.0f - f.x) * (1.0f - f.y), f.x * (1.0f - f.y), (1.0f - f.x) * f.y, f.x * f.y};
    uint ids[8];
    float weights[8];
    uint count = 0u;
    for (uint corner = 0u; corner < 4u; ++corner)
    {
        const int2 at = min(cell + int2(corner & 1u, corner >> 1u), int2(last, last));
        const uint value = gTerrainControl.Load(int4(at, tileLayer, 0));
        const float overlay = float((value >> 10u) & 255u) / 255.0f;
        const uint materials[2] = {value & 31u, (value >> 5u) & 31u};
        const float amounts[2] = {cornerWeights[corner] * (1.0f - overlay), cornerWeights[corner] * overlay};
        for (uint part = 0u; part < 2u; ++part)
        {
            if (amounts[part] <= 0.0f)
                continue;
            uint index = 0u;
            while (index < count && ids[index] != materials[part])
                ++index;
            if (index == count)
            {
                ids[count] = materials[part];
                weights[count] = 0.0f;
                ++count;
            }
            weights[index] += amounts[part];
        }
    }
    first = 0u;
    second = 0u;
    float firstWeight = -1.0f;
    float secondWeight = 0.0f;
    for (uint index = 0u; index < count; ++index)
    {
        if (weights[index] > firstWeight)
        {
            second = first;
            secondWeight = max(firstWeight, 0.0f);
            first = ids[index];
            firstWeight = weights[index];
        }
        else if (weights[index] > secondWeight)
        {
            second = ids[index];
            secondWeight = weights[index];
        }
    }
    blend = secondWeight / max(firstWeight + secondWeight, 1.0e-4f);
}

// 画素の地表：2 材質を高さで混ぜ、広い明暗のむらを足す。
TerrainSurfaceSample TerrainResolveSurface(TerrainVertex input, float3 macroNormal)
{
    uint first;
    uint second;
    float blend;
    TerrainPickSurfaces(input.tileUv, input.tileLayer, first, second, blend);
    const uint limit = gTerrainSurfaceCount - 1u;
    first = min(first, limit);
    second = min(second, limit);
    const float distanceToCamera = length(input.worldPosition - gSurfaceCameraPosition);
    const float near = saturate(1.0f - (distanceToCamera - kHexNearMeters) / kHexFadeMeters);
    const float cliff = 1.0f - smoothstep(0.55f, 0.75f, macroNormal.y);
    const float3 dpdx = ddx(input.worldPosition);
    const float3 dpdy = ddy(input.worldPosition);
    TerrainSurfaceSample a = TerrainSampleSurface(first, input.worldPosition, dpdx, dpdy, macroNormal, near, cliff);
    if (blend > 0.004f)
    {
        const TerrainSurfaceSample b = TerrainSampleSurface(second, input.worldPosition, dpdx, dpdy, macroNormal, near, cliff);
        const float contrast = max(gTerrainSurfaceParams.Load(int4(first, 0, 0, 0)).y, 0.01f);
        const float heightA = a.height + (1.0f - blend);
        const float heightB = b.height + blend;
        const float top = max(heightA, heightB) - contrast;
        const float weightA = max(heightA - top, 0.0f);
        const float weightB = max(heightB - top, 0.0f);
        const float t = weightB / max(weightA + weightB, 1.0e-4f);
        a.albedo = lerp(a.albedo, b.albedo, t);
        a.height = lerp(a.height, b.height, t);
        a.normal = lerp(a.normal, b.normal, t);
        a.roughness = lerp(a.roughness, b.roughness, t);
        a.ao = lerp(a.ao, b.ao, t);
    }
    // 広い範囲の明暗のむら（遠景の繰り返しを隠す）。
    const float2 wide = input.worldPosition.xz;
    const float macro = TerrainValueNoise(wide * 0.019f) * 0.6f + TerrainValueNoise(wide * 0.083f) * 0.4f;
    a.albedo *= lerp(0.82f, 1.14f, macro);
    a.normal = normalize(a.normal);
    return a;
}

float4 SurfaceGeometryPS(TerrainVertex input) : SV_TARGET0
{
    const float3 normal = TerrainNormalAt(input);
    float3 albedo;
    float roughness;
    TerrainPlaceholderSurface(input.worldPosition, normal, albedo, roughness);
    return HikariPackSceneSurface(normal, roughness, 0.0f);
}

struct TerrainGeometryFeatureOutput
{
    float4 surface : SV_TARGET0;
    uint2 features : SV_TARGET1;
    float4 outlineStyle : SV_TARGET2;
};

TerrainGeometryFeatureOutput SurfaceGeometryFeaturePS(TerrainVertex input)
{
    TerrainGeometryFeatureOutput output = (TerrainGeometryFeatureOutput)0;
    output.surface = SurfaceGeometryPS(input);
    return output;
}

float4 SurfaceForwardPS(TerrainVertex input) : SV_TARGET
{
    const float3 worldPosition = input.worldPosition;
    const float3 normal = TerrainNormalAt(input);
    const float3 view = HikariCameraViewDirection(worldPosition);
    float3 albedo;
    float roughness;
    float3 shadingNormal = normal;
    float surfaceOcclusion = 1.0f;
    if (gTerrainSurfaceCount != 0u)
    {
        const TerrainSurfaceSample surface = TerrainResolveSurface(input, normal);
        albedo = surface.albedo;
        roughness = surface.roughness;
        shadingNormal = surface.normal;
        surfaceOcclusion = surface.ao;
    }
    else
    {
        TerrainPlaceholderSurface(worldPosition, normal, albedo, roughness);
    }
    if (gDebugView == HIKARI_DEBUG_VIEW_NEUTRAL_CLAY)
    {
        albedo = 0.5f.xxx;
    }

    float4 nearField = float4(0.0f, 0.0f, 0.0f, 1.0f);
    if (gSsaoEnabled > 0.5f)
    {
        nearField = gSsaoTex.Load(int3(int2(input.position.xy), 0));
        nearField.rgb = HikariRemovePreExposure(max(nearField.rgb, 0.0f.xxx), gViewExposure);
    }

    const float3 light = normalize(-gDirectionalDir.xyz);
    float shadowVisibility = 1.0f;
    if (dot(normal, light) > 0.0f)
    {
        if ((gScreenSpaceTraceFlags & HIKARI_SCREEN_SPACE_RAY_SHADOW_VALID) != 0u && gShadowEnabled != 0u)
        {
            // 光線は距離の先も地形の代理を見ているので、影の距離で薄くしない。
            shadowVisibility = gRayTracedShadow.Load(int3(int2(input.position.xy), 0));
        }
        else
        {
            shadowVisibility = SampleDirectionalVisibility(worldPosition, normal, 1u) *
                HikariContactShadow(worldPosition, normal, input.position.xy, 1u);
        }
    }

    const HikariPbrShadingContext pbr =
        HikariBuildForwardPbrShadingContext(albedo, 0.0f, roughness, 1.0f.xxx, 1.0f, shadingNormal, view);
    const HikariAmbientIblLobes ambient =
        HikariEvaluateAmbientIblLobes(pbr, surfaceOcclusion, nearField, worldPosition, normal, 0.0f.xxxx, 0.0f);
    const float3 sunRadiance =
        gDirectionalColor.rgb * (gDirectionalIntensity * HikariForwardCloudSolarVisibility(worldPosition));
    HikariDirectPbrLobes directional = HikariEvaluateDirectionalPbrLobes(pbr, light, sunRadiance, gDirectionalDir.w);
    directional.diffuse *= shadowVisibility;
    directional.specular *= shadowVisibility;
    const HikariDirectPbrLobes local =
        AccumulateLocalLightPbrLobes(pbr, worldPosition, input.position.xy, normal, 1u);
    if (gDebugView == HIKARI_DEBUG_VIEW_DIRECT_DIFFUSE)
        return HikariPreExposeView(float4(directional.diffuse + local.diffuse, 1.0f));
    if (gDebugView == HIKARI_DEBUG_VIEW_DIRECTIONAL_SPECULAR)
        return HikariPreExposeView(float4(directional.specular, 1.0f));
    if (gDebugView == HIKARI_DEBUG_VIEW_LOCAL_SPECULAR)
        return HikariPreExposeView(float4(local.specular, 1.0f));
    if (gDebugView == HIKARI_DEBUG_VIEW_INDIRECT_DIFFUSE)
        return HikariPreExposeView(float4(ambient.diffuse, 1.0f));
    if (gDebugView == HIKARI_DEBUG_VIEW_INDIRECT_SPECULAR)
        return HikariPreExposeView(float4(ambient.specular, 1.0f));
    const float3 color = directional.diffuse + directional.specular + local.diffuse + local.specular +
        ambient.diffuse + ambient.specular;
    return HikariPreExposeView(float4(ApplyFog(color, worldPosition), 1.0f));
}
