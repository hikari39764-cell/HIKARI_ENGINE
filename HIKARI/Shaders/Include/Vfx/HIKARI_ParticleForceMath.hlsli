#ifndef HIKARI_PARTICLE_FORCE_MATH
#define HIKARI_PARTICLE_FORCE_MATH
// 同じ解析的 curl 場を CPU 参照と GPU 粒子で使う。
// C++ 側の型・標準ライブラリは呼び出し元で読み込み、shader の依存閉包に含めない。
#ifdef __cplusplus
namespace HIKARI::RENDER3D::PARTICLES {
using float3 = MATH::Vec3;
#define PF_INLINE inline
#define PF_SIN std::sin
#define PF_COS std::cos
#define PF_LENGTH MATH::Length
#define PF_CROSS MATH::Cross
#define PF_MAX (std::max)
#define PF_CLAMP std::clamp
#else
#define PF_INLINE
#define PF_SIN sin
#define PF_COS cos
#define PF_LENGTH length
#define PF_CROSS cross
#define PF_MAX max
#define PF_CLAMP clamp
#endif
PF_INLINE float3 ParticleCurl(float3 p) {
    // A=(sin(y+cos(z)),sin(z+cos(x)),sin(x+cos(y))) の curl。
    return float3(-PF_SIN(p.y)*PF_COS(p.x+PF_COS(p.y))-PF_COS(p.z+PF_COS(p.x)),
        -PF_SIN(p.z)*PF_COS(p.y+PF_COS(p.z))-PF_COS(p.x+PF_COS(p.y)),
        -PF_SIN(p.x)*PF_COS(p.z+PF_COS(p.x))-PF_COS(p.y+PF_COS(p.z)));
}
PF_INLINE float3 ParticleForceAcceleration(float3 position, float seconds,
    float3 center, float radius, float3 axis, float vortex,
    float turbulence, float frequency, float evolution, float attraction) {
    const float3 delta=position-center;
    const float distance=PF_LENGTH(delta);
    const float falloff=radius>0 ? PF_CLAMP(1.0f-distance/radius,0.0f,1.0f) : 1.0f;
    if (falloff<=0) return float3(0,0,0);
    const float axisLength=PF_LENGTH(axis);
    axis=axisLength>0.0001f ? axis*(1.0f/axisLength) : float3(0,1,0);
    const float3 radial=delta*(1.0f/PF_MAX(distance,0.0001f));
    float3 force=PF_CROSS(axis,radial)*vortex-radial*attraction;
    if (turbulence!=0) {
        const float phase=seconds*evolution;
        const float3 p=delta*frequency+float3(phase,phase*0.731f,phase*1.173f);
        force=force+(ParticleCurl(p)+ParticleCurl(p*2.13f+float3(17.1f,9.2f,31.7f))*0.35f)*(turbulence*0.37f);
    }
    return force*(falloff*falloff);
}
#ifdef __cplusplus
}
#endif
#undef PF_INLINE
#undef PF_SIN
#undef PF_COS
#undef PF_LENGTH
#undef PF_CROSS
#undef PF_MAX
#undef PF_CLAMP
#endif
