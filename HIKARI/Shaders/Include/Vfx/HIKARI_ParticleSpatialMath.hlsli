#ifndef HIKARI_PARTICLE_SPATIAL_MATH
#define HIKARI_PARTICLE_SPATIAL_MATH
// 局所空間変形の CPU/GPU 共通実装。カメラ、画面座標は入力に持たない。
#ifdef __cplusplus
namespace HIKARI::RENDER3D::PARTICLES {
using float3=MATH::Vec3;
using float4=MATH::Vec4;
#define SP_INLINE inline
#define SP_SIN std::sin
#define SP_COS std::cos
#define SP_SQRT std::sqrt
#define SP_ATAN2 std::atan2
#define SP_ABS std::abs
#define SP_MAX (std::max)
#define SP_CLAMP std::clamp
#else
#define SP_INLINE
#define SP_SIN sin
#define SP_COS cos
#define SP_SQRT sqrt
#define SP_ATAN2 atan2
#define SP_ABS abs
#define SP_MAX max
#define SP_CLAMP clamp
#endif
#define HIKARI_EFFECT(name,id) static const unsigned int Spatial##name=id;
#include "HIKARI_MaterialEffectKinds.hlsli"
#undef HIKARI_EFFECT
SP_INLINE float SpatialLength(float3 p) { return SP_SQRT(p.x*p.x+p.y*p.y+p.z*p.z); }
SP_INLINE float SpatialSaturate(float x) { return SP_CLAMP(x,0.0f,1.0f); }
SP_INLINE float3 ParticleDeformLocal(unsigned int kind,float3 position,float4 p,float4 q) {
    float3 result=position;
    if(kind==SpatialTwirl) {
        float x=position.x-(p.z-.5f),y=position.y-(p.w-.5f);
        float f=SpatialSaturate(1-SP_SQRT(x*x+y*y)/SP_MAX(p.y,.001f));
        float a=p.x*.01745329252f*f*f,c=SP_COS(a),s=SP_SIN(a);
        result.x=x*c-y*s+p.z-.5f;result.y=x*s+y*c+p.w-.5f;
    } else if(kind==SpatialPolar) {
        // 長さ X を円弧へ写す。横断面 Y と厚み Z を保持する三次元の曲げ。
        float x=position.x-(p.y-.5f),y=position.y-(p.z-.5f);
        float k=6.2831853f*SP_MAX(p.w,.001f),radius=1/k;
        if(q.x>.5f) {
            result.x=SP_ATAN2(x,y+radius)/k+p.y-.5f;
            result.y=SP_SQRT(x*x+(y+radius)*(y+radius))-radius+p.z-.5f;
        } else {
            result.x=(radius+y)*SP_SIN(x*k)+p.y-.5f;
            result.y=(radius+y)*SP_COS(x*k)-radius+p.z-.5f;
        }
        result=position+(result-position)*SpatialSaturate(p.x);
    } else if(kind==SpatialWave) {
        float x=position.x-(q.y-.5f),y=position.y-(q.z-.5f);
        float distance=SP_SQRT(x*x+y*y),angle=q.w*.01745329252f;
        float axis=q.x>.5f?distance:x*SP_COS(angle)+y*SP_SIN(angle);
        result.z+=SP_SIN((axis*p.y-p.z)*6.2831853f)*p.x*SpatialSaturate(1-distance*p.w);
    } else if(kind==SpatialTurbulentDisplace) {
        float3 at=position*SP_MAX(p.y,.01f)+float3(p.z,p.z*.731f,p.z*1.173f);
        float amount=p.x;
        result=result+float3(SP_SIN(at.y+SP_COS(at.z)),SP_SIN(at.z+SP_COS(at.x)),SP_SIN(at.x+SP_COS(at.y)))*amount;
        at=at*2.7f+float3(19.3f,7.7f,31.7f);
        result=result+float3(SP_SIN(at.y+SP_COS(at.z)),SP_SIN(at.z+SP_COS(at.x)),SP_SIN(at.x+SP_COS(at.y)))*(amount*p.w);
    }
    return result;
}
SP_INLINE float ParticleSpatialMask(unsigned int mask,bool inverted,float3 position,float4 p) {
    float value=1;
    if(mask>=1 && mask<=3) {
        float x=position.x+.5f,y=position.y+.5f;
        float field=mask==1?SP_SQRT((x-p.x)*(x-p.x)+(y-p.y)*(y-p.y)):(mask==2?x:y);
        float t=SpatialSaturate((field-p.z)/SP_MAX(p.w,.00001f));value=1-t*t*(3-2*t);
        if(inverted)value=1-value;
    }
    return value;
}
SP_INLINE float ParticleSpatialBoundExpansion(unsigned int kind,float4 p,float4 q) {
    if(kind==SpatialTwirl)return 2*SpatialLength(float3(p.z-.5f,p.w-.5f,0));
    if(kind==SpatialPolar)return 2*SpatialLength(float3(p.y-.5f,p.z-.5f,0))+2/(6.2831853f*SP_MAX(p.w,.001f))+(q.x>.5f?.5f/SP_MAX(p.w,.001f):0);
    if(kind==SpatialWave)return SP_ABS(p.x);
    if(kind==SpatialTurbulentDisplace)return SP_ABS(p.x)*(1+SP_ABS(p.w))*1.732051f;
    return 0;
}
#ifdef __cplusplus
}
#endif
#undef SP_INLINE
#undef SP_SIN
#undef SP_COS
#undef SP_SQRT
#undef SP_ATAN2
#undef SP_ABS
#undef SP_MAX
#undef SP_CLAMP
#endif
