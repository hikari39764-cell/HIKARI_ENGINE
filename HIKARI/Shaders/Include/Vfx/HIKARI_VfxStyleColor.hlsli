#ifndef HIKARI_VFX_STYLE_COLOR
#define HIKARI_VFX_STYLE_COLOR
float3 StyleHue(float3 rgb,float angle) {
    float s,c;sincos(radians(angle),s,c);float d=(1+2*c)/3,a=(1-c)/3-s*.57735027,b=(1-c)/3+s*.57735027;
    return max(0,float3(dot(rgb,float3(d,a,b)),dot(rgb,float3(b,d,a)),dot(rgb,float3(a,b,d))));
}
float4 StyleSurface(uint kind,float2 uv,float4 source) {
    float4 p=gUser[1],q=gUser[2];float3 color=StyleColor(source);float luma=StyleLuma(color);
    if(kind==VfxOpacity)return source*saturate(p.x);
    if(kind==VfxColorCorrect) { color=((color-p.w)*p.x-.5)*p.y+.5;color=StyleHue(lerp(StyleLuma(color).xxx,color,p.z),q.x); }
    else if(kind==VfxPosterize || kind==VfxDither) {
        float dither=0;
        if(kind==VfxDither) {
            uint2 cell=(uint2)floor(uv/max(StyleRadius(p.y),.0001));
            uint2 a=cell&1,b=(cell>>1)&1;
            float bayer=float((2*(a.x^a.y)+a.y)*4+2*(b.x^b.y)+b.y)+.5;
            dither=(bayer/16-.5)*saturate(p.z);
        }
        color=lerp(color,StyleQuantize(color,p.x,dither),saturate(kind==VfxDither?p.w:p.y));
    }
    else if(kind==VfxLevels) { float3 graded=pow(max((color-p.x)/max(p.y-p.x,.0001),0),1/max(p.z,.05));color=lerp(color,graded,saturate(p.w)); }
    else if(kind==VfxChannelMixer) return source*lerp(1,saturate(dot(color,p.xyz)),saturate(p.w));
    else if(kind==VfxLuminanceMask) {
        float a=smoothstep(p.x,p.x+max(p.y,.0001),luma);
        return p.z>.5?float4(color*a,a):source*a;
    }
    else if(kind==VfxDuotone || kind==VfxThreshold || kind==VfxHalftone || kind==VfxGradient) {
        float tone=saturate(luma/(1+luma));
        if(kind==VfxDuotone)tone=saturate((tone-.5)*p.y+.5+p.x);
        if(kind==VfxThreshold)tone=smoothstep(p.x-max(p.y,.001),p.x+max(p.y,.001),luma);
        if(kind==VfxHalftone) {
            float2 cell=frac(StyleRotate(uv,p.y)/max(StyleRadius(p.x),.001))-.5;
            float radius=sqrt(saturate(tone*p.z))*.7;
            tone=1-smoothstep(radius,radius+.04,length(cell));
        }
        float3 styled=lerp(gUser[3].rgb,gUser[4].rgb,tone);
        if(kind==VfxGradient && gCombo>=2) {
            styled=gUser[8].rgb;
            [loop]for(uint i=1;i<(uint)min(gCombo,8);++i) {
                float start=i<=4?gUser[6][i-1]:gUser[7][i-5];
                float end=i<4?gUser[6][i]:gUser[7][i-4];
                styled=lerp(styled,gUser[8+i].rgb,smoothstep(start,max(end,start+.0001),tone));
            }
        }
        if(kind!=VfxGradient)styled*=lerp(1,max(luma,1),kind==VfxHalftone?1:saturate(p.z));
        color=lerp(color,styled,kind==VfxGradient?1:saturate(p.w));
    }
    else if(kind==VfxNoise) {
        float2 at=uv*max(p.x,.01)+(p.y+q.y)*float2(1,.371);
        float n=q.x>.5?StyleCellular(at):(StyleNoise(at)+StyleNoise(at*2.7)*p.z)/(1+p.z);
        n=saturate(.5+(n-.5)*p.w);
        return StyleWithColor(source,color*lerp(1,n,saturate(q.w)))*lerp(1,n,saturate(q.z));
    }
    else if(kind==VfxVignette) { float2 delta=(uv-.5)*float2(p.w,1);color*=1-smoothstep(p.y,p.y+max(p.z,.001),length(delta)*2)*saturate(p.x); }
    else if(kind==VfxGrain) { float n=StyleHash(floor(uv/max(StyleRadius(p.y),.001))+p.z);color*=max(0,1+(n*2-1)*p.x*p.w); }
    else if(kind==VfxScanlines) { float phase=(StyleRotate(uv,p.w).y/max(p.x,.001)+p.z)*6.2831853;color*=1-(.5+.5*cos(phase))*saturate(p.y); }
    else if(kind==VfxLightSweep) {
        float at=dot(uv,StyleRotate(float2(1,0),p.z)),width=max(p.y,.001)*.5;
        color+=gUser[3].rgb*smoothstep(p.x-width,p.x,at)*(1-smoothstep(p.x,p.x+width,at))*p.w;
    }
    return StyleWithColor(source,max(color,0));
}
#endif
