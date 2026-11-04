#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"
#include "Include/Post/HIKARI_PostHash.hlsli"

// 近場の GI（可視性ビットマスク、Therrien ほか 2023。GTAO のスライスを 32 の扇形に分けたもの）。画素を通る画面上の
// 直線（スライス）ごとに、両側の深度の標本を「前の面から厚さ分だけ奥まで」の物として扇形を塞ぎ、新しく塞いだ扇形の
// 測度（余弦重み、GTAO と同じ測度）を距離で弱めて「塞いだ量」に積み、同じ重みでその物の放射輝度（前フレームの不透明の
// 場面色を再投影）を近場の照り返しとして積む。可視性は 1 − 塞いだ量。Forward は探針の間接光に可視性を掛け、近場の
// 照り返しを足す。物の裏へ光が回り込むので、普通の AO のように物を無限に厚いとみなして暗くしすぎない。
// 出力は RGBA16F：rgb = 近場の照り返し E/pi（今フレームの前露出済み）、a = 可視性。時間蓄積は持たないので乱数は画素
// 位置だけで決め、後段の 5 tap の両側フィルタで均す。

cbuffer SsaoPassCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gScreenParams;
    float4 gAoParams0;
    float4 gAoParams1;
    float4 gBlurParams;
    float4 gDepthScreenParams;
    float4x4 gPreviousViewProj;
    float4 gNearFieldParams; // x 厚さ [m], y 1 = 前フレームの場面色あり, z 前露出の換算（P / P_prev）
};

#define gAoRadius gAoParams0.x
#define gAoBias gAoParams0.y
#define gAoStrength gAoParams0.z
#define gAoPower gAoParams0.w
#define gAoSliceCount gAoParams1.x
#define gAoStepCount gAoParams1.y

static const float kPi = 3.14159265f;
static const float kHalfPi = 1.57079633f;
static const uint kSectorCount = 32u;

Texture2D<float> gSceneDepthTex : register(t0);
Texture2D gNormalRoughnessTex : register(t1);
Texture2D<float4> gPreviousSceneColor : register(t2);
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

float3 DecodeNormal(float4 packed)
{
    return HikariDecodeOctahedralDirection(packed.rg);
}

float3 ReconstructWorld(float2 uv, float depth)
{
    float3 world;
    HikariReconstructWorldPosition(uv, depth, gInvViewProj, world);
    return world;
}

float2 ProjectWorld(float3 world)
{
    float2 uv;
    float projectedDepth;
    float viewDepth;
    HikariProjectWorldPosition(world, gViewProj, uv, projectedDepth, viewDepth);
    return uv;
}

// 世界半径を画面上の画素数へ。面の接線方向へ半径だけ動いた点を射影して測る。
float ScreenRadiusPixels(float2 centerUv, float3 worldPos, float3 normal)
{
    float3 up = abs(normal.y) < 0.98f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, normal));
    float2 radiusUv = ProjectWorld(worldPos + tangent * gAoRadius) - centerUv;
    float radius = length(radiusUv * gScreenParams.xy);
    return clamp(radius, 2.0f, 0.12f * gScreenParams.x);
}

// スライス内の角 θ（視線から測り、正は画面の正の向きの側）までの、|sin θ|·cos(θ − γ) の積分（θ = 0 で 0）。
// GTAO の内側の積分と同じ測度で、法線まわりの半球全体では cos γ + γ sin γ になる（射影した法線の長さを掛けて平均すると
// 余弦重みの可視性）。
float SliceMeasure(float theta, float sinGamma, float cosGamma, float gamma)
{
    const float h = 0.25f * (cosGamma - cos(2.0f * theta - gamma) + 2.0f * theta * sinGamma);
    return theta >= 0.0f ? h : -h;
}

// acos の多項式近似（Abramowitz & Stegun 4.4.45、誤差 7e-5 rad。扇形 1 つは 0.098 rad）。
float SectorAcos(float x)
{
    const float a = abs(x);
    const float r = sqrt(1.0f - a) * (1.5707288f + a * (-0.2121144f + a * (0.0742610f - 0.0187293f * a)));
    return x >= 0.0f ? r : kPi - r;
}

// [u0, u1)（半球を 0..1 にした位置）にかかる扇形の bit。
uint SectorBits(float u0, float u1)
{
    const uint first = min(uint(saturate(u0) * kSectorCount), kSectorCount - 1u);
    const uint last = min(uint(ceil(saturate(u1) * kSectorCount)), kSectorCount);
    const uint count = last > first ? last - first : 1u;
    return (count >= 32u ? 0xffffffffu : ((1u << count) - 1u)) << first;
}

// 前フレームの場面色（前露出を今フレームへ換算）。画面外は無い。
bool PreviousRadiance(float3 world, out float3 radiance)
{
    radiance = 0.0f.xxx;
    float2 uv;
    float projectedDepth;
    float viewDepth;
    if (!HikariProjectWorldPosition(world, gPreviousViewProj, uv, projectedDepth, viewDepth) || any(uv < 0.0f) ||
        any(uv > 1.0f))
        return false;
    const float3 color = gPreviousSceneColor.SampleLevel(gLinearClamp, uv, 0).rgb;
    radiance = all(isfinite(color)) ? max(color, 0.0f.xxx) * gNearFieldParams.z : 0.0f.xxx;
    return true;
}

float4 NearFieldGi(float2 uv, float depth, float3 n, bool sampleNormals)
{
    const float3 position = ReconstructWorld(uv, depth);
    const float3 view = normalize(gCameraPos.xyz - position);
    const float radiusPixels = ScreenRadiusPixels(uv, position, n);
    const float2 texel = gScreenParams.zw;
    const float2 pixel = floor(uv * gScreenParams.xy);
    const float noiseSlice = HikariInterleavedGradientNoise(pixel, 0u);
    const float noiseStep = HikariInterleavedGradientNoise((pixel + 5.0f).yx, 0u);
    const uint sliceCount = max((uint)gAoSliceCount, 1u);
    const uint stepCount = max((uint)gAoStepCount, 1u);
    const float thickness = max(gNearFieldParams.x, 0.01f);
    const bool bounce = gNearFieldParams.y > 0.5f;

    float visibility = 0.0f;
    float3 indirect = 0.0f.xxx;
    [loop] for (uint slice = 0u; slice < sliceCount; ++slice)
    {
        const float phi = (float(slice) + noiseSlice) * (kPi / float(sliceCount));
        const float2 direction = float2(cos(phi), sin(phi));
        // スライス面：視線と、画面上の方向に対応する世界方向が張る平面。
        const float3 sliceDir = ReconstructWorld(uv + direction * texel, depth) - position;
        const float3 axis = normalize(sliceDir - view * dot(sliceDir, view));
        const float3 sliceNormal = normalize(cross(axis, view));
        const float3 projected = n - sliceNormal * dot(n, sliceNormal);
        const float projectedLength = length(projected);
        if (projectedLength < 1.0e-4f)
            continue;
        const float3 projectedDir = projected / projectedLength;
        const float gamma = sign(dot(projectedDir, axis)) * acos(saturate(dot(projectedDir, view)));
        const float sinGamma = sin(gamma);
        const float cosGamma = cos(gamma);
        const float lower = gamma - kHalfPi;
        const float sliceMeasure =
            SliceMeasure(lower + kPi, sinGamma, cosGamma, gamma) - SliceMeasure(lower, sinGamma, cosGamma, gamma);
        uint occluded = 0u;
        float blocked = 0.0f;
        [unroll] for (uint side = 0u; side < 2u; ++side)
        {
            const float sideSign = side == 0u ? -1.0f : 1.0f;
            [loop] for (uint step = 0u; step < stepCount; ++step)
            {
                // 近くを密に（2 乗の間隔）。
                const float t = (float(step) + noiseStep) / float(stepCount);
                float2 sampleUv = uv + sideSign * direction * texel * radiusPixels * t * t + sideSign * direction * texel;
                if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f))
                    break;
                sampleUv = HikariSnapUvToTexelCenter(sampleUv, gDepthScreenParams);
                const float sampleDepth = gSceneDepthTex.SampleLevel(gPointClamp, sampleUv, 0);
                if (sampleDepth >= 0.99999f)
                    continue;
                const float3 samplePosition = ReconstructWorld(sampleUv, sampleDepth);
                const float3 delta = samplePosition - position;
                const float distance = length(delta);
                if (distance <= gAoBias || distance > gAoRadius)
                    continue;
                // 前の面と、厚さだけ奥（相機から離れる向き）の面がスライス内で見込む角。
                const float frontAngle = sideSign * SectorAcos(clamp(dot(delta / distance, view), -1.0f, 1.0f));
                const float3 back = delta - view * thickness;
                const float backAngle = sideSign * SectorAcos(clamp(dot(normalize(back), view), -1.0f, 1.0f));
                const float u0 = (min(frontAngle, backAngle) - lower) / kPi;
                const float u1 = (max(frontAngle, backAngle) - lower) / kPi;
                if (u1 <= 0.0f || u0 >= 1.0f)
                    continue;
                const uint bits = SectorBits(u0, u1);
                const uint fresh = bits & ~occluded;
                occluded |= bits;
                [branch] if (fresh != 0u)
                {
                    // 新しく塞いだ扇形の測度（区間の測度を bit の割合で配る）。遠い物ほど弱く塞ぐ：半径に近い物は探針の
                    // 間接光がすでに含んでいて、そこを均一な光として引くと、光が偏って届く所（上から照らされた壁の足元
                    // など）を二重に暗くする（GIReference の小屋で路径追踪と対照して決めた）。
                    const float a = clamp(min(frontAngle, backAngle), lower, lower + kPi);
                    const float b = clamp(max(frontAngle, backAngle), lower, lower + kPi);
                    const float span = SliceMeasure(b, sinGamma, cosGamma, gamma) -
                        SliceMeasure(a, sinGamma, cosGamma, gamma);
                    const float share = float(countbits(fresh)) / float(max(countbits(bits), 1u));
                    const float weight = max(span, 0.0f) * share * saturate(1.0f - distance / gAoRadius);
                    blocked += weight;
                    [branch] if (bounce)
                    {
                        // 照り返しは物の表がこちらを向く時だけ。
                        float facing = 1.0f;
                        if (sampleNormals)
                        {
                            const float3 emitterNormal =
                                DecodeNormal(gNormalRoughnessTex.SampleLevel(gPointClamp, sampleUv, 0));
                            facing = saturate(dot(emitterNormal, -delta / distance) * 4.0f);
                        }
                        float3 radiance;
                        if (facing > 0.0f && PreviousRadiance(samplePosition, radiance))
                            indirect += radiance * (projectedLength * weight * facing);
                    }
                }
            }
        }
        visibility += projectedLength * max(sliceMeasure - blocked, 0.0f);
    }
    visibility /= float(sliceCount);
    indirect /= float(sliceCount);
    const float shaped = pow(saturate(1.0f - saturate((1.0f - saturate(visibility)) * gAoStrength)), gAoPower);
    return float4(indirect, shaped);
}

float4 PSMain(VSOut input) : SV_TARGET
{
    const float2 uv = HikariSnapUvToTexelCenter(input.uv, gDepthScreenParams);
    const float depth = gSceneDepthTex.SampleLevel(gPointClamp, uv, 0);
    if (depth >= 0.99999f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    return NearFieldGi(uv, depth, DecodeNormal(gNormalRoughnessTex.SampleLevel(gPointClamp, uv, 0)), true);
}

float4 PSMainDepthOnly(VSOut input) : SV_TARGET
{
    const float2 uv = HikariSnapUvToTexelCenter(input.uv, gDepthScreenParams);
    const float depth = gSceneDepthTex.SampleLevel(gPointClamp, uv, 0);
    if (depth >= 0.99999f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    return NearFieldGi(uv, depth, HikariNormalFromSceneDepth(gSceneDepthTex, gPointClamp, uv,
        ReconstructWorld(uv, depth), gDepthScreenParams.zw, gInvViewProj, gCameraPos.xyz), false);
}
