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
// 色の図（瓦片ごと）：RGB は線形の平方根、A は環境光の遮蔽。地表の色はどの距離でもこれで決まる。
Texture2DArray<float4> gTerrainColorMap : register(t7, space10);

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
    uint gTerrainColorMapReady;
    float gTerrainSnowLine;
    float gTerrainSnowAmount;
    float gTerrainWetness;
    float gTerrainReserved;
    float gTerrainOverlayHeightMin;
    float gTerrainOverlayHeightRange;
    float gTerrainSeaLevel;
    float gTerrainReserved2;
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

#include "Include/Terrain/HIKARI_TerrainKitSurfaceShading.hlsli"

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

// 編集の時に重ねて見る数据図（TerrainOverlay と番号を揃える）。自分で光らせ、光の影響を受けない。
float3 TerrainOverlayColor(TerrainVertex input, float3 normal)
{
    const float3 cool = float3(0.05f, 0.18f, 0.55f);
    const float3 warm = float3(0.85f, 0.15f, 0.05f);
    const uint control = gTerrainControl.Load(int4(int2(input.tileUv * (gTerrainTileCells + 1.0f)), input.tileLayer, 0));
    const uint surface = min(control & 31u, max(gTerrainSurfaceCount, 1u) - 1u);
    if (gTerrainDebugFlags == 1u)
    {
        // 高さ：青から赤、10 m ごとの等高線。
        const float share = saturate((input.worldPosition.y - gTerrainOverlayHeightMin) / gTerrainOverlayHeightRange);
        const float band = frac(input.worldPosition.y / 10.0f);
        const float contour = smoothstep(0.0f, 0.06f, band) * smoothstep(1.0f, 0.94f, band);
        return lerp(cool, warm, share) * lerp(0.35f, 1.0f, contour);
    }
    if (gTerrainDebugFlags == 2u)
    {
        // 傾き：平らは緑、45 度で赤。
        const float slopeDegrees = degrees(acos(saturate(normal.y)));
        return lerp(float3(0.1f, 0.55f, 0.1f), warm, saturate(slopeDegrees / 45.0f));
    }
    if (gTerrainDebugFlags == 3u)
    {
        return lerp(float3(0.5f, 0.45f, 0.35f), float3(0.05f, 0.25f, 0.8f), (float)((control >> 28u) & 15u) / 15.0f);
    }
    if (gTerrainDebugFlags == 4u)
    {
        // 地表：枠ごとに違う色。
        const float hue = frac((float)surface * 0.618034f);
        return saturate(abs(frac(hue + float3(0.0f, 0.667f, 0.333f)) * 6.0f - 3.0f) - 1.0f) * 0.7f + 0.15f;
    }
    // 草の密度：無ければ灰、濃いほど緑。
    const float density = gTerrainSurfaceParams.Load(int4(surface, 2, 0, 0)).x;
    return lerp(float3(0.25f, 0.25f, 0.25f), float3(0.1f, 0.75f, 0.1f), saturate(density));
}

float4 SurfaceForwardPS(TerrainVertex input) : SV_TARGET
{
    const float3 worldPosition = input.worldPosition;
    const float3 normal = TerrainNormalAt(input);
    if (gTerrainDebugFlags != 0u)
    {
        const float shade = 0.65f + 0.35f * saturate(dot(normal, normalize(float3(0.4f, 0.8f, 0.3f))));
        return float4(TerrainOverlayColor(input, normal) * shade, 1.0f);
    }
    const float3 view = HikariCameraViewDirection(worldPosition);
    float3 albedo;
    float roughness;
    float3 shadingNormal = normal;
    float surfaceOcclusion = 1.0f;
    if (gTerrainSurfaceCount != 0u)
    {
        const TerrainSurfaceSample surface = TerrainShadeSurface(input, normal);
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
