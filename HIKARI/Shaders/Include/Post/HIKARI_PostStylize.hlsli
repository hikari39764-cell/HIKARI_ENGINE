#ifndef HIKARI_POST_STYLIZE_HLSLI
#define HIKARI_POST_STYLIZE_HLSLI

// 表示の色の段階（phase Display）で画を描き直す効果の共通部品。大きさは 1080p の画素で指定し、解像度で拡大する。
#include "HIKARI_PostEffectInterface.hlsli"
#include "HIKARI_PostHash.hlsli"
#include "HIKARI_PostSurfaceInputs.hlsli"
#include "../Color/HIKARI_Luminance.hlsli"

Texture2D gSceneTex : register(t0);
SamplerState gSamp : register(s0);

// 1080p の画素を今の出力の画素へ。
float HikariStylizePixelScale()
{
    return max(gResolutionY, 1.0f) / 1080.0f;
}

float2 HikariStylizeTexel()
{
    return rcp(max(float2(gResolutionX, gResolutionY), 1.0f.xx));
}

// 幾何の線。1/z の二階差（平面の上では 0）を中心の 1/z で割った値で測る。
// x は手前側の段差（輪郭と凸の折れ目。線は手前の面に引く）、y は凹の折れ目（奥まった角）。
// 大きな段差の奥側は線にしない（輪郭が二重にならない）。radiusPixels だけ離れた上下左右を見るので線の太さもこれで決まる。
float2 HikariStylizeGeometryEdges(float2 uv, float radiusPixels)
{
    const float2 offset = HikariStylizeTexel() * max(radiusPixels, 0.5f);
    const float center = HikariPostReadInverseDepth(uv);
    if (center <= 0.0f)
        return 0.0f.xx;
    const float left = HikariPostReadInverseDepth(uv - float2(offset.x, 0.0f));
    const float right = HikariPostReadInverseDepth(uv + float2(offset.x, 0.0f));
    const float up = HikariPostReadInverseDepth(uv - float2(0.0f, offset.y));
    const float down = HikariPostReadInverseDepth(uv + float2(0.0f, offset.y));
    const float2 secondDifference = float2(left + right, up + down) - 2.0f * center;
    const float nearSide = max(-secondDifference.x, -secondDifference.y) / center;
    const float farSide = max(secondDifference.x, secondDifference.y) / center;
    return float2(saturate(nearSide), farSide < 0.25f ? saturate(farSide) : 0.0f);
}

// 表示の明るさを白点と黒点で 0..1 へ伸ばす。白点より明るい所は紙の白、黒点より暗い所はベタになる。
float HikariStylizeTone(float3 color, float blackPoint, float whitePoint)
{
    return saturate((HikariLuminance(color) - blackPoint) / max(whitePoint - blackPoint, 0.01f));
}

// 明るさの線（Sobel）。模様や影の境に線を足す。
float HikariStylizeLumaEdge(float2 uv, float radiusPixels)
{
    const float2 t = HikariStylizeTexel() * max(radiusPixels, 0.5f);
    const float nw = HikariLuminance(gSceneTex.Sample(gSamp, uv - t).rgb);
    const float n = HikariLuminance(gSceneTex.Sample(gSamp, uv + float2(0.0f, -t.y)).rgb);
    const float ne = HikariLuminance(gSceneTex.Sample(gSamp, uv + float2(t.x, -t.y)).rgb);
    const float w = HikariLuminance(gSceneTex.Sample(gSamp, uv + float2(-t.x, 0.0f)).rgb);
    const float e = HikariLuminance(gSceneTex.Sample(gSamp, uv + float2(t.x, 0.0f)).rgb);
    const float sw = HikariLuminance(gSceneTex.Sample(gSamp, uv + float2(-t.x, t.y)).rgb);
    const float s = HikariLuminance(gSceneTex.Sample(gSamp, uv + float2(0.0f, t.y)).rgb);
    const float se = HikariLuminance(gSceneTex.Sample(gSamp, uv + t).rgb);
    const float gx = (ne + 2.0f * e + se) - (nw + 2.0f * w + sw);
    const float gy = (sw + 2.0f * s + se) - (nw + 2.0f * n + ne);
    return length(float2(gx, gy));
}

// 線の強さを感度（0..1）から。深度の段差は相対値、明るさの線は Sobel の大きさで測る。空の画素は明るさの線を引かない
// （雲の細かな明暗が線の雑音になる）。
float HikariStylizeToneLines(float2 uv, float radiusPixels, float lumaSensitivity)
{
    const float lumaThreshold = lerp(1.2f, 0.08f, saturate(lumaSensitivity));
    const float lumaLine = HikariPostReadInverseDepth(uv) > 0.0f
        ? smoothstep(lumaThreshold, lumaThreshold * 2.0f, HikariStylizeLumaEdge(uv, radiusPixels))
        : 0.0f;
    return lumaLine;
}

// 格子点の乱数を双一次で補間した値の雑音（0..1）。
float HikariStylizeValueNoise(float2 p)
{
    const float2 cell = floor(p);
    const float2 f = p - cell;
    const float2 u = f * f * (3.0f - 2.0f * f);
    const float a = HikariHash12(cell);
    const float b = HikariHash12(cell + float2(1.0f, 0.0f));
    const float c = HikariHash12(cell + float2(0.0f, 1.0f));
    const float d = HikariHash12(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// 画素位置を angleDegrees だけ回した格子の座標（1 単位 = cellPixels 画素）。
float2 HikariStylizeRotatedGrid(float2 pixel, float cellPixels, float angleDegrees)
{
    const float angle = radians(angleDegrees);
    const float2 axisX = float2(cos(angle), sin(angle));
    const float2 axisY = float2(-axisX.y, axisX.x);
    return float2(dot(pixel, axisX), dot(pixel, axisY)) / max(cellPixels, 1.0f);
}

// 等間隔の平行線（線の幅は間隔に対する割合 coverage）。画素一つ分で縁を均す。
float HikariStylizeLines(float coordinate, float coverage)
{
    const float distanceToLine = abs(frac(coordinate) - 0.5f);
    const float width = 0.5f * saturate(coverage);
    const float aa = max(fwidth(coordinate), 1e-4f);
    return 1.0f - smoothstep(width - aa, width + aa, distanceToLine);
}

#endif
