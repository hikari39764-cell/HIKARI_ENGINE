cbuffer EnvironmentLightingCB : register(b0)
{
    float4 gAtmospherePlanetParams;
    float4 gAtmosphereRayleigh;
    float4 gAtmosphereMie;
    float4 gAtmosphereOzone;
    float4 gAtmosphereOzoneParams;
    float4 gAtmosphereSunDirection;
    float4 gAtmosphereSunColorRadiance;
    float4 gAtmosphereGroundColor;
    float4 gZenith;
    float4 gHorizon;
    float4 gGround;
    float4 gSource; // mode, sky-view LUT valid
};
cbuffer ConvolutionJob : register(b1) { uint gJob; uint gMip; uint gSize; uint gMipCount; };
TextureCube<float4> gSourceCube : register(t0);
Texture2D<float4> gPanorama : register(t1);
Texture2D<float4> gSkyView : register(t2);
RWTexture2DArray<float4> gOutput : register(u0);
SamplerState gLinearClamp : register(s0);
SamplerState gPanoramaSampler : register(s1);
#include "Include/Atmosphere/HIKARI_AtmosphereScattering.hlsli"
#include "Include/Sky/HIKARI_SkyRadiance.hlsli"
#include "Include/Sky/HIKARI_EquirectangularSampling.hlsli"

float3 CubeDirection(uint face, float2 uv)
{
    const float2 p=uv*2.0f-1.0f;
    if (face==0u) return normalize(float3(1,-p.y,-p.x));
    if (face==1u) return normalize(float3(-1,-p.y,p.x));
    if (face==2u) return normalize(float3(p.x,1,p.y));
    if (face==3u) return normalize(float3(p.x,-1,-p.y));
    if (face==4u) return normalize(float3(p.x,-p.y,1));
    return normalize(float3(-p.x,-p.y,-1));
}

float3 CaptureRadiance(float3 direction)
{
    const uint mode=(uint)(gSource.x+0.5f);
    if (mode==2u) return gSourceCube.SampleLevel(gLinearClamp,direction,0).rgb;
    if (mode==3u) return gPanorama.SampleLevel(gPanoramaSampler,HikariDirectionToEquirectangularUv(direction),0).rgb;
    if (mode==4u && gAtmosphereSunDirection.w>0.5f)
    {
        // 太陽円盤は直接光 BRDF が所有する。空の散乱輝度だけを畳み込む。
        if (gSource.y>0.5f)
        {
            const float2 uv=float2(frac(atan2(direction.z,direction.x)*0.15915494309f),direction.y*0.5f+0.5f);
            return gSkyView.SampleLevel(gPanoramaSampler,uv,0).rgb;
        }
        const float3 observer=float3(0,HikariAtmospherePlanetRadius()+1,0);
        return HikariAtmosphereEvaluateSkyRadianceWithSteps(observer,direction,gGround.rgb,
            gAtmosphereOzoneParams.w,gAtmosphereOzoneParams.y,8u,4u);
    }
    return HikariEvaluateGradientSky(direction,gZenith.rgb,gHorizon.rgb,gGround.rgb,gHorizon.w);
}

[numthreads(8,8,1)]
void main(uint3 pixel : SV_DispatchThreadID)
{
    if (pixel.x>=gSize || pixel.y>=gSize || pixel.z>=6u) return;
    const float3 n=CubeDirection(pixel.z,(float2(pixel.xy)+0.5f)/float(gSize));
    float3 radiance=0;
    if (gJob==0u) radiance=CaptureRadiance(n);
    else if (gJob==2u && gMip==0u) radiance=gSourceCube.SampleLevel(gLinearClamp,n,0).rgb;
    else
    {
        const float3 axis=abs(n.z)<0.999f ? float3(0,0,1) : float3(1,0,0);
        const float3 tangent=normalize(cross(axis,n)), bitangent=cross(n,tangent);
        const float roughness=float(gMip)/float(max(1u,gMipCount-1u));
        const float alphaSquared=pow(roughness,4.0f);
        float weight=0;
        // 固定上限。画面解像度/材質数に依存せず、更新した cube に一度だけ払う。
        const uint sampleCount=256u;
        [loop] for (uint sample=0u;sample<sampleCount;++sample)
        {
            const float2 u=float2((float(sample)+0.5f)/float(sampleCount),float(reversebits(sample))*2.3283064365386963e-10f);
            const float phi=u.x*6.28318530718f;
            const float cosTheta=gJob==1u ? sqrt(1.0f-u.y) : sqrt((1.0f-u.y)/(1.0f+(alphaSquared-1.0f)*u.y));
            const float sinTheta=sqrt(saturate(1.0f-cosTheta*cosTheta));
            const float3 h=tangent*(cos(phi)*sinTheta)+bitangent*(sin(phi)*sinTheta)+n*cosTheta;
            const float3 l=gJob==1u ? h : reflect(-n,h);
            // cosine sample は E/pi の平均、GGX は split-sum の NdotL 正規化。
            const float w=gJob==1u ? 1.0f : saturate(dot(n,l));
            if (w>0) radiance+=gSourceCube.SampleLevel(gLinearClamp,l,0).rgb*w;
            weight+=w;
        }
        radiance/=max(weight,1e-6f);
    }
    gOutput[pixel]=float4(all(isfinite(radiance)) ? max(radiance,0.0f.xxx) : 0.0f.xxx,1);
}
