#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
#include "Include/Vfx/HIKARI_VfxStyleCommon.hlsli"
#include "Include/Vfx/HIKARI_VfxStyleFilters.hlsli"
#include "Include/Vfx/HIKARI_VfxStyleColor.hlsli"
float4 main(HikariPostEffectInput input):SV_TARGET {
    float4 source=StyleRead(input.uv);
    uint kind=(uint)gUser[0].x;
    const bool emptyPoint = StyleCoverage(source)<=0 && kind!=VfxBlur && kind!=VfxStreak && kind!=VfxRadialBlur &&
        kind!=VfxGlow && kind!=VfxOutline && kind!=VfxAlphaMorph && kind!=VfxInk && kind!=VfxChromatic && kind!=VfxPixelate;
    float4 styled=0;
    // 被覆を拡張しない処理は空画素で評価しない。近傍フィルタは従来どおり評価する。
    [branch] if (!emptyPoint) {
        float4 filtered=StyleFilter(kind,input.uv,source);
        styled=StyleSurface(kind,input.uv,filtered);
    }
    float4 result=lerp(source,styled,StyleWeight(input.uv,source));
    result=float4(clamp(result.rgb,0,65504),saturate(result.a));
    return any(isnan(result))?source:result;
}
