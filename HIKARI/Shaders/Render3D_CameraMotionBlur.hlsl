// カメラの運動ブラー。TAA の後の出力解像度のシーン線形 HDR に掛ける。
// 速度は描画解像度の運動ベクトル（現在 -> 前フレーム、描画画素）を出力画素へ直し、シャッターが開いている割合を
// 掛けたもの。タイルごとの最大速度（TileMaxCS）と近傍の最大（NeighborMaxCS）を求め、その方向に沿って集める
// （McGuire et al. 2012 の再構成フィルター: 手前のブラーは奥へ広がり、奥のブラーは手前の物体に遮られる）。

cbuffer CameraMotionBlurCB : register(b0)
{
    float4 gSizes;       // xy = 出力の大きさ、zw = 描画の大きさ
    float4 gBlur;        // x = シャッターの割合、y = ブラーの最大長 [出力画素]、z = 標本数、w = 正射影なら 1
    float4 gDepthTiles;  // x = near、y = far、zw = タイル数
};

Texture2D<float4> gSceneColor : register(t0);
Texture2D<float2> gMotion : register(t1);
Texture2D<float> gSceneDepth : register(t2);
Texture2D<float2> gTileMax : register(t3);
Texture2D<float2> gNeighborMax : register(t4);
RWTexture2D<float2> gTileMaxOut : register(u0);
RWTexture2D<float2> gNeighborMaxOut : register(u1);
RWTexture2D<float4> gOutput : register(u2);
SamplerState gLinearClamp : register(s0);

#define HIKARI_MOTION_BLUR_TILE 32

int2 RenderPixel(float2 uv)
{
    return clamp(int2(uv * gSizes.zw), int2(0, 0), int2(gSizes.zw) - 1);
}

// 出力画素でのブラーの速度（物体が前フレームから動いた向き）。長さは最大長で切る。
float2 BlurVelocity(float2 uv)
{
    const float2 motion = gMotion.Load(int3(RenderPixel(uv), 0));
    float2 velocity = -motion * (gSizes.xy / gSizes.zw) * gBlur.x;
    const float speed = length(velocity);
    if (speed > gBlur.y)
    {
        velocity *= gBlur.y / speed;
    }
    return velocity;
}

float LinearDepth(float2 uv)
{
    const float depth = gSceneDepth.Load(int3(RenderPixel(uv), 0));
    const float nearZ = gDepthTiles.x;
    const float farZ = gDepthTiles.y;
    if (gBlur.w > 0.5f)
    {
        return lerp(nearZ, farZ, depth);
    }
    return nearZ * farZ / max(farZ - depth * (farZ - nearZ), 1.0e-6f);
}

float2 LongerOf(float2 a, float2 b)
{
    return dot(a, a) >= dot(b, b) ? a : b;
}

groupshared float2 sTileVelocity[HIKARI_MOTION_BLUR_TILE * HIKARI_MOTION_BLUR_TILE];

[numthreads(HIKARI_MOTION_BLUR_TILE, HIKARI_MOTION_BLUR_TILE, 1)]
void TileMaxCS(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID, uint index : SV_GroupIndex)
{
    const uint2 pixel = groupId.xy * HIKARI_MOTION_BLUR_TILE + threadId.xy;
    float2 velocity = 0.0f.xx;
    if (all(pixel < uint2(gSizes.xy)))
    {
        velocity = BlurVelocity((float2(pixel) + 0.5f) / gSizes.xy);
    }
    sTileVelocity[index] = velocity;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint stride = HIKARI_MOTION_BLUR_TILE * HIKARI_MOTION_BLUR_TILE / 2u; stride > 0u; stride >>= 1u)
    {
        if (index < stride)
        {
            sTileVelocity[index] = LongerOf(sTileVelocity[index], sTileVelocity[index + stride]);
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (index == 0u)
    {
        gTileMaxOut[groupId.xy] = sTileVelocity[0];
    }
}

[numthreads(8, 8, 1)]
void NeighborMaxCS(uint3 id : SV_DispatchThreadID)
{
    const int2 tile = int2(id.xy);
    const int2 count = int2(gDepthTiles.zw);
    if (any(tile >= count))
    {
        return;
    }
    float2 velocity = 0.0f.xx;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int2 neighbor = clamp(tile + int2(x, y), int2(0, 0), count - 1);
            velocity = LongerOf(velocity, gTileMax.Load(int3(neighbor, 0)));
        }
    }
    gNeighborMaxOut[tile] = velocity;
}

// 距離 distance がブラーの長さ length の内側にある度合い（円錐と円柱）。
float Cone(float distance, float length)
{
    return saturate(1.0f - distance / length);
}

float Cylinder(float distance, float length)
{
    return 1.0f - smoothstep(0.95f * length, 1.05f * length, distance);
}

[numthreads(8, 8, 1)]
void GatherCS(uint3 id : SV_DispatchThreadID)
{
    const uint2 pixel = id.xy;
    if (any(pixel >= uint2(gSizes.xy)))
    {
        return;
    }
    const float4 center = gSceneColor.Load(int3(pixel, 0));
    const float2 neighborVelocity = gNeighborMax.Load(int3(pixel / HIKARI_MOTION_BLUR_TILE, 0));
    const float neighborSpeed = length(neighborVelocity);
    if (neighborSpeed < 0.5f)
    {
        gOutput[pixel] = center;
        return;
    }
    const float2 uv = (float2(pixel) + 0.5f) / gSizes.xy;
    const float centerSpeed = max(length(BlurVelocity(uv)), 0.5f);
    const float centerDepth = LinearDepth(uv);
    const float softDepth = max(0.05f, centerDepth * 0.02f);
    // 画素ごとに揺らした標本位置（縞を細かい雑音に変える）。TAA の後なのでフレームごとには変えず、ちらつかせない。
    const float jitter = frac(52.9829189f * frac(dot(float2(pixel), float2(0.06711056f, 0.00583715f)))) - 0.5f;
    const uint sampleCount = (uint)gBlur.z;
    float weight = 1.0f / centerSpeed;
    float3 color = center.rgb * weight;
    for (uint i = 0u; i < sampleCount; ++i)
    {
        const float t = clamp(((float)i + 0.5f + jitter) / (float)sampleCount * 2.0f - 1.0f, -1.0f, 1.0f);
        const float2 offset = neighborVelocity * (0.5f * t);
        const float distance = length(offset);
        const float2 sampleUv = uv + offset / gSizes.xy;
        const float sampleDepth = LinearDepth(sampleUv);
        const float sampleSpeed = max(length(BlurVelocity(sampleUv)), 0.5f);
        const float front = saturate(1.0f - (sampleDepth - centerDepth) / softDepth);
        const float back = saturate(1.0f - (centerDepth - sampleDepth) / softDepth);
        // 手前でぼけた標本が画素へ広がる / 奥の標本が画素自身のブラーの背景になる / 両方ぼけて重なる。
        const float coverage = front * Cone(distance, sampleSpeed) + back * Cone(distance, centerSpeed) +
            Cylinder(distance, sampleSpeed) * Cylinder(distance, centerSpeed) * 2.0f;
        color += gSceneColor.SampleLevel(gLinearClamp, sampleUv, 0.0f).rgb * coverage;
        weight += coverage;
    }
    gOutput[pixel] = float4(color / weight, center.a);
}
