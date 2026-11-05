#ifndef HIKARI_CLOUD_SOLAR_VISIBILITY_INCLUDED
#define HIKARI_CLOUD_SOLAR_VISIBILITY_INCLUDED

struct HikariCloudSolarVisibilityData
{
    float4 originDescriptor;
    float4 rightExtent;
    float4 upResolution;
    float4 directionSteps;
};

// tile0 は各列の signed near/far（太陽方向）。tile1..N は far->near の累積T。
uint2 HikariCloudSolarAtlasPixel(uint2 column, uint tile, uint resolution)
{
    return column+uint2(tile%8u,tile/8u)*resolution;
}

float HikariCloudSolarColumn(Texture2D<float4> atlas, uint2 column, uint resolution, uint steps, float receiverDistance)
{
    const float2 interval=atlas.Load(int3(HikariCloudSolarAtlasPixel(column,0u,resolution),0)).rg;
    if(interval.y<=interval.x || receiverDistance>=interval.y) return 1.0f;
    const float depth=saturate((interval.y-receiverDistance)/(interval.y-interval.x))*float(steps);
    const uint lower=min(uint(depth),steps);
    const float first=lower==0u ? 1.0f : atlas.Load(int3(HikariCloudSolarAtlasPixel(column,lower,resolution),0)).r;
    const float second=atlas.Load(int3(HikariCloudSolarAtlasPixel(column,min(lower+1u,steps),resolution),0)).r;
    return saturate(lerp(first,second,frac(depth)));
}

// 表の無絆定の貼図の番号（originDescriptor.w は番号 + 1、0 で表なし。表なしでも 0 番を返し、読む関数が 1 を返す）。
// 雲の影と流体の烟の影（HikariFluidSolarVisibility）が同じ並びで、面の太陽は二つを掛ける。
uint HikariSolarVisibilityDescriptor(HikariCloudSolarVisibilityData field)
{
    return uint(max(field.originDescriptor.w, 1.0f)) - 1u;
}

float HikariSampleCloudSolarVisibility(Texture2D<float4> atlas, HikariCloudSolarVisibilityData field, float3 worldPosition)
{
    if(field.originDescriptor.w<0.5f || field.rightExtent.w<=0 || field.upResolution.w<2 || field.directionSteps.w<1)
        return 1.0f;
    const float3 relative=worldPosition-field.originDescriptor.xyz;
    const float2 uv=float2(dot(relative,field.rightExtent.xyz),dot(relative,field.upResolution.xyz))/(2*field.rightExtent.w)+.5f;
    if(any(uv<=0) || any(uv>=1)) return 1.0f;
    const uint resolution=uint(field.upResolution.w),steps=uint(field.directionSteps.w);
    const float2 pixel=uv*float(resolution)-.5f;
    const int2 base=int2(floor(pixel)); const float2 fraction=frac(pixel);
    const float receiver=dot(relative,field.directionSteps.xyz);
    float transmittance=0;
    [unroll] for(uint y=0u;y<2u;++y) [unroll] for(uint x=0u;x<2u;++x)
    {
        const uint2 column=uint2(clamp(base+int2(x,y),0,int(resolution)-1));
        const float weight=(x==0u ? 1-fraction.x : fraction.x)*(y==0u ? 1-fraction.y : fraction.y);
        transmittance+=weight*HikariCloudSolarColumn(atlas,column,resolution,steps,receiver);
    }
    // 列ごとの shell 区間を補間前に評価し、雲の上/内部で地表用の影を重ねない。
    const float edge=min(min(uv.x,uv.y),min(1-uv.x,1-uv.y));
    return lerp(1.0f,transmittance,saturate(edge*float(resolution)*.25f));
}

#endif
