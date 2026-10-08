#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"

// 画面空間の分離型次表面散乱（Jimenez らの separable SSS の簡略）。Forward が書いた「拡散光 × 強さ（rgb）, 散乱半径 [m]（a）」
// を深度に沿って水平→垂直にガウスぼかしし、ぼかした分と元の差だけを場面色へ加算する。鏡面と自発光は触らない。
// 画素半径は半径 [m] を隣の画素との世界距離で割って求めるので、投影行列を別に持たない。
// チャネルごとに半径の比を変え（赤が最も遠くへ散る）、皮膚のような赤い縁のにじみを出す。

cbuffer SubsurfacePassCB : register(b0)
{
    float4x4 gInvViewProj;
    float4 gScreenParams;   // x: 幅, y: 高さ, z: 1/幅, w: 1/高さ
    float4 gBlurParams;     // xy: ぼかす方向 (1,0) / (0,1), z: 強さ, w: 画素半径の上限
    float4 gChannelFalloff; // rgb: チャネルごとの半径の比, w: 未使用
};

Texture2D gSourceTex : register(t0);     // 水平: Forward の拡散光 + 半径。垂直: 水平の結果 + 半径
Texture2D gSceneDepthTex : register(t1);
Texture2D gOriginalTex : register(t2);   // 垂直のみ: Forward の元の拡散光（差分を取る）
SamplerState gPointClamp : register(s0);
SamplerState gLinearClamp : register(s1);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut o;
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2);
    o.uv = pos;
    o.pos = float4(pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

// 片側の tap 数。t = i / kTapCount を半径に掛けた位置を読む。
static const uint kTapCount = 7u;
// 半径を 3σ とするガウス（t = 0, 1/7, ..., 1）。
static const float kSigma = 1.0f / 3.0f;

bool ReconstructView(float2 uv, out float3 worldPos, out float viewDepth)
{
    const float depth = gSceneDepthTex.SampleLevel(gPointClamp, uv, 0.0f).r;
    worldPos = 0.0f.xxx;
    viewDepth = 0.0f;
    if (depth >= 0.99999f)
        return false;
    return HikariReconstructWorldPosition(uv, depth, gInvViewProj, worldPos) &&
        HikariReconstructViewDepth(uv, depth, gInvViewProj, viewDepth);
}

float4 BlurAlong(float2 uv, float2 direction)
{
    const float4 center = gSourceTex.SampleLevel(gPointClamp, uv, 0.0f);
    const float radius = center.a;
    if (radius <= 0.0f)
        return center;
    float3 worldPos;
    float viewDepth;
    if (!ReconstructView(uv, worldPos, viewDepth))
        return center;
    // 隣の画素との世界距離から画素半径を決める（深さに比例して縮む）。
    float3 neighborPos;
    float neighborDepth;
    const float2 texel = gScreenParams.zw;
    const float2 neighborUv = uv + direction * texel;
    if (!ReconstructView(neighborUv, neighborPos, neighborDepth))
        return center;
    const float worldPerPixel = max(length(neighborPos - worldPos), 1.0e-5f);
    const float pixelRadius = min(radius / worldPerPixel, gBlurParams.w);
    if (pixelRadius < 0.5f)
        return center;

    float3 sum = center.rgb;
    float3 weightSum = 1.0f.xxx;
    const float3 sigma = max(gChannelFalloff.rgb, 0.05f) * kSigma;
    [unroll] for (uint i = 1u; i <= kTapCount; ++i)
    {
        const float t = float(i) / float(kTapCount);
        const float3 gauss = exp(-0.5f * (t / sigma) * (t / sigma));
        const float2 offset = direction * (t * pixelRadius) * texel;
        [unroll] for (int side = -1; side <= 1; side += 2)
        {
            const float2 tapUv = uv + offset * float(side);
            if (any(tapUv < 0.0f) || any(tapUv > 1.0f))
                continue;
            const float4 tap = gSourceTex.SampleLevel(gLinearClamp, tapUv, 0.0f);
            if (tap.a <= 0.0f)
                continue;
            float3 tapPos;
            float tapDepth;
            if (!ReconstructView(tapUv, tapPos, tapDepth))
                continue;
            // 半径より奥や手前の面は別の物。深さの差で重みを落とす。
            const float depthWeight = saturate(1.0f - abs(tapDepth - viewDepth) / max(radius, 1.0e-4f));
            const float3 weight = gauss * depthWeight;
            sum += tap.rgb * weight;
            weightSum += weight;
        }
    }
    return float4(sum / weightSum, radius);
}

float4 PSBlurHorizontal(VSOut input) : SV_TARGET
{
    return BlurAlong(input.uv, float2(1.0f, 0.0f));
}

// 垂直ぼかしの結果と元の拡散光の差を加算合成（blend ONE/ONE）する。
float4 PSBlurVerticalComposite(VSOut input) : SV_TARGET
{
    const float4 blurred = BlurAlong(input.uv, float2(0.0f, 1.0f));
    if (blurred.a <= 0.0f)
        return 0.0f.xxxx;
    const float3 original = gOriginalTex.SampleLevel(gPointClamp, input.uv, 0.0f).rgb;
    return float4((blurred.rgb - original) * gBlurParams.z, 0.0f);
}
