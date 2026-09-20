#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
#include "Include/Vfx/HIKARI_VfxStyleCommon.hlsli"
#include "Include/Vfx/HIKARI_VfxStyleFilters.hlsli"
#include "Include/Vfx/HIKARI_VfxStyleColor.hlsli"
float4 main(HikariPostEffectInput input):SV_TARGET {
    float4 source=StyleRead(input.uv);
    uint kind=(uint)gUser[0].x;
    float4 filtered=StyleFilter(kind,input.uv,source);
    // フィルタは前段の完成画像、色処理は同じ被覆契約を使用する。
    float4 styled=StyleSurface(kind,input.uv,filtered);
    if(StyleCoverage(source)<=0 && kind!=VfxBlur && kind!=VfxStreak && kind!=VfxRadialBlur &&
        kind!=VfxGlow && kind!=VfxOutline && kind!=VfxAlphaMorph && kind!=VfxInk && kind!=VfxChromatic && kind!=VfxPixelate) styled=0;
    float4 result=lerp(source,styled,StyleWeight(input.uv,source));
    result=float4(clamp(result.rgb,0,65504),saturate(result.a));
    return any(isnan(result))?source:result;
}
