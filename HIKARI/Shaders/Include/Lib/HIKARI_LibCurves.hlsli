#ifndef HIKARI_LIB_CURVES_HLSLI
#define HIKARI_LIB_CURVES_HLSLI

// 値の写し替えと形の決まった曲線。どれも引数だけで計算する。

// x を [inMin, inMax] から [outMin, outMax] へ写す（範囲の外もそのまま延ばす）。
float HikariRemap(float x, float inMin, float inMax, float outMin, float outMax)
{
    return outMin + (x - inMin) * (outMax - outMin) / (inMax - inMin);
}

// 写したあと [outMin, outMax] に収める。
float HikariRemapClamped(float x, float inMin, float inMax, float outMin, float outMax)
{
    const float t = saturate((x - inMin) / (inMax - inMin));
    return lerp(outMin, outMax, t);
}

// lerp の逆：x が a から b までのどこにあるか（0..1、外は延ばす）。
float HikariInverseLerp(float a, float b, float x)
{
    return (x - a) / (b - a);
}

// smoothstep より端が平らな 5 次の段（Perlin）。
float HikariSmootherstep(float edge0, float edge1, float x)
{
    const float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Schlick の bias（b < 0.5 で暗い側へ、> 0.5 で明るい側へ寄せる）と gain（中央の傾き）。0..1 の x に使う。
float HikariBias(float x, float b)
{
    return x / ((1.0f / max(b, 1.0e-5f) - 2.0f) * (1.0f - x) + 1.0f);
}
float HikariGain(float x, float g)
{
    return x < 0.5f ? HikariBias(2.0f * x, g) * 0.5f : 1.0f - HikariBias(2.0f - 2.0f * x, g) * 0.5f;
}

// 緩急（t は 0..1）。
float HikariEaseInQuad(float t) { return t * t; }
float HikariEaseOutQuad(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
float HikariEaseInOutQuad(float t) { return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t); }
float HikariEaseInCubic(float t) { return t * t * t; }
float HikariEaseOutCubic(float t) { const float u = 1.0f - t; return 1.0f - u * u * u; }
float HikariEaseInOutCubic(float t) { return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * pow(1.0f - t, 3.0f); }
float HikariEaseOutExpo(float t) { return t >= 1.0f ? 1.0f : 1.0f - exp2(-10.0f * t); }
// 少し行き過ぎて戻る（overshoot は 1.70158 が標準）。
float HikariEaseOutBack(float t, float overshoot)
{
    const float u = t - 1.0f;
    return 1.0f + (overshoot + 1.0f) * u * u * u + overshoot * u * u;
}

// center を中心に幅 width の滑らかな山（0..1）。
float HikariPulse(float x, float center, float width)
{
    const float d = abs(x - center) / max(width, 1.0e-5f);
    return 1.0f - smoothstep(0.0f, 1.0f, d);
}

// 0..1 の x を steps 段に量子化する（段の値は 0..1 に均等）。
float HikariQuantize(float x, float steps)
{
    const float n = max(steps, 1.0f);
    return floor(saturate(x) * n * 0.9999f) / max(n - 1.0f, 1.0f);
}

// 周期 1 ののこぎり波（0..1）と三角波（0..1..0）。
float HikariSawtooth(float x) { return frac(x); }
float HikariTriangleWave(float x) { return 1.0f - abs(frac(x) * 2.0f - 1.0f); }

// 0 で 0、1 で 0、中央で 1 になる放物線を k で尖らせる（Inigo Quilez）。
float HikariParabola(float x, float k) { return pow(4.0f * x * (1.0f - x), k); }

// 立ち上がってから指数で減る衝撃（1/k で最大 1）。打撃の光り方に使う。
float HikariImpulse(float x, float k)
{
    const float h = k * x;
    return h * exp(1.0f - h);
}

#endif
