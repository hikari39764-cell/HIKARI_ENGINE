#include "Include/Post/HIKARI_PostEffectInterface.hlsli"

Texture2D gSceneTex : register(t0);
// 色調整の 3D LUT（表示の符号化した色で引く）。一辺の格子数は gUser[2].w、0 なら束ねていない。
Texture3D gGradingLut : register(t1);
// 場景の不透明な面の深度（描画解像度）。画面の保護を、画面の前にある物の上へ透けさせないのに使う。無い時は 0。
Texture2D<float> gReadabilityDepth : register(t2);

float ReadReadabilityDepth(float2 uv)
{
    uint width, height;
    gReadabilityDepth.GetDimensions(width, height);
    if (width == 0u || height == 0u)
        return 0.0f;
    const uint2 pixel = min(uint2(saturate(uv) * float2(width, height)), uint2(width - 1u, height - 1u));
    return gReadabilityDepth.Load(int3(pixel, 0));
}
SamplerState gSamp : register(s0);

// Scene lighting stays linear HDR through the render pipeline. This pass
// applies the selected tone curve and the single display gamma transform.

float3 ApplyReinhard(float3 color)
{
    return color / (color + 1.0f);
}

float3 ApplyACESFitted(float3 color)
{
    const float3x3 inputMatrix = float3x3(
        0.59719f, 0.35458f, 0.04823f,
        0.07600f, 0.90834f, 0.01566f,
        0.02840f, 0.13383f, 0.83777f);
    const float3x3 outputMatrix = float3x3(
         1.60475f, -0.53108f, -0.07367f,
        -0.10208f,  1.10813f, -0.00605f,
        -0.00327f, -0.07276f,  1.07602f);

    float3 aces = mul(inputMatrix, max(color, 0.0f));
    const float3 numerator =
        aces * (aces + 0.0245786f) - 0.000090537f;
    const float3 denominator =
        aces * (0.983729f * aces + 0.4329510f) + 0.238081f;
    aces = numerator / max(denominator, 1e-5f);
    return saturate(mul(outputMatrix, aces));
}

// AgX（Troy Sobotka）。少し内側へ寄せた原色の空間で log2 の 16.5 段を [0, 1] に写し、S 字曲線を通す。
// 高い輝度は色相を保ったまま白へ寄り、飽和した青が紫へ転ぶことがない。結果は線形へ戻して返す。
float3 AgxContrast(float3 x)
{
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x -
        0.00232f;
}

float3 ApplyAgX(float3 color)
{
    static const float3x3 kInset = float3x3(
        0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f,
        0.0784335999999992f, 0.878468636469772f, 0.0784336f,
        0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f);
    static const float3x3 kOutset = float3x3(
        1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f,
        -0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f,
        -0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f);
    static const float kMinimumEv = -12.47393f;
    static const float kMaximumEv = 4.026069f;
    float3 x = mul(max(color, 1.0e-10f.xxx), kInset);
    x = saturate((clamp(log2(x), kMinimumEv, kMaximumEv) - kMinimumEv) / (kMaximumEv - kMinimumEv));
    x = AgxContrast(x);
    // 写真に近い見えの look（Blender の Punchy を弱めたもの）。符号化した空間で中間調を締め、彩度を少し戻す。
    const float luma = dot(x, float3(0.2126f, 0.7152f, 0.0722f));
    x = luma + 1.2f * (pow(max(x, 0.0f.xxx), 1.2f) - luma);
    x = mul(x, kOutset);
    return saturate(pow(max(x, 0.0f.xxx), 2.2f));
}

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    float4 sceneColor = gSceneTex.Sample(gSamp, input.uv);
    const int curve = (int)round(gUser[0].x);
    const float exposure = max(gUser[0].y, 0.0f);
    const float gamma = max(gUser[0].z, 0.01f);
    const float3 whiteBalance = max(gUser[1].rgb, 0.0f);
    const float contrast = clamp(gUser[1].a, 0.25f, 2.0f);
    const float saturation = clamp(gUser[2].x, 0.0f, 2.0f);

    float3 color = max(sceneColor.rgb, 0.0f) * exposure * whiteBalance;
    const float luminance = dot(
        color,
        float3(0.2126f, 0.7152f, 0.0722f));
    color = max(
        lerp(luminance.xxx, color, saturation),
        0.0f);
    // 中間灰 0.18 を中心に輝度を対数で伸ばす（階調曲線の前。暗部を 0 で切らず色相も保ち、明部は曲線の肩が受ける）。
    const float graded = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    if (graded > 0.0f)
    {
        color *= 0.18f * pow(graded / 0.18f, contrast) / graded;
    }
    if (curve == 1)
    {
        color = ApplyReinhard(color);
    }
    else if (curve == 2)
    {
        color = ApplyACESFitted(color);
    }
    else if (curve == 3)
    {
        color = ApplyAgX(color);
    }

    float2 screenUv = input.uv * 2.0f - 1.0f;
    screenUv *= 1.0f + gUser[2].y * gUser[2].z * dot(screenUv, screenUv);
    const float2 resolution = max(float2(gResolutionX, gResolutionY), 1.0f.xx);
    const float2 readabilityUv = screenUv * .5f + .5f;
    const float readability = HikariScreenReadabilityAt(readabilityUv, resolution, ReadReadabilityDepth(readabilityUv));
    color = lerp(color, max(sceneColor.rgb, 0.0f), readability);
    color = pow(saturate(color), 1.0f / gamma);
    const float lutSize = gUser[2].w;
    if (lutSize > 0.0f)
    {
        // 格子の中心を引く。読みやすさを保つ領域は調整しない。
        const float3 lutUv = color * ((lutSize - 1.0f) / lutSize) + 0.5f / lutSize;
        color = lerp(gGradingLut.SampleLevel(gSamp, lutUv, 0.0f).rgb, color, readability);
    }
    // 8 bit への量子化で空のなめらかな階調に縞が出ないよう、画素ごとに ±1 段の三角分布の揺らぎを足す。
    const float2 pixel = input.position.xy;
    const float noiseA = frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
    const float noiseB = frac(52.9829189f * frac(dot(pixel + 17.0f, float2(0.00583715f, 0.06711056f))));
    color = saturate(color + (noiseA + noiseB - 1.0f) / 255.0f);
    return float4(color, sceneColor.a);
}
