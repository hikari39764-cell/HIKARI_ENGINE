#include "Include/Temporal/HIKARI_TemporalShaderInterface.hlsli"

// 後処理の線（漫画の輪郭・折れ目など）が読む深度を、出力の解像度で時間方向に均して作る。
// ずらした（jitter）描画の深度をそのまま読むと、輪郭の位置が毎フレーム標本の位置と一緒に跳ね、線が震える。
// 各出力画素の中心に当たる景色の標本を取り、運動ベクトルで引き寄せた前の結果と混ぜる。前の結果は今の近傍の
// 深度の範囲へ切り詰め、現れたばかりの面や動いた縁に古い深度が残らないようにする。
// 値は元の深度と同じ（0 = 近い面、1 = 遠い面と空）。

cbuffer TemporalStableDepthCB : register(b0)
{
    float4 gRenderSize;    // xy: 描画の大きさ、zw: その逆数
    float4 gOutputSize;    // xy: 出力の大きさ、zw: その逆数
    float4 gStableParams;  // xy: 今のずらし（描画の画素）、z: 前の結果が使えるか、w: 前の結果の重み
};

Texture2D<float> gStableSceneDepth : register(t0);  // 描画の解像度、ずらした深度
Texture2D<float2> gStableMotion : register(t1);     // 描画の解像度、今から前への動き（描画の画素）
Texture2D<float> gStableHistory : register(t2);     // 出力の解像度、前のフレームの結果

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut output;
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = pos;
    output.position =
        float4(pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

int2 ClampRenderPixel(int2 pixel)
{
    return clamp(pixel, int2(0, 0), max(int2(gRenderSize.xy), int2(1, 1)) - 1);
}

int2 ClampOutputPixel(int2 pixel)
{
    return clamp(pixel, int2(0, 0), max(int2(gOutputSize.xy), int2(1, 1)) - 1);
}

float CatmullRomWeight(float value)
{
    float x = abs(value);
    if (x <= 1.0f)
    {
        return ((1.5f * x - 2.5f) * x) * x + 1.0f;
    }
    if (x < 2.0f)
    {
        return (((-0.5f * x + 2.5f) * x - 4.0f) * x) + 2.0f;
    }
    return 0.0f;
}

// 前の結果は Catmull-Rom で引く（双一次で毎フレーム引き直すと、動くたびに深度の段差がなまって線が太る）。
// 行き過ぎは呼び出し側の切り詰めで抑える。
float SampleHistory(float2 uv)
{
    float2 texel = uv * gOutputSize.xy - 0.5f;
    int2 basePixel = int2(floor(texel));
    float2 fraction = texel - float2(basePixel);
    float sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int y = -1; y <= 2; ++y)
    {
        float weightY = CatmullRomWeight(float(y) - fraction.y);
        [unroll]
        for (int x = -1; x <= 2; ++x)
        {
            float weight = CatmullRomWeight(float(x) - fraction.x) * weightY;
            sum += gStableHistory.Load(int3(ClampOutputPixel(basePixel + int2(x, y)), 0)) * weight;
            weightSum += weight;
        }
    }
    return sum / max(weightSum, 1e-5f);
}

float PSMain(VSOut input) : SV_TARGET0
{
    // この出力画素の中心の景色は、ずらした描画では jitter だけ動いた所に写っている。
    float2 renderPosition = input.uv * gRenderSize.xy + gStableParams.xy;
    int2 nearestPixel = ClampRenderPixel(int2(floor(renderPosition)));
    float current = gStableSceneDepth.Load(int3(nearestPixel, 0));

    float lower = current;
    float upper = current;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float neighbor = gStableSceneDepth.Load(int3(ClampRenderPixel(nearestPixel + int2(x, y)), 0));
            lower = min(lower, neighbor);
            upper = max(upper, neighbor);
        }
    }

    if (gStableParams.z < 0.5f || !isfinite(current))
    {
        return saturate(current);
    }

    float2 motion = gStableMotion.Load(int3(nearestPixel, 0));
    float2 previousUv = input.uv + motion * gRenderSize.zw;
    if (any(previousUv < 0.0f) || any(previousUv > 1.0f) || any(!isfinite(previousUv)))
    {
        return current;
    }
    float history = clamp(SampleHistory(previousUv), lower, upper);
    return lerp(current, history, saturate(gStableParams.w));
}
