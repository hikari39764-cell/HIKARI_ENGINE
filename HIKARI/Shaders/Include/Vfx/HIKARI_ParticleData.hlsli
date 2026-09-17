#ifndef HIKARI_PARTICLE_DATA_INCLUDED
#define HIKARI_PARTICLE_DATA_INCLUDED
// Matches INTERNAL::GpuParticle. Birth transform is immutable for world-space particles.
struct Particle
{
    float4 positionAge;
    float4 velocityLifetime;
    float4 physicsPositionSeed;
    float4 scaleRotation;
    float4 orientation;
    float4 color;
    float4 birthRotation;
    float4 birthScale;
};
#endif
