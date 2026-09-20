// 同一の空間写像を頂点・法線・保守的な境界に適用する。
#include "HIKARI_ParticleSpatialMath.hlsli"
float3 ParticleSpatialScale(float3 scale) {
    return float3(scale.x<0?-1:1,scale.y<0?-1:1,scale.z<0?-1:1)*max(abs(scale),.0001f.xxx);
}
float3 ParticleSpatialPoint(float3 position, GpuParticleSpatialEffect effect) {
    float3 scale=ParticleSpatialScale(effect.scale.xyz);
    float3 local=RotateByQuaternion(position-effect.origin.xyz, float4(-effect.rotation.xyz,effect.rotation.w))/scale;
    float3 deformed=ParticleDeformLocal((uint)effect.metadata.x,local,effect.parameters,effect.parameters2);
    float weight=saturate(effect.metadata.y)*ParticleSpatialMask((uint)effect.metadata.z,effect.metadata.w>.5,local,effect.maskParameters);
    return effect.origin.xyz+RotateByQuaternion(lerp(local,deformed,weight)*scale,effect.rotation);
}
void ParticleSpatialVertex(inout float3 position, inout float3 normal, inout float3 tangent) {
    if (gShapeState.spatialCount==0) return;
    float epsilon=.001f;
    float3 tangentPoint=position+NormalizeOr(tangent,float3(1,0,0))*epsilon;
    float3 bitangentPoint=position+NormalizeOr(cross(normal,tangent),float3(0,1,0))*epsilon;
    [loop] for(uint i=0;i<gShapeState.spatialCount;++i) {
        GpuParticleSpatialEffect effect=gShapeState.spatialEffects[i];
        position=ParticleSpatialPoint(position,effect);
        tangentPoint=ParticleSpatialPoint(tangentPoint,effect);
        bitangentPoint=ParticleSpatialPoint(bitangentPoint,effect);
    }
    tangent=NormalizeOr(tangentPoint-position,tangent);
    normal=NormalizeOr(cross(tangent,bitangentPoint-position),normal);
}
void ParticleSpatialBounds(inout float3 center, inout float radius) {
    [loop] for(uint i=0;i<gShapeState.spatialCount;++i) {
        GpuParticleSpatialEffect effect=gShapeState.spatialEffects[i];
        float3 scale=abs(ParticleSpatialScale(effect.scale.xyz));
        float3 local=RotateByQuaternion(center-effect.origin.xyz,float4(-effect.rotation.xyz,effect.rotation.w))/scale;
        float localRadius=length(local)+radius/min(scale.x,min(scale.y,scale.z));
        radius=(localRadius+ParticleSpatialBoundExpansion((uint)effect.metadata.x,effect.parameters,effect.parameters2))*max(scale.x,max(scale.y,scale.z));
        center=effect.origin.xyz;
    }
}
void ParticleSpatialBasis(Particle particle, inout float3 right, inout float3 up, inout float3 normal) {
    // 変形平面は粒子の姿勢に固定。カメラを動かしても空間写像の入力を変えない。
    if (gShapeState.spatialCount==0) return;
    float4 orientation=NormalizeQuaternion(particle.orientation);
    right=RotateByQuaternion(float3(1,0,0),orientation);
    up=RotateByQuaternion(float3(0,1,0),orientation);
    normal=RotateByQuaternion(float3(0,0,1),orientation);
}
