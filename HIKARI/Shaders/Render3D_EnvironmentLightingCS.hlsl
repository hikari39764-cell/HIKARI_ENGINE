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
    float4 gSource; // mode, sky-view LUT valid, night sky background [nits], cloud sky valid
};
cbuffer ConvolutionJob : register(b1) { uint gJob; uint gMip; uint gSize; uint gMipCount; };
TextureCube<float4> gSourceCube : register(t0);
Texture2D<float4> gPanorama : register(t1);
Texture2D<float4> gSkyView : register(t2);
// 雲の空（天空 LUT と同じ写像）。rgb 乗算済みの絶対輝度、a 不透明度。
Texture2D<float4> gCloudSky : register(t3);
RWTexture2DArray<float4> gOutput : register(u0);
SamplerState gLinearClamp : register(s0);
SamplerState gPanoramaSampler : register(s1);
#include "Include/Atmosphere/HIKARI_AtmosphereScattering.hlsli"
#include "Include/Sky/HIKARI_SkyRadiance.hlsli"
#include "Include/Sky/HIKARI_EquirectangularSampling.hlsli"
#include "Include/Sky/HIKARI_NightSky.hlsli"

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

float3 OverlayClouds(float3 direction, float3 sky)
{
    if (gSource.w<0.5f) return sky;
    const float4 cloud=gCloudSky.SampleLevel(gPanoramaSampler,HikariAtmosphereSkyViewUv(direction),0);
    return sky*(1.0f-cloud.a)+cloud.rgb;
}

float3 SkyRadiance(float3 direction)
{
    const uint mode=(uint)(gSource.x+0.5f);
    if (mode==2u) return gSourceCube.SampleLevel(gLinearClamp,direction,0).rgb;
    if (mode==3u) return gPanorama.SampleLevel(gPanoramaSampler,HikariDirectionToEquirectangularUv(direction),0).rgb;
    if (mode!=4u) return 0.0f.xxx;
    // 星は点光源で照明への寄与が小さいので、夜空の背景だけを足す。
    const float3 night=HikariNightSkyBackground(direction,gSource.z);
    if (gAtmosphereSunDirection.w<=0.5f) return night;
    // 太陽円盤は直接光 BRDF が所有する。空の散乱輝度だけを畳み込む。
    if (gSource.y>0.5f)
    {
        return gSkyView.SampleLevel(gPanoramaSampler,HikariAtmosphereSkyViewUv(direction),0).rgb+night;
    }
    const float3 observer=float3(0,HikariAtmospherePlanetRadius()+1,0);
    return HikariAtmosphereEvaluateSkyRadianceWithSteps(observer,direction,gAtmosphereGroundColor.rgb,
        gAtmosphereOzoneParams.w,gAtmosphereOzoneParams.y,8u,4u,0.0f.xxx)+night;
}

float3 CaptureRadiance(float3 direction)
{
    return OverlayClouds(direction,SkyRadiance(direction));
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
