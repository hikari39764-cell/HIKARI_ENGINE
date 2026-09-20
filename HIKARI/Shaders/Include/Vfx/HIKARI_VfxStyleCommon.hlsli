#ifndef HIKARI_VFX_STYLE_COMMON
#define HIKARI_VFX_STYLE_COMMON
#define HIKARI_EFFECT(name,id) static const uint Vfx##name = id;
#include "HIKARI_MaterialEffectKinds.hlsli"
#undef HIKARI_EFFECT
Texture2D<float4> gStyleSource : register(t0);
SamplerState gStyleSampler : register(s0);
float4 StyleRead(float2 uv) {
    if (any(uv<0) || any(uv>1)) return 0;
    return gStyleSource.SampleLevel(gStyleSampler,uv,0);
}
float StyleLuma(float3 color) { return dot(max(color,0),float3(.299,.587,.114)); }
float StyleCoverage(float4 value) { return max(saturate(value.a),saturate(max(value.r,max(value.g,value.b)))); }
float3 StyleColor(float4 value) { return value.rgb / (value.a>0.00001 ? value.a : 1); }
float4 StyleWithColor(float4 value,float3 color) { return float4(color*(value.a>0.00001 ? value.a : 1),value.a); }
float StyleHash(float2 p) { return frac(sin(dot(p,float2(12.9898,78.233)))*43758.5453); }
float StyleNoise(float2 p) {
    float2 cell=floor(p),f=frac(p);f=f*f*(3-2*f);
    return lerp(lerp(StyleHash(cell),StyleHash(cell+float2(1,0)),f.x),
        lerp(StyleHash(cell+float2(0,1)),StyleHash(cell+1),f.x),f.y);
}
float StyleCellular(float2 p) {
    float2 cell=floor(p),f=frac(p);float distance=2;
    [unroll]for(int y=-1;y<=1;++y) [unroll]for(int x=-1;x<=1;++x) {
        float2 offset=float2(x,y),id=cell+offset;
        float2 feature=float2(StyleHash(id),StyleHash(id+19.7));
        distance=min(distance,length(offset+feature-f));
    }
    return saturate(distance);
}
float2 StyleRotate(float2 uv,float angle) {
    float s,c;sincos(radians(angle),s,c);return float2(c*uv.x-s*uv.y,s*uv.x+c*uv.y);
}
float2 StyleRadius(float radius) { return radius*float2(1,gResolutionX/max(gResolutionY,1)); }
float StyleWeight(float2 uv,float4 source) {
    uint mask=(uint)gUser[0].z;float value=1;
    float4 p=gUser[5];
    if(mask==1) value=1-smoothstep(p.z,p.z+max(p.w,.00001),length(uv-p.xy));
    if(mask==2 || mask==3) value=1-smoothstep(p.z,p.z+max(p.w,.00001),mask==2?uv.x:uv.y);
    if(mask==4) value=StyleCoverage(source);
    if(mask==5) value=saturate(StyleLuma(StyleColor(source)));
    if(mask!=0 && gUser[0].w>.5) value=1-value;
    return saturate(gUser[0].y)*value;
}
// HDR を有限の階調へ切り下げる。最上段を 1 に丸めて逆変換すると発光が爆発する。
float3 StyleQuantize(float3 color,float levels,float dither) {
    levels=clamp(round(levels),2,64);
    float3 positive=max(color,0),bounded=positive/(1+positive);
    float3 bucket=clamp(floor(bounded*levels+dither),0,levels-1);
    return bucket/max(levels-bucket,1);
}
#endif
