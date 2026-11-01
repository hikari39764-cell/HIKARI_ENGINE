#ifndef HIKARI_LIB_LIGHTING_HLSLI
#define HIKARI_LIB_LIGHTING_HLSLI

// 風格化や試作で使う簡単な光の受け方。物理ベースの本体（GGX、Fresnel、拡散と鏡面の分け方）は
// Include/HIKARI_PbrCommon.hlsli を使う。n, l, v は正規化済みで、l と v は面から外へ向く。
#include "Include/HIKARI_PbrCommon.hlsli"

float HikariLambert(float3 n, float3 l) { return saturate(dot(n, l)); }

// 半 Lambert（Valve）：裏側も 0 にならず柔らかく暗くなる。
float HikariHalfLambert(float3 n, float3 l)
{
    const float h = dot(n, l) * 0.5f + 0.5f;
    return h * h;
}

// 光を wrap（0..1）だけ裏へ回り込ませる。
float HikariWrapLighting(float3 n, float3 l, float wrap) { return saturate((dot(n, l) + wrap) / (1.0f + wrap)); }

float HikariBlinnPhong(float3 n, float3 l, float3 v, float shininess)
{
    return pow(saturate(dot(n, normalize(l + v))), max(shininess, 1.0f));
}

// 縁の光：視線と直交する所ほど 1 に近い。
float HikariRim(float3 n, float3 v, float power) { return pow(1.0f - saturate(dot(n, v)), max(power, 0.01f)); }

// 当たりの量 x（0..1）を steps 段に分け、段の境を softness の幅でぼかす（0..1）。描画様式の Cel と同じ考え方。
float HikariToonRamp(float x, float steps, float softness)
{
    const float n = max(steps, 1.0f);
    float band = 0.0f;
    [loop]
    for (float level = 1.0f; level <= n; level += 1.0f)
    {
        const float edge = level / (n + 1.0f);
        band += smoothstep(edge - softness, edge + softness, x);
    }
    return band / n;
}

// 視点空間の法線から MatCap テクスチャの UV。
float2 HikariMatcapUv(float3 viewSpaceNormal) { return viewSpaceNormal.xy * float2(0.5f, -0.5f) + 0.5f; }

#endif
