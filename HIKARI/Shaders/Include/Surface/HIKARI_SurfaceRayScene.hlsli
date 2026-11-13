#ifndef HIKARI_SURFACE_RAY_SCENE_HLSLI
#define HIKARI_SURFACE_RAY_SCENE_HLSLI

// モジュールの面（HIKARI/Project/RenderSurface.h）の shader から、場面の加速構造へ影の光線を飛ばす。
// Forward の通道（SurfaceMS・SurfaceVS・SurfaceForwardPS）でだけ使える。先に HIKARI_ForwardCommon.hlsli を読み、
// 飛ばす前に HikariSurfaceRaySceneValid() を確かめる（光線追跡を使わない設定や、加速構造が出来る前のフレームは 0）。
//
// 草の葉のように、画面の画素ではなく物そのものの位置で影を決めたい時に使う。画面の光線追跡の影（gRayTracedShadow）は
// 不透明な面の画素の値なので、その手前に描く薄い物が読むと、後ろの面の影を拾う。

RaytracingAccelerationStructure gSurfaceRayScene : register(t0, space11);

bool HikariSurfaceRaySceneValid()
{
    return (gScreenSpaceTraceFlags & HIKARI_SCREEN_SPACE_SURFACE_RAY_SCENE_VALID) != 0u;
}

// 起点から向きへ、minDistance から maxDistance までの間に遮る物があるか。
// 見る物：モジュールの代理（地形など。表だけが遮る）、影を落とす不透明な模型、大量配置。材質は読まないので、
// 切り抜きの材質は板ごと遮り、半透明の模型と影を落とさない模型は遮らない。
bool HikariSurfaceRayOccluded(float3 origin, float3 direction, float minDistance, float maxDistance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = minDistance;
    ray.TMax = maxDistance;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_BACK_FACING_TRIANGLES> query;
    // 0x83 = 代理（0x01）＋ 大量配置（0x02）＋ 影を落とす不透明な模型（0x80。kRuntimeRayTracingSolidCasterMask）。
    query.TraceRayInline(gSurfaceRayScene, RAY_FLAG_NONE, 0x83u, ray);
    query.Proceed();
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

#endif
