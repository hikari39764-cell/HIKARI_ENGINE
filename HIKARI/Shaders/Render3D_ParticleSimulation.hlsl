#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"
#include "Include/Material/HIKARI_MaterialExpression.hlsli"

#include "Include/Vfx/HIKARI_ParticleData.hlsli"
#include "Include/Vfx/HIKARI_ParticleForceMath.hlsli"
#include "Include/Vfx/HIKARI_FluidGrid.hlsli"
#include "Include/Math/HIKARI_BillboardMath.hlsli"

#define HIKARI_PARTICLE_LAYOUT(name, value) static const uint name = value;
#include "Include/Vfx/HIKARI_ParticleEmitterLayout.hlsli"
#undef HIKARI_PARTICLE_LAYOUT
static const uint HIKARI_PARTICLE_RANDOM_LIFETIME = 1u;
static const uint HIKARI_PARTICLE_RANDOM_SHAPE_RADIUS = 2u;
static const uint HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_Y = 3u;
static const uint HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_ANGLE = 4u;
static const uint HIKARI_PARTICLE_RANDOM_SHAPE_OFFSET_RADIUS = 5u;
static const uint HIKARI_PARTICLE_RANDOM_RADIAL_SPEED = 6u;
static const uint HIKARI_PARTICLE_RANDOM_POSITION = 16u;
static const uint HIKARI_PARTICLE_RANDOM_ROTATION = 32u;
static const uint HIKARI_PARTICLE_RANDOM_SCALE = 48u;
static const uint HIKARI_PARTICLE_RANDOM_COLOR = 64u;
static const uint HIKARI_PARTICLE_ROTATED_BILLBOARD = 0u;
static const uint HIKARI_PARTICLE_FIXED_Y_AXIS = 1u;
static const uint HIKARI_PARTICLE_DIRECTIONAL_BILLBOARD = 2u;
static const uint HIKARI_PARTICLE_FIXED = 3u;

ByteAddressBuffer gEmitterData : register(t0);
RWStructuredBuffer<Particle> gParticles : register(u0);
// 粒子の事件（HIKARI_ParticleEmitterLayout.hlsli の EVENT_* の並び）：UpdateCS が碰撞・寿命切れを区へ書き、全ての層を
// 進めた後に EventSpawnCS がそれを読んで生成する（Niagara の GPU 事件、Unity VFX Graph の GPU Event と同じ流れ）。
RWByteAddressBuffer gEvents : register(u2);

cbuffer SimulationConstants : register(b0)
{
    uint gParticleOffset;
    uint gParticleCapacity;
    uint gEmitterIndex;
    uint gSpawnCursor;
    uint gSpawnCount;
    uint gRandomSeed;
    float gDeltaSeconds;
    uint gSimulationFlags;  // bit 0 直接の形、bit 8..10 粒子が乗る流体の域の数（kGpuParticleFluidFlowShift）
};

// 粒子が乗る流体の域（C++ の FluidRenderer::PrepareFlow と同じ並び）：世界→格子（格、角が原点）の行、格の数・大きさ h、
// 域が世界で動く速さ（局部空間の域。世界空間の域は 0）。
struct FluidFlowDomain
{
    float4 toGrid0;
    float4 toGrid1;
    float4 toGrid2;
    float4 cells;
    float4 motion;
};
cbuffer FluidFlowConstants : register(b1)
{
    FluidFlowDomain gFluidFlow[4];
};
Texture3D<float4> gFluidVelocity0 : register(t1);
Texture3D<float4> gFluidVelocity1 : register(t2);
Texture3D<float4> gFluidVelocity2 : register(t3);
Texture3D<float4> gFluidVelocity3 : register(t4);
SamplerState gFluidSampler : register(s0);

// 域 1 つの世界の点 world での流体の速度（世界、m/s）。速度体積の xyz は域の軸に沿った格の下側の面の速度（格の数 + 1 の
// 体積、交错格子）を成分ごとに半格ずらして読み、域が動く速さを足す。箱の外なら inside = 0。
float3 FluidFlowAt(FluidFlowDomain domain, Texture3D<float4> velocity, float3 world, inout float inside)
{
    const float3 grid = float3(dot(domain.toGrid0, float4(world, 1.0f)), dot(domain.toGrid1, float4(world, 1.0f)),
                               dot(domain.toGrid2, float4(world, 1.0f)));
    if (inside > 0.0f || any(grid < 0.0f) || any(grid > domain.cells.xyz))
        return 0.0f.xxx;
    inside = 1.0f;
    const float3 v = HikariFluidVelocityAt(velocity, gFluidSampler, grid, domain.cells.xyz + 1.0f);
    // 行は域の軸 / h なので、h を掛けると域の軸に戻る。
    return (domain.toGrid0.xyz * v.x + domain.toGrid1.xyz * v.y + domain.toGrid2.xyz * v.z) * domain.cells.w +
           domain.motion.xyz;
}

// 世界の点 world の流体の速度（入っている最初の域）。どの域にも入っていなければ inside = 0。
float3 FluidFlowVelocity(float3 world, uint count, out float inside)
{
    inside = 0.0f;
    float3 flow = 0.0f.xxx;
    if (count > 0u)
        flow += FluidFlowAt(gFluidFlow[0], gFluidVelocity0, world, inside);
    if (count > 1u)
        flow += FluidFlowAt(gFluidFlow[1], gFluidVelocity1, world, inside);
    if (count > 2u)
        flow += FluidFlowAt(gFluidFlow[2], gFluidVelocity2, world, inside);
    if (count > 3u)
        flow += FluidFlowAt(gFluidFlow[3], gFluidVelocity3, world, inside);
    return flow;
}

// ---------------------------------------------------------------- 場景深度との碰撞
// Unity VFX Graph の Collide with Depth Buffer、Niagara の Scene Depth 碰撞と同じ考え方：粒子の模拟は描画より先に走るので、
// 上一帧の主視图の深度（半解像度、2×2 の最も近い面。CollisionDepthCS が作る）と、その視图の行列を使う。
Texture2D<float> gCollisionDepth : register(t5);  // CollisionDepthCS では場景の深度（全解像度）
RWTexture2D<float> gCollisionDepthOut : register(u1);
cbuffer CollisionViewConstants : register(b2)
{
    float4x4 gCollisionViewProj;
    float4x4 gCollisionInvViewProj;
    float4 gCollisionSize;    // xy 碰撞深度の大きさ（texel）、zw その逆数
    float4 gCollisionCamera;  // xyz 視点、w 有効（1）
};

// 主視图の深度を半解像度へ：2×2 の最も近い面（深度は 0 が近い）。
[numthreads(8, 8, 1)]
void CollisionDepthCS(uint3 id : SV_DispatchThreadID)
{
    uint width, height, sourceWidth, sourceHeight;
    gCollisionDepthOut.GetDimensions(width, height);
    gCollisionDepth.GetDimensions(sourceWidth, sourceHeight);
    if (id.x >= width || id.y >= height)
        return;
    const int2 limit = int2(sourceWidth, sourceHeight) - 1;
    float nearest = 1.0f;
    [unroll] for (uint corner = 0u; corner < 4u; ++corner)
        nearest = min(nearest, gCollisionDepth.Load(int3(min(int2(id.xy) * 2 + int2(corner & 1u, corner >> 1u), limit), 0)));
    gCollisionDepthOut[id.xy] = nearest;
}

// 碰撞深度の texel の面の位置（相機を原点にした座標）。空（深度 1）なら valid = false。
float3 CollisionSurface(int2 texel, out bool valid)
{
    const int2 clamped = clamp(texel, int2(0, 0), int2(gCollisionSize.xy) - 1);
    const float depth = gCollisionDepth.Load(int3(clamped, 0));
    valid = depth > 0.0f && depth < 0.99999f;
    const float2 uv = (float2(clamped) + 0.5f) * gCollisionSize.zw;
    const float4 world = mul(gCollisionInvViewProj, float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f));
    return world.xyz / world.w;
}

// 粒子を上一帧の視图へ投影し、面より奥で thickness 以内なら面の上へ戻して面の法線で跳ね返す。法線は隣の texel の世界位置
// （左右・上下のうち深度の差が小さい側。物体の縁で奥の面を拾わない）の外積。戻り値は碰撞したか、impactSpeed は面へ
// 向かっていた速さ。
bool CollideSceneDepth(inout float3 position, inout float3 velocity, float thickness, float restitution, float friction,
                       out float impactSpeed)
{
    impactSpeed = 0.0f;
    if (gCollisionCamera.w <= 0.0f)
        return false;
    // 相機を原点にして投影・還元する（大きな座標の場面でも面の位置と法線が揺れない。流体の視線と同じ）。
    float3 relative = position - gCollisionCamera.xyz;
    const float4 clip = mul(gCollisionViewProj, float4(relative, 1.0f));
    if (clip.w <= 1.0e-4f)
        return false;
    const float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) >= 1.0f) || ndc.z <= 0.0f || ndc.z >= 1.0f)
        return false;
    const int2 texel = int2(float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * gCollisionSize.xy);
    bool valid;
    const float3 surface = CollisionSurface(texel, valid);
    if (!valid)
        return false;
    const float behind = length(relative) - length(surface);
    if (behind <= 0.0f || behind > thickness)
        return false;
    bool left, right, up, down;
    const float3 xMinus = CollisionSurface(texel - int2(1, 0), left);
    const float3 xPlus = CollisionSurface(texel + int2(1, 0), right);
    const float3 yMinus = CollisionSurface(texel - int2(0, 1), up);
    const float3 yPlus = CollisionSurface(texel + int2(0, 1), down);
    const float3 dx = right && (!left || distance(xPlus, surface) < distance(xMinus, surface)) ? xPlus - surface
                                                                                              : surface - xMinus;
    const float3 dy = down && (!up || distance(yPlus, surface) < distance(yMinus, surface)) ? yPlus - surface
                                                                                           : surface - yMinus;
    float3 normal = cross(dy, dx);
    if (dot(normal, normal) <= 1.0e-12f)
        return false;
    normal = normalize(normal);
    if (dot(normal, -surface) < 0.0f)
        normal = -normal;
    // 面の平面の上（法線側へ 1 cm）へ戻し、面へ向かう速さを restitution で反転、面に沿う速さを friction で減らす。視線では
    // 奥でも面の平面の表側にいる（面をかすめる角度）なら碰撞しない。
    const float signedDistance = dot(relative - surface, normal);
    if (signedDistance >= 0.0f)
        return false;
    relative -= normal * (signedDistance - 0.01f);
    position = gCollisionCamera.xyz + relative;
    const float into = dot(velocity, normal);
    impactSpeed = max(-into, 0.0f);
    const float3 along = velocity - normal * into;
    velocity = along * (1.0f - saturate(friction)) + normal * (into < 0.0f ? -into * saturate(restitution) : into);
    return true;
}

#include "Include/Vfx/HIKARI_ParticleEmitterEvaluation.hlsli"

// 局部模拟の粒子の速度を世界へ（事件は世界の位置と速度で書く）。
float3 ParticleWorldVelocity(uint emitterIndex, float3 velocity)
{
    if ((asuint(LoadEmitterValue(emitterIndex, 14u).w) & 1u) == 0u)
        return velocity;
    return RotateByQuaternion(velocity * LoadEmitterValue(emitterIndex, 2u).xyz, LoadEmitterValue(emitterIndex, 1u));
}

// この層が書く区へ事件を足す（区が満ちたら捨てる）。kind 0 碰撞（impactSpeed は面へ向かっていた速さ）、1 寿命切れ。
void AppendParticleEvent(uint kind, float3 position, float3 velocity, float impactSpeed)
{
    const float4 events = LoadEmitterValue(gEmitterIndex, HIKARI_PARTICLE_EVENTS);
    const uint region = asuint(events.x);
    if (region == 0u || (asuint(events.y) & (1u << kind)) == 0u || (kind == 0u && impactSpeed < events.z))
        return;
    const uint regionBase = (region - 1u) * HIKARI_PARTICLE_EVENT_REGION_BYTES;
    uint index;
    gEvents.InterlockedAdd(regionBase, 1u, index);
    if (index >= HIKARI_PARTICLE_EVENTS_PER_REGION)
        return;
    const uint at = regionBase + 16u + index * 32u;
    gEvents.Store4(at, uint4(asuint(position), kind));
    gEvents.Store4(at + 16u, uint4(asuint(velocity), asuint(impactSpeed)));
}

// フレームの初めに全ての区の数を 0 にする（どの層も進める前に一度）。
[numthreads(64, 1, 1)]
void EventClearCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x < HIKARI_PARTICLE_EVENT_REGIONS)
        gEvents.Store(dispatchThreadId.x * HIKARI_PARTICLE_EVENT_REGION_BYTES, 0u);
}

[numthreads(64, 1, 1)]
void ResetCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gParticleCapacity)
    {
        return;
    }
    Particle particle = (Particle)0;
    if ((gSimulationFlags & 1u) != 0u && dispatchThreadId.x < asuint(LoadEmitterValue(gEmitterIndex, 0u).x))
    {
        const uint base = 1u + dispatchThreadId.x * 9u;
        particle.positionAge = LoadEmitterValue(gEmitterIndex, base);
        particle.orientation = LoadEmitterValue(gEmitterIndex, base+1u);
        const float4 scale = LoadEmitterValue(gEmitterIndex, base+2u);
        particle.scaleRotation = float4(abs(scale.xy), 0.0f, abs(scale.z));
        particle.color = LoadEmitterValue(gEmitterIndex, base+3u);
        particle.velocityLifetime = float4(LoadEmitterValue(gEmitterIndex, base+4u).xyz, max(scale.w, 0.0001f));
        particle.physicsPositionSeed = LoadEmitterValue(gEmitterIndex, base+5u);
        particle.birthRotation = LoadEmitterValue(gEmitterIndex, base+6u);
        particle.birthScale = LoadEmitterValue(gEmitterIndex, base+7u);
        const float2 uvRange = LoadEmitterValue(gEmitterIndex, base+8u).xy;
        particle.scaleRotation.z = uvRange.x;
        particle.birthRotation.w = uvRange.y;
    }
    gParticles[gParticleOffset + dispatchThreadId.x] = particle;
}

[numthreads(64, 1, 1)]
void UpdateCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gParticleCapacity)
    {
        return;
    }
    uint particleIndex = gParticleOffset + dispatchThreadId.x;
    Particle particle = gParticles[particleIndex];
    if (particle.velocityLifetime.w <= 0.0f)
    {
        return;
    }

    const float dt = isfinite(gDeltaSeconds) ? clamp(gDeltaSeconds, 0.0f, 2.0f) : 0.0f;
    particle.positionAge.w += dt;
    if (particle.positionAge.w >= particle.velocityLifetime.w)
    {
        AppendParticleEvent(1u, particle.positionAge.xyz, ParticleWorldVelocity(gEmitterIndex,
                            particle.velocityLifetime.xyz), 0.0f);
        particle.velocityLifetime.w = 0.0f;
        particle.color.a = 0.0f;
        gParticles[particleIndex] = particle;
        return;
    }

    float4 forces = LoadEmitterValue(gEmitterIndex, 5u);
    float3 gravity = forces.xyz *
        LoadEmitterValue(gEmitterIndex, 10u).z;
    float drag = max(forces.w, 0.0f);
    const float4 collision = LoadEmitterValue(gEmitterIndex,14u);
    const bool collide = (asuint(collision.w)&2u)!=0u;
    const bool depthCollide = (asuint(collision.w) & 4u) != 0u;
    // CPU 参照と同じ解析積分。衝突時のみ分割し、GPU 提出数には影響しない。
    const float4 fieldCenter=LoadEmitterValue(gEmitterIndex,HIKARI_PARTICLE_FIELD_CENTER);
    const float4 fieldAxis=LoadEmitterValue(gEmitterIndex,HIKARI_PARTICLE_FIELD_AXIS);
    const float4 fieldFlow=LoadEmitterValue(gEmitterIndex,HIKARI_PARTICLE_FIELD_FLOW);
    const float4 fieldTime = LoadEmitterValue(gEmitterIndex, HIKARI_PARTICLE_FIELD_TIME);
    const float fieldSeconds = fieldTime.x;
    const bool fieldActive=fieldAxis.w!=0 || fieldFlow.x!=0 || fieldFlow.w!=0;
    // 流体に乗る（世界空間の粒子だけ）：毎秒 follow の割合で速度をその場の流体の速度へ寄せる。
    const float follow = fieldTime.y;
    const uint flowCount = (gSimulationFlags >> 8u) & 7u;
    const bool followFluid = follow > 0.0f && flowCount > 0u && (asuint(collision.w) & 1u) == 0u;
    const uint steps = fieldActive || followFluid ? (uint)clamp(ceil(dt*60.0f),1.0f,120.0f)
        : collide || depthCollide ? (uint)clamp(ceil(dt * 30.0f), 1.0f, 8.0f) : 1u;
    const float step = dt / steps;
    const float x = drag * step;
    const float damping = exp(-x);
    const float velocityIntegral = x < 1.0e-3f
        ? step * (1.0f - x * 0.5f + x*x / 6.0f) : (1.0f - damping) / drag;
    const float accelerationIntegral = x < 1.0e-3f
        ? step*step * (0.5f - x / 6.0f + x*x / 24.0f) : (step - velocityIntegral) / drag;
    // このフレームで一番強く当たった碰撞（面へ向かっていた速さ）と、その直後の速度。
    float impact = -1.0f;
    float3 impactVelocity = 0.0f;
    for (uint index=0u; index<steps; ++index)
    {
        float3 acceleration=gravity;
        if (fieldActive) acceleration+=ParticleForceAcceleration(particle.physicsPositionSeed.xyz,
            fieldSeconds-dt+(index+0.5f)*step,fieldCenter.xyz,fieldCenter.w,fieldAxis.xyz,fieldAxis.w,
            fieldFlow.x,fieldFlow.y,fieldFlow.z,fieldFlow.w);
        if (followFluid)
        {
            float inside;
            const float3 flow = FluidFlowVelocity(particle.physicsPositionSeed.xyz, flowCount, inside);
            acceleration += (flow - particle.velocityLifetime.xyz) * (follow * inside);
        }
        particle.physicsPositionSeed.xyz += particle.velocityLifetime.xyz * velocityIntegral + acceleration * accelerationIntegral;
        particle.velocityLifetime.xyz = particle.velocityLifetime.xyz * damping + acceleration * velocityIntegral;
        if (collide && particle.physicsPositionSeed.y < collision.x)
        {
            const float into = max(-particle.velocityLifetime.y, 0.0f);
            particle.physicsPositionSeed.y = collision.x;
            if (particle.velocityLifetime.y < 0.0f)
                particle.velocityLifetime.y *= -saturate(collision.y);
            particle.velocityLifetime.xz *= 1.0f-saturate(collision.z);
            if (into > impact)
            {
                impact = into;
                impactVelocity = particle.velocityLifetime.xyz;
            }
        }
        if (depthCollide)
        {
            float3 velocity = particle.velocityLifetime.xyz;
            float into;
            if (CollideSceneDepth(particle.physicsPositionSeed.xyz, velocity, fieldTime.z, collision.y, collision.z,
                                  into))
            {
                particle.velocityLifetime.xyz = velocity;
                if (into > impact)
                {
                    impact = into;
                    impactVelocity = velocity;
                }
            }
        }
    }
    EvaluateParticlePresentation(gEmitterIndex, particle);
    if (impact >= 0.0f)
        AppendParticleEvent(0u, particle.positionAge.xyz, ParticleWorldVelocity(gEmitterIndex, impactVelocity), impact);
    gParticles[particleIndex] = particle;
}

// 発射器の形と分布から粒子を一つ作る。fromEvent の時は形の中心を事件の位置に置き、事件の速度を EVENT_SPAWN.x の割合で
// 受け継ぐ（局部模拟の層は発射器の変換を戻した位置と速度で持つ）。
Particle MakeSpawnedParticle(uint particleSeed, bool fromEvent, float3 eventPosition, float3 eventVelocity)
{
    float3 localPosition = 0.0f;
    float3 localDirection = float3(0.0f, 1.0f, 0.0f);
    float4 shapeParameters = LoadEmitterValue(gEmitterIndex, 4u);
    float4 emitterOrigin = LoadEmitterValue(gEmitterIndex, 0u);
    float4 emitterScale = LoadEmitterValue(gEmitterIndex, 2u);
    uint shape = asuint(shapeParameters.x);
    float shapeRadius = lerp(
        emitterOrigin.w,
        emitterScale.w,
        asuint(shapeParameters.z) == 1u
            ? RandomForProperty(
                particleSeed,
                asuint(shapeParameters.w),
                HIKARI_PARTICLE_RANDOM_SHAPE_RADIUS)
            : 0.0f);
    shapeRadius = max(shapeRadius, 0.0f);
    if (shape == 1u)
    {
        float y = lerp(
            -1.0f,
            1.0f,
            RandomForProperty(
                particleSeed,
                0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_Y));
        float angle = RandomForProperty(
            particleSeed,
            0u,
            HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_ANGLE) *
            6.28318530718f;
        float radius = pow(RandomForProperty(
            particleSeed,
            0u,
            HIKARI_PARTICLE_RANDOM_SHAPE_OFFSET_RADIUS),
            1.0f / 3.0f) *
            shapeRadius;
        float radial = sqrt(max(0.0f, 1.0f - y * y));
        localDirection = float3(
            radial * cos(angle),
            y,
            radial * sin(angle));
        localPosition = localDirection * radius;
    }
    else if (shape == 2u)
    {
        float halfAngle = clamp(
            shapeParameters.y * 0.5f,
            0.0f,
            3.14159265359f);
        float y = lerp(
            cos(halfAngle),
            1.0f,
            RandomForProperty(
                particleSeed,
                0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_Y));
        float angle = RandomForProperty(
            particleSeed,
            0u,
            HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_ANGLE) *
            6.28318530718f;
        float radial = sqrt(max(0.0f, 1.0f - y * y));
        localDirection = float3(
            radial * cos(angle),
            y,
            radial * sin(angle));
        float distance = pow(RandomForProperty(
            particleSeed,
            0u,
            HIKARI_PARTICLE_RANDOM_SHAPE_OFFSET_RADIUS),
            1.0f / 3.0f) * shapeRadius;
        localPosition = localDirection * distance;
    }

    else if (shape == 3u)
    {
        float4 shapeBox = LoadEmitterValue(gEmitterIndex, 12u);
        localPosition = float3(
            lerp(-shapeBox.x, shapeBox.x, RandomForProperty(
                particleSeed, 0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_Y)),
            lerp(-shapeBox.y, shapeBox.y, RandomForProperty(
                particleSeed, 0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_ANGLE)),
            lerp(-shapeBox.z, shapeBox.z, RandomForProperty(
                particleSeed, 0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_OFFSET_RADIUS)));
        localDirection = float3(0.0f, 1.0f, 0.0f);
    }
    else if (shape == 4u)
    {
        float4 shapeBox = LoadEmitterValue(gEmitterIndex, 12u);
        float angle = RandomForProperty(
            particleSeed, 0u,
            HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_ANGLE) *
            6.28318530718f;
        float distance = sqrt(RandomForProperty(
            particleSeed, 0u,
            HIKARI_PARTICLE_RANDOM_SHAPE_OFFSET_RADIUS)) * shapeRadius;
        float y = lerp(
            -shapeBox.w * 0.5f,
            shapeBox.w * 0.5f,
            RandomForProperty(
                particleSeed, 0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_Y));
        localPosition = float3(
            cos(angle) * distance, y, sin(angle) * distance);
        localDirection = float3(0.0f, 1.0f, 0.0f);
    }
    else if (shape == 5u)
    {
        float4 shapeCircle = LoadEmitterValue(gEmitterIndex, 13u);
        // 放出角度範囲。0 以下 (古い資産) は従来どおり全周。
        float arcDegrees = shapeCircle.w > 0.0f ? min(shapeCircle.w, 360.0f)
                                                : 360.0f;
        float angle = RandomForProperty(
            particleSeed, 0u,
            HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_ANGLE) *
            radians(arcDegrees);
        float innerRadius = clamp(shapeCircle.x, 0.0f, shapeRadius);
        float distance = shapeRadius;
        if (asuint(shapeCircle.y) == 0u && shapeRadius > innerRadius)
        {
            float u = RandomForProperty(
                particleSeed, 0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_OFFSET_RADIUS);
            distance = sqrt(lerp(
                innerRadius * innerRadius,
                shapeRadius * shapeRadius,
                u));
        }
        localPosition = float3(
            cos(angle) * distance, 0.0f, sin(angle) * distance);
        localDirection = float3(0.0f, 1.0f, 0.0f);
    }
    else if (shape == 6u)
    {
        float4 shapeCircle = LoadEmitterValue(gEmitterIndex, 13u);
        float halfLength = shapeCircle.z * 0.5f;
        localPosition = float3(
            lerp(-halfLength, halfLength, RandomForProperty(
                particleSeed, 0u,
                HIKARI_PARTICLE_RANDOM_SHAPE_DIRECTION_Y)),
            0.0f,
            0.0f);
        localDirection = float3(0.0f, 1.0f, 0.0f);
    }

    float4 rotation = LoadEmitterValue(gEmitterIndex, 1u);
    float3 simulationPosition = localPosition;
    localPosition *= emitterScale.xyz;
    float3 worldPosition = (fromEvent ? eventPosition : emitterOrigin.xyz) +
        RotateByQuaternion(localPosition, rotation);
    const float4 inverseRotation = float4(-rotation.xyz, rotation.w);
    const float3 safeScale = lerp(1.0f, emitterScale.xyz, (float3)(abs(emitterScale.xyz) > 1.0e-6f));
    if (fromEvent)
        simulationPosition = RotateByQuaternion(worldPosition - emitterOrigin.xyz, inverseRotation) / safeScale;
    float3 worldDirection = normalize(
        RotateByQuaternion(localDirection, rotation));
    float4 lifetimeAndSpeed = LoadEmitterValue(gEmitterIndex, 3u);
    float4 distributionMetadata = LoadEmitterValue(gEmitterIndex, 11u);
    float lifetime = lerp(
        lifetimeAndSpeed.x,
        lifetimeAndSpeed.y,
        asuint(distributionMetadata.x) == 1u
            ? RandomForProperty(
                particleSeed,
                asuint(distributionMetadata.y),
                HIKARI_PARTICLE_RANDOM_LIFETIME)
            : 0.0f);
    float speed = lerp(
        lifetimeAndSpeed.z,
        lifetimeAndSpeed.w,
        asuint(distributionMetadata.z) == 1u
            ? RandomForProperty(
                particleSeed,
                asuint(distributionMetadata.w),
                HIKARI_PARTICLE_RANDOM_RADIAL_SPEED)
            : 0.0f) * LoadEmitterValue(gEmitterIndex, 10u).x;

    Particle particle = (Particle)0;
    const bool localSimulation = (asuint(LoadEmitterValue(gEmitterIndex,14u).w)&1u)!=0u;
    float3 velocity = (localSimulation ? localDirection : worldDirection) * speed;
    if (fromEvent)
    {
        const float3 inherited = eventVelocity * LoadEmitterValue(gEmitterIndex, HIKARI_PARTICLE_EVENT_SPAWN).x;
        velocity += localSimulation ? RotateByQuaternion(inherited, inverseRotation) / safeScale : inherited;
    }
    particle.positionAge = float4(worldPosition,0.0f);
    particle.velocityLifetime = float4(velocity,max(lifetime,0.0001f));
    particle.physicsPositionSeed = float4(localSimulation ? simulationPosition : worldPosition,asfloat(particleSeed));
    particle.birthRotation = rotation;
    particle.birthScale = emitterScale;
    EvaluateParticlePresentation(gEmitterIndex, particle);
    return particle;
}

[numthreads(64, 1, 1)]
void SpawnCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gSpawnCount || gParticleCapacity == 0u ||
        asuint(LoadEmitterValue(gEmitterIndex, 10u).w) == 0u)
        return;
    const uint particleSlot = (gSpawnCursor + dispatchThreadId.x) % gParticleCapacity;
    const uint particleSeed = Hash(gRandomSeed ^ (particleSlot * 747796405u));
    gParticles[gParticleOffset + particleSlot] = MakeSpawnedParticle(particleSeed, false, 0.0f, 0.0f);
}

// 元の層の区の事件を読み、一つの事件あたり EVENT_SPAWN.z >> 8 個を生成する（線程は 事件 × 個数）。槽はこの層の GPU の
// 游標を原子的に進めて取る（同じフレームの事件の数は CPU に分からないため）。
[numthreads(64, 1, 1)]
void EventSpawnCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (gParticleCapacity == 0u || asuint(LoadEmitterValue(gEmitterIndex, 10u).w) == 0u)
        return;
    const uint region = asuint(LoadEmitterValue(gEmitterIndex, HIKARI_PARTICLE_EVENTS).w);
    const float4 spawn = LoadEmitterValue(gEmitterIndex, HIKARI_PARTICLE_EVENT_SPAWN);
    const uint kind = asuint(spawn.z) & 255u;
    const uint perEvent = max(asuint(spawn.z) >> 8u, 1u);
    const uint eventIndex = dispatchThreadId.x / perEvent;
    if (region == 0u || eventIndex >= HIKARI_PARTICLE_EVENTS_PER_REGION)
        return;
    const uint regionBase = (region - 1u) * HIKARI_PARTICLE_EVENT_REGION_BYTES;
    if (eventIndex >= min(gEvents.Load(regionBase), HIKARI_PARTICLE_EVENTS_PER_REGION))
        return;
    const uint at = regionBase + 16u + eventIndex * 32u;
    const uint4 positionKind = gEvents.Load4(at);
    const float4 velocityImpact = asfloat(gEvents.Load4(at + 16u));
    if (positionKind.w != kind || (kind == 0u && velocityImpact.w < spawn.y))
        return;
    uint cursor;
    gEvents.InterlockedAdd(HIKARI_PARTICLE_EVENT_CURSOR_OFFSET + asuint(spawn.w) * 4u, 1u, cursor);
    const uint particleSlot = cursor % gParticleCapacity;
    const uint particleSeed = Hash(gRandomSeed ^ (particleSlot * 747796405u) ^ (dispatchThreadId.x * 2891336453u));
    gParticles[gParticleOffset + particleSlot] =
        MakeSpawnedParticle(particleSeed, true, asfloat(positionKind.xyz), velocityImpact.xyz);
}
