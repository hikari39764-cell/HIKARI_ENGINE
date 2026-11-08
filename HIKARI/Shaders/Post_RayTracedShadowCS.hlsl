#include "Include/Lighting/HIKARI_RuntimeRayLighting.hlsli"
#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"

// 光線追跡の太陽の影。場面の深度から画素の位置を戻し、太陽の円錐の中へ数本の影光線を飛ばして可視率（1 = 照らされる）を
// 書く。Forward はこの面の可視率でカスケード影を置き換える（半透明や地形は従来どおりカスケードを読む）。
// 円錐の半角は太陽の角半径 × 半影の倍率。可視率を独立に時間積分し、色の TAA が無い時も半影を安定させる。

cbuffer RayTracedShadowCB : register(b0)
{
    float4x4 gInvViewProj;
    float4 gCameraPosition;
    float4 gScreenParams;   // xy 大きさ, zw 1/大きさ
    float4 gSunCone;        // xyz 太陽へ向かう単位ベクトル, w 円錐の半角 [rad]
    float4 gRayParams;      // x 最大距離 [m], y 標本数, z フレーム番号, w 法線方向の始点のずらし [m]
    float4 gInputFlags;     // x 前フレームの可視率の履歴が使えれば 1, y 代理だけを見る光線の長さ [m]（無ければ 0）
    HikariRayLightingData gRayLighting;
    float4x4 gPreviousViewProj;
    float4 gPreviousCamera;
};

Texture2D<float> gSceneDepth : register(t0, space9);
Texture2D<float4> gSceneSurface : register(t1, space9);
Texture2D<float> gVisibilityIn : register(t2, space9); // CSFilter の入力（CSMain が書いた生の可視率）
Texture2D<float4> gHistory : register(t3, space9);
RWTexture2D<float> gVisibility : register(u0, space9);
RWTexture2D<float4> gFiltered : register(u1, space9);
SamplerState gLinearClamp : register(s0);
SamplerState gMaterialWrap : register(s1);
SamplerState gPointClamp : register(s2);

// GeometryAux の面の記述にある着色の法線（法線地図・デカール込み）。不透明面の画素は必ず書かれている。
// 深度の差分の法線は細い物や輪郭で奥の面を拾って向きを誤り、影の中に光が漏れたので使わない。
float3 SurfaceNormal(uint2 pixel)
{
    float3 normal;
    float roughness;
    float metallic;
    HikariUnpackSceneSurface(gSceneSurface.Load(int3(pixel, 0)), normal, roughness, metallic);
    return normal;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint2 pixel = id.xy;
    if (any(pixel >= uint2(gScreenParams.xy)))
        return;
    const float2 uv = (float2(pixel) + 0.5f) * gScreenParams.zw;
    const float depth = gSceneDepth.SampleLevel(gPointClamp, uv, 0.0f);
    if (!HikariIsValidSceneDepth(depth))
    {
        gVisibility[pixel] = 1.0f;
        return;
    }
    float3 position;
    if (!HikariReconstructWorldPosition(uv, depth, gInvViewProj, position))
    {
        gVisibility[pixel] = 1.0f;
        return;
    }
    const float3 normal = SurfaceNormal(pixel);
    const float3 toSun = gSunCone.xyz;
    const float cosine = dot(normal, toSun);
    // 光に背く面は影（Forward の N・L も同じ法線で 0 になる）。
    if (cosine <= 0.0f || gRayLighting.scene.w != 0u)
    {
        gVisibility[pixel] = cosine <= 0.0f ? 0.0f : 1.0f;
        return;
    }
    // 光をかすめる面ほど自分の面に当たりやすいので、始点を法線方向へ多めにずらす。
    const float distanceToCamera = length(position - gCameraPosition.xyz);
    const float offset = gRayParams.w * (1.0f + distanceToCamera * 0.02f) * (2.0f - cosine);
    const float sinHalfAngle = sin(gSunCone.w);
    const uint sampleCount = max((uint)gRayParams.y, 1u);
    const float frame = gRayParams.z;
    float lit = 0.0f;
    [loop] for (uint i = 0u; i < sampleCount; ++i)
    {
        const float2 seed = float2(pixel) + float2(i, i * 3u) * 17.0f + frame * 5.588238f;
        const float2 xi = float2(frac(52.9829189f * frac(dot(seed, float2(0.06711056f, 0.00583715f)))),
            frac(52.9829189f * frac(dot(seed.yx + 11.0f, float2(0.00583715f, 0.06711056f)))));
        RayDesc ray;
        ray.Origin = position + normal * offset;
        ray.Direction = HikariSampleEmitterCone(toSun, sinHalfAngle * sinHalfAngle, xi);
        ray.TMin = 0.001f;
        ray.TMax = max(gRayParams.x, 0.1f);
        // 半透明（ガラスなど）は透過率の分だけ光を通す。
        float through = HikariTraceRuntimeShadowTransmittance(ray, gRayLighting.scene.x, gRayLighting.scene.y,
            gRayLighting.time, gMaterialWrap);
        // 影の距離より先は代理（地形）だけを見る。低い太陽の山の影は距離の設定に関わらず届く。
        if (through > 0.0f && gInputFlags.y > ray.TMax)
        {
            ray.TMin = ray.TMax;
            ray.TMax = gInputFlags.y;
            if (HikariTraceRuntimeProxyOccluded(ray))
                through = 0.0f;
        }
        lit += through;
    }
    gVisibility[pixel] = lit / float(sampleCount);
}

// 可視率の空間フィルター。画素ごと 2 本程度の二値標本は TAA だけでは半影に 2 画素周期の模様が残るので、
// 同じ面（中心の接平面からの距離が許容内）の 7x7 近傍を平均してから Forward へ渡す。
// 近傍の可視率と深度は群（8x8 と周り 3 画素）で一度だけ読んで共有する（画素ごとに 49 回読むと 4K で約 0.6 ms）。
// 同じ面の判定は近傍の位置を戻さず、中心の接平面を投影の逆行列で同次座標へ移して比べる（近傍ごとの行列積と除算が要らない）。
#define HIKARI_SHADOW_FILTER_RADIUS 3
#define HIKARI_SHADOW_FILTER_TILE (8 + 2 * HIKARI_SHADOW_FILTER_RADIUS)
groupshared float gFilterVisibility[HIKARI_SHADOW_FILTER_TILE * HIKARI_SHADOW_FILTER_TILE];
groupshared float gFilterDepth[HIKARI_SHADOW_FILTER_TILE * HIKARI_SHADOW_FILTER_TILE];

[numthreads(8, 8, 1)]
void CSFilter(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint threadIndex : SV_GroupIndex)
{
    const int2 last = int2(gScreenParams.xy) - 1;
    const int2 tileOrigin = int2(group.xy) * 8 - HIKARI_SHADOW_FILTER_RADIUS;
    for (uint entry = threadIndex; entry < HIKARI_SHADOW_FILTER_TILE * HIKARI_SHADOW_FILTER_TILE; entry += 64u)
    {
        const int2 tap = clamp(
            tileOrigin + int2(entry % HIKARI_SHADOW_FILTER_TILE, entry / HIKARI_SHADOW_FILTER_TILE), int2(0, 0), last);
        gFilterVisibility[entry] = gVisibilityIn[tap];
        gFilterDepth[entry] = gSceneDepth.Load(int3(tap, 0));
    }
    GroupMemoryBarrierWithGroupSync();

    const uint2 pixel = id.xy;
    if (any(pixel >= uint2(gScreenParams.xy)))
        return;
    const int2 local = int2(pixel) - int2(group.xy) * 8 + HIKARI_SHADOW_FILTER_RADIUS;
    const int center = local.y * HIKARI_SHADOW_FILTER_TILE + local.x;
    const float raw = gFilterVisibility[center];
    gFiltered[pixel] = float4(raw, 0, 0, 0);
    // 近傍が全部照らされている／全部影なら平均する必要がない（画面の大半はここで終わる）。
    float lowest = raw;
    float highest = raw;
    [unroll] for (int sy = -HIKARI_SHADOW_FILTER_RADIUS; sy <= HIKARI_SHADOW_FILTER_RADIUS; ++sy)
    {
        [unroll] for (int sx = -HIKARI_SHADOW_FILTER_RADIUS; sx <= HIKARI_SHADOW_FILTER_RADIUS; ++sx)
        {
            const float value = gFilterVisibility[center + sy * HIKARI_SHADOW_FILTER_TILE + sx];
            lowest = min(lowest, value);
            highest = max(highest, value);
        }
    }
    const float2 uv = (float2(pixel) + 0.5f) * gScreenParams.zw;
    const float depth = gSceneDepth.Load(int3(pixel, 0));
    float3 position;
    if (!HikariIsValidSceneDepth(depth) ||
        !HikariReconstructWorldPosition(uv, depth, gInvViewProj, position))
    {
        return;
    }
    const float3 normal = SurfaceNormal(pixel);
    const float distanceToCamera = length(position - gCameraPosition.xyz);
    if (distanceToCamera >= 65000.0f) return;
    const float2 encodedNormal = HikariEncodeOctahedralDirection(normal);
    if (highest - lowest < 0.002f)
    {
        gFiltered[pixel] = float4(raw, encodedNormal, distanceToCamera);
        return;
    }
    // 接平面からの許容距離：距離の 1.5% + 1cm（床をかすめて見る画素でも同じ面は通し、段差や輪郭は止める）。
    // 近傍の同次座標 h = (ndc, 深度, 1) の世界位置は (M h).xyz / (M h).w（M は投影の逆行列）なので、
    // dot(法線, 位置 - 中心) = dot(plane, M h) / (M h).w = dot(plane M, h) / dot(M[3], h)。
    const float tolerance = 0.015f * distanceToCamera + 0.01f;
    const float4 plane = mul(float4(normal, -dot(normal, position)), gInvViewProj);
    float sum = 0.0f;
    float weight = 0.0f;
    [unroll] for (int dy = -HIKARI_SHADOW_FILTER_RADIUS; dy <= HIKARI_SHADOW_FILTER_RADIUS; ++dy)
    {
        [unroll] for (int dx = -HIKARI_SHADOW_FILTER_RADIUS; dx <= HIKARI_SHADOW_FILTER_RADIUS; ++dx)
        {
            const int2 tap = clamp(int2(pixel) + int2(dx, dy), int2(0, 0), last);
            const float tapDepth = gFilterDepth[center + dy * HIKARI_SHADOW_FILTER_TILE + dx];
            if (!HikariIsValidSceneDepth(tapDepth))
                continue;
            const float2 ndc = ((float2(tap) + 0.5f) * gScreenParams.zw * 2.0f - 1.0f) * float2(1.0f, -1.0f);
            const float4 h = float4(ndc, tapDepth, 1.0f);
            if (abs(dot(plane, h)) > tolerance * abs(dot(gInvViewProj[3], h)))
                continue;
            sum += gFilterVisibility[center + dy * HIKARI_SHADOW_FILTER_TILE + dx];
            weight += 1.0f;
        }
    }
    float visibility = weight > 0.0f ? sum / weight : raw;
    float2 previousUv;
    float previousDepth, previousViewDepth;
    if (gInputFlags.x > 0.5f && HikariProjectWorldPosition(position, gPreviousViewProj,
        previousUv, previousDepth, previousViewDepth) && all(previousUv > 0) && all(previousUv < 1))
    {
        const float4 history = gHistory.SampleLevel(gPointClamp, previousUv, 0);
        const float expectedDistance = length(position - gPreviousCamera.xyz);
        if (history.w > 0 && abs(history.w - expectedDistance) < max(0.03f, expectedDistance * 0.01f) &&
            dot(HikariDecodeOctahedralDirection(history.yz), normal) > 0.95f)
        {
            // 移動する影は受光面の運動に現れない。可視率の変化も調べ、全照明/全遮蔽の領域は上で即時更新する。
            const float previous = clamp(history.x, lowest, highest);
            const float historyWeight = 0.85f * (1.0f - smoothstep(0.08f, 0.3f, abs(previous - visibility)));
            visibility = lerp(visibility, previous, historyWeight);
        }
    }
    gFiltered[pixel] = float4(visibility, encodedNormal, distanceToCamera);
}
