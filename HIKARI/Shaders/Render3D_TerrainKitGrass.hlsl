// 地形モジュールの草。Eric Wohllaib「Procedural Grass in Ghost of Tsushima」（GDC 2021）の考え方を mesh shader で作る。
// 緩衝を持たず、世界の格子と地形の図だけから一枚ずつ葉を作る。どの地表材質に生えるかは材質の制御図で決まるので、
// 筆や自動規則で材質を塗るとそのまま草が変わる。root は Render3D_TerrainKitSurface と同じ。
// 前向きの pass だけで描き、そこで深度も書く。葉は細く、深度の前段は mesh shader の計算が二重になるだけで得が少ない。
// 光追の影と SSAO は葉の無い深度（地面）で求まり、葉は同じ画素の影を読む（葉一枚の自己影は元から無い）。
//   JointPalette -> t0, space10（小区画）、ClusterGeometryPool -> t1..t8, space10（瓦片の図）、MeshletVisibleRanges -> b3
#include "Include/Forward/HIKARI_ForwardCommon.hlsli"
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/Surface/HIKARI_SurfacePartition.hlsli"
#include "Include/Forward/HIKARI_AmbientIblCommon.hlsli"
#include "Include/Lighting/HIKARI_SurfaceDirectLighting.hlsli"
#include "Include/Forward/HIKARI_ContactShadow.hlsli"
#include "Include/Forward/HIKARI_ForwardMaterialInputs.hlsli"
#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"

struct GrassPatch
{
    float2 originXZ;
    float2 tileOriginXZ;
    uint level;
    uint tileLayer;
    uint flips;    // bit 0, 1：x, z を逆順に描く（カメラに近い葉から描いて、隠れる画素の着色を減らす）
    uint reserved;
};

StructuredBuffer<GrassPatch> gGrassPatches : register(t0, space10);
Texture2DArray<float> gTerrainHeight : register(t1, space10);
Texture2DArray<float2> gTerrainNormal : register(t2, space10);
Texture2DArray<uint> gTerrainControl : register(t3, space10);
// 材質の表（列 = 枠の番号）。2 行目から草の値（HIKARI_TerrainKitRenderData.cpp と揃える）。
Texture2DArray<float4> gTerrainSurfaceParams : register(t6, space10);
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
    float gGrassFirstRange; // 一番細かい段の葉が消える距離 [m]。段ごとに倍
    float gGrassDensity;    // 全体の密度（0..1）
    uint gGrassSurfaceCount;
    uint gGrassColorMapReady;
    float2 gGrassWindDirection;
    float gGrassWindStrength;
    float gGrassWindSpeed;
    float gGrassWaterLevel; // これより低い地面（海の中）には生やさない
    float3 gGrassReserved;
};

// 小区画は辺 64 個の格子点、mesh shader の一組は 4x4 の 16 枚。格子の間隔は段ごとに倍になる。
static const float GRASS_BASE_SPACING = 0.125f;
static const uint GRASS_TOP_LEVEL = 3u;
static const uint GRASS_BLADES_PER_GROUP = 16u;
static const uint GRASS_HIGH_PAIRS = 7u; // 高い段の葉：7 段の左右と先端で 15 頂点、13 三角形
static const uint GRASS_LOW_PAIRS = 3u;  // 低い段の葉：3 段の左右と先端で 7 頂点、5 三角形
static const uint GRASS_LAST_HIGH_LEVEL = 0u;

static const uint GRASS_PARAM_SHAPE = 2u;  // 密度、高さの下限、上限、幅
static const uint GRASS_PARAM_POSE = 3u;   // 傾き、傾きの揺らぎ（ラジアン）、曲がり、塊の大きさ
static const uint GRASS_PARAM_CLUMP = 4u;  // 塊の高さの差、中心への寄り、向きの揃い、中心から離れる向き
static const uint GRASS_PARAM_ROOT = 5u;   // 根元の色（線形）、透過
static const uint GRASS_PARAM_TIP = 6u;    // 先端の色（線形）、粗さ
static const uint GRASS_PARAM_EXTRA = 7u;  // 塊の色の差、地面の色の混ざり、風の効き、根元の暗さ
static const uint GRASS_PARAM_PLUME = 8u;  // 穂の色（線形）、穂をつける葉の割合

// 段ごとの葉の上の位置（頂点を曲がりの大きい先端へ寄せた値。i / 段数 の 0.75 乗）と、その位置の幅。
static const float GRASS_HIGH_T[8] = {0.0000f, 0.2324f, 0.3908f, 0.5297f, 0.6572f, 0.7770f, 0.8908f, 1.0000f};
static const float GRASS_LOW_T[4] = {0.0000f, 0.4387f, 0.7378f, 1.0000f};
static const float GRASS_HIGH_WIDTH[8] = {0.7200f, 0.8421f, 0.7246f, 0.6124f, 0.4986f, 0.3771f, 0.2370f, 0.0000f};
static const float GRASS_LOW_WIDTH[4] = {0.7200f, 0.6870f, 0.4189f, 0.0000f};
// 一番細かい段の葉の密度の倍率（近くだけ費用が大きいので減らし、幅で補う）。
static const float GRASS_FINEST_DENSITY = 0.6f;

struct GrassParams
{
    float4 shape;
    float4 pose;
    float4 clump;
    float4 root;
    float4 tip;
    float4 extra;
    float4 plume;
};

GrassParams GrassLoadParams(uint surface)
{
    GrassParams params;
    params.shape = gTerrainSurfaceParams.Load(int4(surface, GRASS_PARAM_SHAPE, 0, 0));
    params.pose = gTerrainSurfaceParams.Load(int4(surface, GRASS_PARAM_POSE, 0, 0));
    params.clump = gTerrainSurfaceParams.Load(int4(surface, GRASS_PARAM_CLUMP, 0, 0));
    params.root = gTerrainSurfaceParams.Load(int4(surface, GRASS_PARAM_ROOT, 0, 0));
    params.tip = gTerrainSurfaceParams.Load(int4(surface, GRASS_PARAM_TIP, 0, 0));
    params.extra = gTerrainSurfaceParams.Load(int4(surface, GRASS_PARAM_EXTRA, 0, 0));
    params.plume = gTerrainSurfaceParams.Load(int4(surface, GRASS_PARAM_PLUME, 0, 0));
    return params;
}

// 組で共通の値（法線と風と地面の色は葉ごとに読まない。組は最大でも 2 m 四方で、どれも緩やかに変わる）。
struct GrassBlock
{
    float3 groundNormal;
    float3 up;
    float wind;
    float3 ground;
    float cameraHeight; // カメラとの距離を地面の高さで測るための、組の中心の高さ
};

// 一枚の葉：曲線の制御点と、頂点ごとに要る値。
struct GrassBlade
{
    float3 root;
    float width;
    float3 p1;
    float morph;
    float3 p2;
    float phase;
    float3 p3;
    float bobAmount;
    float3 lean;
    float thicken;     // 葉が視線に対して横向きの時に太らせる量
    float3 side;
    float sideLight;   // 横に見た時の明暗（丸みの代わり）
    float3 screenSide; // 太らせる向き
    float fog;
    float plume;       // 穂をつける葉は 1
    float3 sunLitRoot;
    float3 sunLitTip;
    float3 ambientRoot;
    float3 ambientTip;
};

// 光は一枚につき二か所で決めて頂点で補間する。画素は影を読んで足すだけ（細い葉は画素の呼び出しが多く、全部を画素で
// 計算すると 4 ms を超え、頂点ごとに計算しても 3 ms かかった）。
struct GrassVertex
{
    float4 position : SV_POSITION;
    float3 sunLit : TEXCOORD0;  // 太陽の直射、透過、反射（影の前）
    float3 ambient : TEXCOORD1; // 探針と空の光（遮蔽の後）
    float across : TEXCOORD2;   // 横（-1..1）
    float fog : TEXCOORD3;
};

// 組で共通の光の入力。
struct GrassLighting
{
    float3 light;     // 太陽へ向かう方向
    float3 sun;       // 雲の影を掛けた太陽の放射
    float3 probes;    // 探針（足りない分は空の光で埋めた E/pi）
    float3 view;      // カメラへ向かう方向（組で一つ）
    float3 halfway;
    float toSunGlow;  // 太陽の方を見ている時の透過の強まり（頂点ごとには変えない）
};

#include "Include/Terrain/HIKARI_TerrainKitGrassBlade.hlsli"

// 一組 32 本：前の 16 本が葉を一枚ずつ判定して作り、頂点は全部の thread で分けて書く（1 本で回すと波の 31 本が遊ぶ）。
static const uint GRASS_THREADS = 32u;
groupshared uint gGrassAliveMask;
groupshared GrassBlade gGrassBlades[GRASS_BLADES_PER_GROUP];

[outputtopology("triangle")]
[numthreads(GRASS_THREADS, 1, 1)]
void SurfaceMS(
    uint3 groupId : SV_GroupID,
    uint thread : SV_GroupIndex,
    out vertices GrassVertex vertices[240],
    out indices uint3 triangles[208])
{
    const GrassPatch patch = gGrassPatches[groupId.x >> 8u];
    const uint blockIndex = groupId.x & 255u;
    const int step = (int)(1u << patch.level);
    const int2 patchCell = (int2)round(patch.originXZ / GRASS_BASE_SPACING);
    // カメラに近い組と葉から描く。
    const bool flipX = (patch.flips & 1u) != 0u;
    const bool flipZ = (patch.flips & 2u) != 0u;
    const int2 blockXZ = int2(flipX ? 15u - (blockIndex & 15u) : (blockIndex & 15u),
        flipZ ? 15u - (blockIndex >> 4u) : (blockIndex >> 4u));
    const int2 blockCell = patchCell + blockXZ * (4 * step);
    const float blockSpan = 4.0f * GRASS_BASE_SPACING * (float)step;
    const float2 blockCenterXZ = (float2)blockCell * GRASS_BASE_SPACING + blockSpan * 0.5f;
    const float3 camera = gSurfaceCameraPosition;

    // 組ごとの視錐台の判定。高さは中心の一点だけ読む（葉の高さと斜面の分を半径に足す）。
    const float2 centerLocal = clamp(blockCenterXZ - patch.tileOriginXZ, 0.0f, gTerrainTileSize);
    const float centerHeight =
        GrassTerrainHeight(GrassTileUv(patch.tileOriginXZ + centerLocal, patch.tileOriginXZ), patch.tileLayer);
    const float3 center = float3(blockCenterXZ.x, centerHeight + 0.5f, blockCenterXZ.y);
    const float radius = blockSpan * 1.4f + 2.0f;
    const float4 row0 = float4(gSurfaceViewProj[0][0], gSurfaceViewProj[0][1], gSurfaceViewProj[0][2], gSurfaceViewProj[0][3]);
    const float4 row1 = float4(gSurfaceViewProj[1][0], gSurfaceViewProj[1][1], gSurfaceViewProj[1][2], gSurfaceViewProj[1][3]);
    const float4 row3 = float4(gSurfaceViewProj[3][0], gSurfaceViewProj[3][1], gSurfaceViewProj[3][2], gSurfaceViewProj[3][3]);
    const float4 planes[4] = {row3 + row0, row3 - row0, row3 + row1, row3 - row1};
    bool inside = true;
    for (uint index = 0u; index < 4u; ++index)
    {
        inside = inside && dot(planes[index], float4(center, 1.0f)) >= -radius * length(planes[index].xyz);
    }

    // 葉の有無を葉ごとに決め、印の並びで詰めた番号を付ける（出力の数を宣言するまで頂点を書けない）。
    if (thread == 0u)
        gGrassAliveMask = 0u;
    GroupMemoryBarrierWithGroupSync();
    int2 baseCell = int2(0, 0);
    uint surface = 0u;
    bool exists = false;
    if (inside && thread < GRASS_BLADES_PER_GROUP)
    {
        const int2 cell = int2(flipX ? 3u - (thread & 3u) : (thread & 3u), flipZ ? 3u - (thread >> 2u) : (thread >> 2u));
        baseCell = blockCell + cell * step;
        exists = GrassBladeExists(patch, baseCell, camera.xz, surface);
        if (exists)
            InterlockedOr(gGrassAliveMask, 1u << thread);
    }
    GroupMemoryBarrierWithGroupSync();
    const uint aliveMask = gGrassAliveMask;
    const uint alive = countbits(aliveMask);
    const uint pairs = patch.level <= GRASS_LAST_HIGH_LEVEL ? GRASS_HIGH_PAIRS : GRASS_LOW_PAIRS;
    const uint bladeVertices = pairs * 2u + 1u;
    const uint bladeTriangles = (pairs - 1u) * 2u + 1u;
    SetMeshOutputCounts(alive * bladeVertices, alive * bladeTriangles);

    if (exists)
    {
        const uint index = countbits(aliveMask & ((1u << thread) - 1u));
        const GrassBlock block = GrassBuildBlock(blockCenterXZ, patch);
        const GrassLighting lighting = GrassReadLighting(center, camera);
        gGrassBlades[index] = GrassBuildBlade(patch, block, lighting, GrassLoadParams(surface), baseCell, camera);
        const uint firstVertex = index * bladeVertices;
        const uint firstTriangle = index * bladeTriangles;
        for (uint row = 0u; row + 1u < pairs; ++row)
        {
            const uint base = firstVertex + row * 2u;
            triangles[firstTriangle + row * 2u] = uint3(base, base + 1u, base + 2u);
            triangles[firstTriangle + row * 2u + 1u] = uint3(base + 2u, base + 1u, base + 3u);
        }
        const uint last = firstVertex + (pairs - 1u) * 2u;
        triangles[firstTriangle + bladeTriangles - 1u] = uint3(last, last + 1u, last + 2u);
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint vertexIndex = thread; vertexIndex < alive * bladeVertices; vertexIndex += GRASS_THREADS)
    {
        const uint blade = vertexIndex / bladeVertices;
        vertices[vertexIndex] = GrassBuildVertex(gGrassBlades[blade], vertexIndex - blade * bladeVertices, pairs);
    }
}

float4 SurfaceForwardPS(GrassVertex input) : SV_TARGET
{
    float shadowVisibility = 1.0f;
    if ((gScreenSpaceTraceFlags & HIKARI_SCREEN_SPACE_RAY_SHADOW_VALID) != 0u && gShadowEnabled != 0u)
    {
        shadowVisibility = gRayTracedShadow.Load(int3(int2(input.position.xy), 0));
    }
    // 葉脈：中央をわずかに明るく、縁を暗く。
    const float vein = 1.04f - 0.2f * input.across * input.across;
    const float3 direct = input.sunLit * shadowVisibility;
    if (gDebugView == HIKARI_DEBUG_VIEW_DIRECT_DIFFUSE)
        return HikariPreExposeView(float4(direct, 1.0f));
    if (gDebugView == HIKARI_DEBUG_VIEW_INDIRECT_DIFFUSE)
        return HikariPreExposeView(float4(input.ambient, 1.0f));
    const float3 color = (direct + input.ambient) * vein;
    return HikariPreExposeView(float4(lerp(color, gFogColorDensity.rgb, input.fog), 1.0f));
}
