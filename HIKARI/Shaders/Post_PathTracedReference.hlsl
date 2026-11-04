#include "Include/Lighting/HIKARI_RuntimeRayLighting.hlsli"
#include "Include/Lighting/HIKARI_PathBsdf.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"
#include "Include/Color/HIKARI_ViewExposure.hlsli"

// 路径追跡の参照（調試視図 PathTraced / PathTracedIndirectDiffuse）。実行時の加速構造へカメラから画素ごとに路径を
// 一本飛ばし、相機と場面が止まっている間はフレームをまたいで和を取る。各頂点で太陽と局所光 1 盞を NEE（光線追跡の
// 反射と同じ HikariRayHitRadiance）、次の向きは拡散（余弦）か鏡面（GGX の見える法線）を反射率の比で選ぶ。空へ抜けた
// 光線は環境の立方体を読む。2 回目の反射から俄羅斯輪盤。透過面は路径から見えない、片面の裏は光を吸う（光線追跡の
// 反射と同じ約束）。霧・体積光・水・画面材質は扱わない。

cbuffer PathTracedReferenceCB : register(b0)
{
    float4x4 gInvViewProj;     // ずらし無しの投影の逆
    float4 gScreenParams;      // xy 大きさ, zw 1/大きさ
    uint4 gPathParams;         // x 標本番号（0 で和を捨てる）, y 最大反射回数, z 1 = 初回の面の間接拡散だけ
    float4 gViewExposure;
    HikariRayLightingData gRayLighting;
};

RWTexture2D<float4> gAccumulation : register(u0, space9);
Texture2D<float4> gAccumulationRead : register(t0, space9);
SamplerState gLinearClamp : register(s0);
SamplerState gMaterialWrap : register(s1);

float2 HikariPathRandom2(inout uint state)
{
    const float x = HikariRandom01(state);
    return float2(x, HikariRandom01(state));
}

// 画素の中の一点を通るカメラの光線（透視でも正投影でも、近い面から遠い面へ）。
bool HikariPathCameraRay(float2 uv, out RayDesc ray)
{
    float3 nearPoint;
    float3 farPoint;
    ray = (RayDesc)0;
    if (!HikariReconstructWorldPosition(uv, 1.0e-4f, gInvViewProj, nearPoint) ||
        !HikariReconstructWorldPosition(uv, 0.99f, gInvViewProj, farPoint))
        return false;
    ray.Origin = nearPoint;
    ray.Direction = normalize(farPoint - nearPoint);
    ray.TMin = 0.0f;
    ray.TMax = 1.0e7f;
    return all(isfinite(ray.Direction));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint2 pixel = id.xy;
    if (any(pixel >= uint2(gScreenParams.xy)))
        return;
    uint seed = HikariPcgHash(pixel.x * 73856093u ^ pixel.y * 19349663u ^ HikariPcgHash(gPathParams.x));
    const float2 uv = (float2(pixel) + HikariPathRandom2(seed)) * gScreenParams.zw;
    const bool indirectOnly = gPathParams.z != 0u;
    float3 radiance = 0.0f.xxx;
    float3 throughput = 1.0f.xxx;
    bool primaryHit = false;
    RayDesc ray;
    if (HikariPathCameraRay(uv, ray))
    {
        [loop]
        for (uint bounce = 0u; bounce <= gPathParams.y; ++bounce)
        {
            HikariRaySurface hit;
            if (!HikariTraceRuntimeScene(ray, gRayLighting.scene.x, gRayLighting.scene.y, gRayLighting.time,
                    gMaterialWrap, HIKARI_RAY_REPORT_BACKFACES, hit))
            {
                if (bounce != 0u)
                    radiance += throughput * HikariRayEnvironmentRadiance(gRayLighting, ray.Direction, gLinearClamp);
                break;
            }
            primaryHit = primaryHit || bounce == 0u;
            const HikariMeshMaterialData material = HikariGetMeshMaterialData(hit.materialIndex);
            if (hit.backFace != 0u && (material.instanceFlags & HIKARI_PROGRAM_FLAG_DOUBLE_SIDED) == 0u)
                break;
            const float3 view = -ray.Direction;
            const bool skipDirect = indirectOnly && bounce == 0u;
            if (!skipDirect)
                radiance += throughput * HikariRayHitRadiance(gRayLighting, hit, view, HikariPathRandom2(seed),
                    0.0f.xxx, 0.0f.xxx, true, gMaterialWrap);
            if (bounce == gPathParams.y || (material.materialFlags & HIKARI_MATERIAL_FLAG_UNLIT) != 0u)
                break;

            const HikariPbrShadingContext pbr = HikariBuildPbrShadingContextWithFootprint(hit.baseColor.rgb,
                hit.pbr.metallic, hit.pbr.roughness, hit.pbr.specularColor, hit.pbr.specularFactor, hit.normal, view,
                0.0f.xxx, 0.0f.xxx);
            const float lobe = HikariRandom01(seed);
            float3 direction;
            float3 weight;
            if (!HikariSamplePathBsdf(pbr, hit.geometricNormal, skipDirect, lobe, HikariPathRandom2(seed), direction,
                    weight))
                break;
            throughput *= weight;
            if (bounce >= 2u)
            {
                const float survival = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05f, 0.95f);
                if (HikariRandom01(seed) >= survival)
                    break;
                throughput /= survival;
            }
            ray.Origin = hit.position + hit.geometricNormal * 0.002f;
            ray.Direction = direction;
            ray.TMin = 0.001f;
            ray.TMax = 1.0e7f;
        }
    }
    if (!all(isfinite(radiance)))
        radiance = 0.0f.xxx;
    // 空が見える標本は数えない（空の画素はラスターの空のまま残す）。
    const float4 sample = primaryHit ? float4(max(radiance, 0.0f.xxx), 1.0f) : 0.0f.xxxx;
    gAccumulation[pixel] = gPathParams.x == 0u ? sample : gAccumulation[pixel] + sample;
}

struct PathCompositeVertex
{
    float4 position : SV_Position;
};

PathCompositeVertex VSMain(uint vertexId : SV_VertexID)
{
    PathCompositeVertex output;
    const float2 position = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(position * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

// 面に当たった標本の平均を場面色へ書く（前露出して、Forward の出力と同じ尺度）。
float4 PSComposite(PathCompositeVertex input) : SV_Target
{
    const float4 sum = gAccumulationRead.Load(int3(int2(input.position.xy), 0));
    if (sum.w < 0.5f)
        discard;
    return float4(HikariPreExpose(sum.rgb / sum.w, gViewExposure), 1.0f);
}
