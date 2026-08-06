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

#endif
