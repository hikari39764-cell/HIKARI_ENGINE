#ifndef HIKARI_CLUSTER_MASS_INSTANCE_VISIBILITY_INCLUDED
#define HIKARI_CLUSTER_MASS_INSTANCE_VISIBILITY_INCLUDED

// HIKARI_ClusterCullCommon.hlsli の投影の関数を使う。

// 実例集の個体を描かない時。画面の高さに対して小さすぎる時と、遠景模型がある種類で今の距離の担当でない時
// （近い模型と遠い模型は同じ閾値で入れ替わるので、隙間も重なりも無い）。
bool HikariClusterCullInstanceHidden(uint absoluteIndex, HikariSurfaceGpuSceneInstance instance)
{
    if (!HikariIsMassGpuSceneIndex(absoluteIndex) ||
        (instance.objectIdHigh & HIKARI_MASS_INSTANCE_IDENTITY_BIT) == 0u)
    {
        return false;
    }
    const float minimum = (float)((instance.objectIdHigh >> 16u) & 255u) * HIKARI_MASS_INSTANCE_MIN_SCREEN_STEP;
    // 個体ごとに閾値をずらす。全部が同じ距離で消えると輪が見えるので、遠くへ行くほど少しずつ間引かれるようにする。
    const uint hash = instance.objectIdLow * 2654435761u;
    const float spread = 0.6f + 0.8f * (float)(hash >> 16u) / 65535.0f;
    if (minimum > 0.0f && HikariClusterCullProjectedScreenRadius(instance.boundsCenterRadius) < minimum * spread)
    {
        return true;
    }
    // 正は近い模型、負は遠い模型。部品ごとに包む球が違うので、個体の原点と拡縮で測る。
    const float screenSwitch = asfloat(gMassRenderProxyBuffer[HikariDecodeMassGpuSceneIndex(absoluteIndex)].reserved);
    if (screenSwitch == 0.0f)
    {
        return false;
    }
    const float3 origin = float3(instance.world[0].w, instance.world[1].w, instance.world[2].w);
    const float3 up = float3(instance.world[0].y, instance.world[1].y, instance.world[2].y);
    const bool beyond = HikariClusterCullProjectedWorldLength(origin, length(up)) < abs(screenSwitch) * spread;
    return screenSwitch > 0.0f ? beyond : !beyond;
}

#endif
