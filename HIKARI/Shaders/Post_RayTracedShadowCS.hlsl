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
    float4 gInputFlags;     // x 面の記述（法線）が束ねてあれば 1
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

// 隣の画素の位置との差と、その長さの二乗（深度が無ければ大きな値）。
float4 NeighborDelta(float2 uv, float2 offset, float3 position)
{
    float3 neighbor;
    const float depth = gSceneDepth.SampleLevel(gPointClamp, uv + offset, 0.0f);
    if (!HikariIsValidSceneDepth(depth) || !HikariReconstructWorldPosition(uv + offset, depth, gInvViewProj, neighbor))
        return float4(0.0f, 0.0f, 0.0f, 1.0e30f);
    const float3 delta = neighbor - position;
    return float4(delta, dot(delta, delta));
}

// 深度の差分から求める実際の面（多角形）の法線。輪郭で奥の物を拾わないよう、左右・上下それぞれ近い方の隣を使う。
float3 NormalFromDepth(float2 uv, float3 position)
{
    const float2 texel = gScreenParams.zw;
    const float4 right = NeighborDelta(uv, float2(texel.x, 0.0f), position);
    const float4 left = NeighborDelta(uv, float2(-texel.x, 0.0f), position);
    const float4 down = NeighborDelta(uv, float2(0.0f, texel.y), position);
    const float4 up = NeighborDelta(uv, float2(0.0f, -texel.y), position);
    const float3 dx = right.w <= left.w ? right.xyz : -left.xyz;
    const float3 dy = down.w <= up.w ? down.xyz : -up.xyz;
    const float3 normal = cross(dy, dx);
    const float lengthSquared = dot(normal, normal);
    if (lengthSquared < 1.0e-12f)
        return normalize(gCameraPosition.xyz - position);
    return normal * rsqrt(lengthSquared);
}

// 面の記述が束ねてあればその法線（頂点法線を補間した滑らかな法線）。無ければ 0。
float3 SmoothSurfaceNormal(float2 uv)
{
    float3 normal = 0.0f.xxx;
    if (gInputFlags.x > 0.5f)
    {
        float roughness;
        float metallic;
        HikariUnpackSceneSurface(gSceneSurface.SampleLevel(gPointClamp, uv, 0.0f), normal, roughness, metallic);
    }
    return dot(normal, normal) < 0.5f ? 0.0f.xxx : normal;
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
    const float3 smoothNormal = SmoothSurfaceNormal(uv);
    const bool hasSmoothNormal = dot(smoothNormal, smoothNormal) > 0.5f;
    const float3 normal = hasSmoothNormal ? smoothNormal : NormalFromDepth(uv, position);
    const float3 toSun = gSunCone.xyz;
    const float cosine = dot(normal, toSun);
    // 光に背く面は影。ただし深度の差分の法線は多角形の面ごとに平らなので、それで切ると球の明暗の境が面の形の
    // 階段になる。その時は自分の形による遮りを数えず、Forward の N・L（滑らかな法線）に任せる。
    if (cosine <= 0.0f || gRayLighting.scene.w != 0u)
    {
        gVisibility[pixel] = cosine <= 0.0f && hasSmoothNormal ? 0.0f : 1.0f;
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
        lit += HikariTraceRuntimeShadowTransmittance(ray, gRayLighting.scene.x, gRayLighting.scene.y,
            gRayLighting.time, gMaterialWrap);
    }
    gVisibility[pixel] = lit / float(sampleCount);
}

// 可視率の空間フィルター。画素ごと 2 本程度の二値標本は TAA だけでは半影に 2 画素周期の模様が残るので、
// 同じ面（中心の接平面からの距離が許容内）の 7x7 近傍を平均してから Forward へ渡す。
[numthreads(8, 8, 1)]
void CSFilter(uint3 id : SV_DispatchThreadID)
{
    const uint2 pixel = id.xy;
    if (any(pixel >= uint2(gScreenParams.xy)))
        return;
    const float raw = gVisibilityIn[pixel];
    gFiltered[pixel] = float4(raw, 0, 0, 0);
    const int2 last = int2(gScreenParams.xy) - 1;
    // 近傍が全部照らされている／全部影なら平均する必要がない（画面の大半はここで終わる）。
    float lowest = raw;
    float highest = raw;
    [unroll] for (int sy = -3; sy <= 3; ++sy)
    {
        [unroll] for (int sx = -3; sx <= 3; ++sx)
        {
            const float value = gVisibilityIn[clamp(int2(pixel) + int2(sx, sy), int2(0, 0), last)];
            lowest = min(lowest, value);
            highest = max(highest, value);
        }
    }
    const float2 uv = (float2(pixel) + 0.5f) * gScreenParams.zw;
    const float depth = gSceneDepth.SampleLevel(gPointClamp, uv, 0.0f);
    float3 position;
    if (!HikariIsValidSceneDepth(depth) ||
        !HikariReconstructWorldPosition(uv, depth, gInvViewProj, position))
    {
        return;
    }
    const float3 smoothNormal = SmoothSurfaceNormal(uv);
    const float3 normal = dot(smoothNormal, smoothNormal) > 0.5f ? smoothNormal : NormalFromDepth(uv, position);
    const float distanceToCamera = length(position - gCameraPosition.xyz);
    if (distanceToCamera >= 65000.0f) return;
    const float2 encodedNormal = HikariEncodeOctahedralDirection(normal);
    if (highest - lowest < 0.002f)
    {
        gFiltered[pixel] = float4(raw, encodedNormal, distanceToCamera);
        return;
    }
    // 接平面からの許容距離：距離の 1.5% + 1cm（床をかすめて見る画素でも同じ面は通し、段差や輪郭は止める）。
    const float tolerance = 0.015f * length(position - gCameraPosition.xyz) + 0.01f;
    float sum = 0.0f;
    float weight = 0.0f;
    [unroll] for (int dy = -3; dy <= 3; ++dy)
    {
        [unroll] for (int dx = -3; dx <= 3; ++dx)
        {
            const int2 tap = clamp(int2(pixel) + int2(dx, dy), int2(0, 0), last);
            const float2 tapUv = (float2(tap) + 0.5f) * gScreenParams.zw;
            const float tapDepth = gSceneDepth.Load(int3(tap, 0));
            float3 tapPosition;
            if (!HikariIsValidSceneDepth(tapDepth) ||
                !HikariReconstructWorldPosition(tapUv, tapDepth, gInvViewProj, tapPosition) ||
                abs(dot(normal, tapPosition - position)) > tolerance)
                continue;
            sum += gVisibilityIn[tap];
            weight += 1.0f;
        }
    }
    float visibility = weight > 0.0f ? sum / weight : raw;
    float2 previousUv;
    float previousDepth, previousViewDepth;
    if (gInputFlags.y > 0.5f && HikariProjectWorldPosition(position, gPreviousViewProj,
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
