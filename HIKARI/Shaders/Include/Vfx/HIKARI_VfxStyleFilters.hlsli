#ifndef HIKARI_VFX_STYLE_FILTERS
#define HIKARI_VFX_STYLE_FILTERS
float4 StyleFilter(uint kind,float2 uv,float4 source) {
    float4 p=gUser[1],q=gUser[2];
    if(kind==VfxPixelate) {
        float2 cell=max(StyleRadius(p.x),1/float2(gResolutionX,gResolutionY));
        return lerp(source,StyleRead((floor(uv/cell)+.5)*cell),saturate(p.y));
    }
    if(kind==VfxBlur || kind==VfxStreak || kind==VfxRadialBlur || kind==VfxGlow) {
        float radius=kind==VfxGlow?p.w:p.x;
        if(radius<=0 && kind!=VfxGlow)return source;
        uint taps=kind==VfxRadialBlur?(uint)clamp(round(p.w),1,8):(uint)clamp(round(kind==VfxStreak || kind==VfxBlur?p.z:4),1,8);
        float2 axis=StyleRotate(float2(1,0),p.y)*StyleRadius(radius);
        float4 sum=0;float3 farSum=0;float weightTotal=0;
        [loop]for(int i=-int(taps);i<=int(taps);++i) {
            if((kind==VfxStreak || kind==VfxRadialBlur) && i>0)continue;
            float distance=float(i)/max(float(taps),1);
            float weight=kind==VfxStreak?pow(max(1-saturate(p.w),.001),abs(float(i))):exp(-3*distance*distance);
            float2 offset=axis*distance;
            if(kind==VfxRadialBlur)offset=(uv-p.yz)*p.x*distance*(q.x<0?-1:1);
            float4 sample=StyleRead(uv+offset);
            if(kind==VfxGlow) {
                // 発光抽出は被覆済みの放射輝度で行い、透明 RGB の縁を発光させない。
                float luma=StyleLuma(sample.rgb);
                sample=float4(sample.rgb*smoothstep(p.x,p.x+max(p.z,.0001),luma),0);
            }
            sum+=sample*weight;weightTotal+=weight;
            if((kind==VfxBlur && p.w>.5) || kind==VfxGlow) {
                float4 other=StyleRead(uv+float2(-offset.y,offset.x));
                if(kind==VfxGlow)other=float4(other.rgb*smoothstep(p.x,p.x+max(p.z,.0001),StyleLuma(other.rgb)),0);
                sum+=other*weight;weightTotal+=weight;
            }
            if(kind==VfxGlow && q.y>0 && q.x>0) {
                float2 farOffset=StyleRadius(q.x)*float2(distance,0);
                float4 horizontal=StyleRead(uv+farOffset);
                float4 vertical=StyleRead(uv+float2(0,farOffset.y==0?StyleRadius(q.x).y*distance:farOffset.y));
                farSum+=(horizontal.rgb*smoothstep(p.x,p.x+max(p.z,.0001),StyleLuma(horizontal.rgb))+
                    vertical.rgb*smoothstep(p.x,p.x+max(p.z,.0001),StyleLuma(vertical.rgb)))*weight;
            }
        }
        float4 filtered=sum/max(weightTotal,.0001);
        return kind==VfxGlow?float4(source.rgb+(filtered.rgb+farSum/max(weightTotal,.0001)*max(q.y,0))*max(p.y,0)*gUser[3].rgb,source.a):filtered;
    }
    if(kind==VfxChromatic) {
        float2 offset=lerp(StyleRotate(float2(1,0),p.y),uv-.5,saturate(p.z))*StyleRadius(p.x);
        float4 r=StyleRead(uv+offset),b=StyleRead(uv-offset);
        float4 shifted=float4(r.r,source.g,b.b,max(source.a,max(r.a,b.a)));
        return lerp(source,shifted,saturate(p.w));
    }
    if(kind==VfxAlphaMorph || kind==VfxOutline || kind==VfxInk) {
        float2 radius=StyleRadius(abs(p.x));
        float4 taps[4]={StyleRead(uv+float2(radius.x,0)),StyleRead(uv-float2(radius.x,0)),
            StyleRead(uv+float2(0,radius.y)),StyleRead(uv-float2(0,radius.y))};
        float coverage=StyleCoverage(source),lo=coverage,hi=coverage;
        float luma=StyleLuma(StyleColor(source)),lowLuma=luma,highLuma=luma;
        float3 expandedColor=StyleColor(source);
        [unroll]for(uint i=0;i<4;++i) {
            float c=StyleCoverage(taps[i]);
            if(c>hi)expandedColor=StyleColor(taps[i]);
            lo=min(lo,c);hi=max(hi,c);
            float l=StyleLuma(StyleColor(taps[i]));lowLuma=min(lowLuma,l);highLuma=max(highLuma,l);
        }
        if(kind==VfxAlphaMorph) {
            float target=p.x>=0?hi:lo;
            target=lerp(target,(hi+lo)*.5,saturate(p.y/max(abs(p.x),.0001)));
            float alpha=lerp(coverage,target,saturate(p.z));
            return float4((p.x>=0?expandedColor:StyleColor(source))*alpha,alpha);
        }
        if(kind==VfxOutline) {
            float edge=(p.y>.5?coverage-lo:hi-coverage)*saturate(p.z)*saturate(p.w)*saturate(gUser[3].a);
            float alpha=saturate(source.a+edge);
            return float4(lerp(source.rgb,gUser[3].rgb*coverage,p.y>.5?saturate(edge/max(coverage,.0001)):0)+
                (p.y>.5?0:gUser[3].rgb*edge),alpha);
        }
        // 墨は色面全体を染める。輪郭だけの暗色加算では不透明な底図に何も起きない。
        float wash=luma/(1+luma);
        float edge=saturate(hi-lo+(highLuma-lowLuma)/(1+highLuma));
        float paper=StyleNoise(uv*max(q.y,.1)*256);
        float pigment=saturate(.3+(1-wash)*.5+edge*max(p.y,0)+(paper-.5)*saturate(p.z));
        float3 ink=lerp(gUser[3].rgb+.22,gUser[3].rgb,pigment);
        ink*=1-paper*saturate(q.w)*.35;
        float alpha=lerp(coverage,hi,saturate(q.x/max(abs(p.x),.0001)));
        alpha*=saturate(1-paper*saturate(q.z))*saturate(gUser[3].a);
        return lerp(source,float4(max(ink,0)*alpha,alpha),saturate(p.w));
    }
    return source;
}
#endif
