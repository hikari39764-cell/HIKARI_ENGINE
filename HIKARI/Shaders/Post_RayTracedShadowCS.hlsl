#include "Include/Lighting/HIKARI_RuntimeRayLighting.hlsli"
#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"

// 光線追跡の太陽の影。場面の深度から画素の位置を戻し、太陽の円錐の中へ数本の影光線を飛ばして可視率（1 = 照らされる）を
// 書く。Forward はこの面の可視率でカスケード影を置き換える（半透明や地形は従来どおりカスケードを読む）。
// 円錐の半角は太陽の角半径 × 半影の倍率（カスケードの PCSS と同じ設定）で、光線は画素とフレームで揺らして TAA が均す。

cbuffer RayTracedShadowCB : register(b0)
{
    float4x4 gInvViewProj;
    float4 gCameraPosition;
    float4 gScreenParams;   // xy 大きさ, zw 1/大きさ
    float4 gSunCone;        // xyz 太陽へ向かう単位ベクトル, w 円錐の半角 [rad]
    float4 gRayParams;      // x 最大距離 [m], y 標本数, z フレーム番号, w 法線方向の始点のずらし [m]
    float4 gInputFlags;     // x 面の記述（法線）が束ねてあれば 1
    HikariRayLightingData gRayLighting;
};

Texture2D<float> gSceneDepth : register(t0, space9);
Texture2D<float4> gSceneSurface : register(t1, space9);
Texture2D<float> gVisibilityIn : register(t2, space9); // CSFilter の入力（CSMain が書いた生の可視率）
RWTexture2D<float> gVisibility : register(u0, space9);
SamplerState gLinearClamp : register(s0);
SamplerState gMaterialWrap : register(s1);
SamplerState gPointClamp : register(s2);

// 面の記述が無い画素は深度の差分で法線を作る（半解像度の AO と同じ近似）。
float3 NormalFromDepth(float2 uv, float3 position)
{
    const float2 texel = gScreenParams.zw;
    float3 right = position, down = position;
    const float depthRight = gSceneDepth.SampleLevel(gPointClamp, uv + float2(texel.x, 0.0f), 0.0f);
    const float depthDown = gSceneDepth.SampleLevel(gPointClamp, uv + float2(0.0f, texel.y), 0.0f);
    if (HikariIsValidSceneDepth(depthRight))
        HikariReconstructWorldPosition(uv + float2(texel.x, 0.0f), depthRight, gInvViewProj, right);
    if (HikariIsValidSceneDepth(depthDown))
        HikariReconstructWorldPosition(uv + float2(0.0f, texel.y), depthDown, gInvViewProj, down);
    const float3 normal = cross(down - position, right - position);
    const float lengthSquared = dot(normal, normal);
    if (lengthSquared < 1.0e-12f)
        return normalize(gCameraPosition.xyz - position);
    return normal * rsqrt(lengthSquared);
}

// 面の記述が束ねてあればその法線、無ければ深度の差分。
float3 SurfaceNormal(float2 uv, float3 position)
{
    float3 normal = 0.0f.xxx;
    if (gInputFlags.x > 0.5f)
    {
        float roughness;
        float metallic;
        HikariUnpackSceneSurface(gSceneSurface.SampleLevel(gPointClamp, uv, 0.0f), normal, roughness, metallic);
    }
    if (dot(normal, normal) < 0.5f)
        normal = NormalFromDepth(uv, position);
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
    const float3 normal = SurfaceNormal(uv, position);
    const float3 toSun = gSunCone.xyz;
    const float cosine = dot(normal, toSun);
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
        HikariRaySurface ignored;
        if (!HikariTraceRuntimeScene(ray, gRayLighting.scene.x, gRayLighting.scene.y, gRayLighting.time, gMaterialWrap,
                HIKARI_RAY_SHADOW, ignored))
            lit += 1.0f;
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
    if (highest - lowest < 0.002f || !HikariIsValidSceneDepth(depth) ||
        !HikariReconstructWorldPosition(uv, depth, gInvViewProj, position))
    {
        gVisibility[pixel] = raw;
        return;
    }
    const float3 normal = SurfaceNormal(uv, position);
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
    gVisibility[pixel] = weight > 0.0f ? sum / weight : raw;
}
