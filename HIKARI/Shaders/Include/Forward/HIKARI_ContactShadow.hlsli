#ifndef HIKARI_CONTACT_SHADOW_INCLUDED
#define HIKARI_CONTACT_SHADOW_INCLUDED

#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"
#include "Include/Post/HIKARI_PostHash.hlsli"

// 接触影：画面空間で光の方へ短く深度を辿り、カスケード影の分解能では消える足元や小物の接地の影を補う。
// 深度は forward より前に揃う現フレームの半解像度の深度ピラミッド（mip 0、2x2 の遠い方）。無い時は何もしない。
// 命中は想定の厚み（gShadowContactThickness）以内の面だけ。始点を画素とフレームでずらし、TAA が均す。
static const uint HIKARI_CONTACT_SHADOW_STEPS = 8u;

float HikariContactShadow(float3 worldPosWS, float3 geometricNormalWS, float2 pixelPosition, uint receiveShadow)
{
    const float traceLength = gShadowContactLength;
    if (traceLength <= 0.0f || receiveShadow == 0u || gShadowEnabled == 0u ||
        (gScreenSpaceTraceFlags & HIKARI_SCREEN_SPACE_CONTACT_DEPTH_VALID) == 0u)
    {
        return 1.0f;
    }
    const float3 toLight = -gDirectionalDir.xyz * rsqrt(max(dot(gDirectionalDir.xyz, gDirectionalDir.xyz), 1.0e-8f));
    // 光に背く面は既に影の中。光をかすめる面は光線が自分の面に沿って走り、半解像度の深度で自分に当たるので、
    // 明暗の境では効きを弱める（そこは直射光も既に弱い）。
    const float cosine = dot(geometricNormalWS, toLight);
    if (cosine <= 0.02f)
        return 1.0f;
    // 始点を面から少し浮かせ、自分の深度に当たらないようにする。
    const float3 origin = worldPosWS + (geometricNormalWS + toLight) * (traceLength * 0.05f);
    // 遠くでは辿る長さが画面で数画素に満たず、半解像度の深度で自分の面に当たる（地平に黒い線が出る）。
    // 画面での長さが短いほど効きを弱め、2 画素未満では辿らない。
    float2 startUv;
    float2 endUv;
    float unusedDepth;
    float unusedViewDepth;
    if (!HikariProjectWorldPosition(origin, gViewProj, startUv, unusedDepth, unusedViewDepth) ||
        !HikariProjectWorldPosition(origin + toLight * traceLength, gViewProj, endUv, unusedDepth, unusedViewDepth))
    {
        return 1.0f;
    }
    const float reach = saturate((length((endUv - startUv) * gScreenParams.xy) - 2.0f) / 6.0f);
    if (reach <= 0.0f)
        return 1.0f;
    const float strength = smoothstep(0.05f, 0.35f, cosine) * saturate(gShadowContactStrength) * reach;
    const float jitter = HikariHash12(pixelPosition + gShadowFrameIndex * 0.618f);
    [loop] for (uint step = 0u; step < HIKARI_CONTACT_SHADOW_STEPS; ++step)
    {
        const float t = (float(step) + jitter) / float(HIKARI_CONTACT_SHADOW_STEPS);
        float2 uv;
        float projectedDepth;
        float rayViewDepth;
        if (!HikariProjectWorldPosition(origin + toLight * (t * traceLength), gViewProj, uv, projectedDepth,
                rayViewDepth))
        {
            return 1.0f;
        }
        float sceneViewDepth;
        if (!HikariReconstructViewDepth(uv, gScreenSpaceDepthPyramid.SampleLevel(gPointClamp, uv, 0.0f), gInvViewProj,
                sceneViewDepth))
        {
            continue;
        }
        // 光線の点が面より奥、かつ厚み以内なら遮られている。近い命中ほど濃い。
        const float behind = rayViewDepth - sceneViewDepth;
        if (behind > 0.0f && behind < gShadowContactThickness)
            return 1.0f - strength * (1.0f - smoothstep(0.5f, 1.0f, t));
    }
    return 1.0f;
}

#endif
