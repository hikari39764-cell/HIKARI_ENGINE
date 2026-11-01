#ifndef HIKARI_LIB_SHAPES_HLSLI
#define HIKARI_LIB_SHAPES_HLSLI

// 符号付き距離（SDF）。外が正、内が負。2D は p をその形の中心からの座標で渡す。

float HikariSdCircle(float2 p, float radius) { return length(p) - radius; }

// halfSize は半分の幅と高さ。
float HikariSdBox(float2 p, float2 halfSize)
{
    const float2 d = abs(p) - halfSize;
    return length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f);
}

float HikariSdRoundBox(float2 p, float2 halfSize, float radius)
{
    return HikariSdBox(p, halfSize - radius) - radius;
}

// a から b への線分（太さ 0）。カプセルは HikariSdSegment(p, a, b) - radius。
float HikariSdSegment(float2 p, float2 a, float2 b)
{
    const float2 pa = p - a, ba = b - a;
    const float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1.0e-8f));
    return length(pa - ba * h);
}

// 半径 radius、太さ thickness の輪。
float HikariSdRing(float2 p, float radius, float thickness) { return abs(length(p) - radius) - thickness * 0.5f; }

float HikariSdSphere(float3 p, float radius) { return length(p) - radius; }

float HikariSdBox3(float3 p, float3 halfSize)
{
    const float3 d = abs(p) - halfSize;
    return length(max(d, 0.0f)) + min(max(d.x, max(d.y, d.z)), 0.0f);
}

float HikariSdCapsule(float3 p, float3 a, float3 b, float radius)
{
    const float3 pa = p - a, ba = b - a;
    const float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1.0e-8f));
    return length(pa - ba * h) - radius;
}

// xz 平面の輪（majorRadius）の太さ minorRadius のドーナツ。
float HikariSdTorus(float3 p, float majorRadius, float minorRadius)
{
    return length(float2(length(p.xz) - majorRadius, p.y)) - minorRadius;
}

// 合わせ方。Smooth は k（距離の単位）の幅で継ぎ目を丸める。
float HikariOpUnion(float a, float b) { return min(a, b); }
float HikariOpIntersect(float a, float b) { return max(a, b); }
float HikariOpSubtract(float a, float b) { return max(a, -b); }
float HikariOpSmoothUnion(float a, float b, float k)
{
    const float h = saturate(0.5f + 0.5f * (b - a) / max(k, 1.0e-5f));
    return lerp(b, a, h) - k * h * (1.0f - h);
}
float HikariOpSmoothSubtract(float a, float b, float k) { return -HikariOpSmoothUnion(-a, b, k); }
float HikariOpSmoothIntersect(float a, float b, float k) { return -HikariOpSmoothUnion(-a, -b, k); }

// 距離を 1 画素ぶんぼかした塗り（0..1）。画素シェーダーで使う（fwidth を使う）。
float HikariSdfFill(float distance)
{
    return saturate(0.5f - distance / max(fwidth(distance), 1.0e-5f));
}

#endif
