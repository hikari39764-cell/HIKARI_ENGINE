#ifndef HIKARI_POST_HASH_INCLUDED
#define HIKARI_POST_HASH_INCLUDED

// Shared screen-space hash used by post effects that rotate kernels or add
// grain. One implementation instead of per-pass copies.
float HikariHash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

// Interleaved Gradient Noise（Jimenez 2014）。画素ごとの値が空間的に偏らない [0,1)。白い乱数（HikariHash12）より
// 粒が細かく、少ない tap の回転やずらしでも斑にならない。frame ごとに黄金比ずつずらし、時間方向に混ぜた時に一様に巡る。
float HikariInterleavedGradientNoise(float2 pixel, uint frame)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))) + 0.61803399f * float(frame & 255u));
}

#endif
