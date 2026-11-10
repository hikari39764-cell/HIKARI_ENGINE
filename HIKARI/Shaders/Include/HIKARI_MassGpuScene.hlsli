#ifndef HIKARI_MASS_GPU_SCENE_INCLUDED
#define HIKARI_MASS_GPU_SCENE_INCLUDED

static const uint HIKARI_MASS_GPU_SCENE_VERSION = 1u;
static const uint HIKARI_MASS_GPU_SCENE_INDEX_BIT = 0x80000000u;
static const uint HIKARI_MASS_GPU_SCENE_INDEX_MASK = 0x7fffffffu;
// objectIdHigh の最上位が立つ個体は実例集の個体（物体ではない）。bit 16..23 が描く最小の大きさ、bit 8..15 が色の揺らぎ、
// bit 0..7 が風の強さ、objectIdLow が種。HIKARI_WorldInstanceStore.cpp の詰め方と揃える。
static const uint HIKARI_MASS_INSTANCE_IDENTITY_BIT = 0x80000000u;
static const float HIKARI_MASS_INSTANCE_WIND_METERS = 0.6f;
static const float HIKARI_MASS_INSTANCE_MIN_SCREEN_STEP = 0.0001f;

struct HikariMassRenderProxyGpuRecord
{
    float3 position;
    uint objectIdLow;
    float4 rotation;
    float3 scale;
    uint objectIdHigh;
    float4 boundsCenterRadius;

    uint sourceRecordIndex;
    uint flags;
    uint materialDataIndex;
    uint clusterGeometrySrvDescriptorIndex;

    uint clusterGeometryMetadataSrvDescriptorIndex;
    uint clusterSurfaceIndex;
    uint clusterRangeIndex;
    uint clusterRangeCount;

    uint clusterIndexCount;
    uint resourceFlags;
    uint geometryBackend;
    uint clusterLodRangeIndex;

    uint clusterLodRangeCount;
    uint clusterSelectedLodIndex;
    uint clusterLodFlags;
    uint reserved;
};

StructuredBuffer<HikariMassRenderProxyGpuRecord>
    gMassRenderProxyBuffer : register(t0, space8);

bool HikariIsMassGpuSceneIndex(uint absoluteIndex)
{
    return (absoluteIndex & HIKARI_MASS_GPU_SCENE_INDEX_BIT) != 0u;
}

uint HikariDecodeMassGpuSceneIndex(uint absoluteIndex)
{
    return absoluteIndex & HIKARI_MASS_GPU_SCENE_INDEX_MASK;
}

float4 HikariNormalizeMassRotation(float4 rotation)
{
    const float lengthSquared = dot(rotation, rotation);
    return isfinite(lengthSquared) && lengthSquared > 1.0e-12f
        ? rotation * rsqrt(lengthSquared)
        : float4(0.0f, 0.0f, 0.0f, 1.0f);
}

void HikariBuildMassMatrices(
    HikariMassRenderProxyGpuRecord record,
    out float4x4 world,
    out float4x4 normalMatrix)
{
    const float4 q = HikariNormalizeMassRotation(record.rotation);
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;

    const float3 safeScale = max(abs(record.scale), 1.0e-6f.xxx);
    const float r00 = 1.0f - 2.0f * (yy + zz);
    const float r01 = 2.0f * (xy - wz);
    const float r02 = 2.0f * (xz + wy);
    const float r10 = 2.0f * (xy + wz);
    const float r11 = 1.0f - 2.0f * (xx + zz);
    const float r12 = 2.0f * (yz - wx);
    const float r20 = 2.0f * (xz - wy);
    const float r21 = 2.0f * (yz + wx);
    const float r22 = 1.0f - 2.0f * (xx + yy);

    world = float4x4(
        r00 * record.scale.x, r01 * record.scale.y,
            r02 * record.scale.z, record.position.x,
        r10 * record.scale.x, r11 * record.scale.y,
            r12 * record.scale.z, record.position.y,
        r20 * record.scale.x, r21 * record.scale.y,
            r22 * record.scale.z, record.position.z,
        0.0f, 0.0f, 0.0f, 1.0f);
    normalMatrix = float4x4(
        r00 / safeScale.x, r01 / safeScale.y,
            r02 / safeScale.z, 0.0f,
        r10 / safeScale.x, r11 / safeScale.y,
            r12 / safeScale.z, 0.0f,
        r20 / safeScale.x, r21 / safeScale.y,
            r22 / safeScale.z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
}

HikariSurfaceGpuSceneInstance HikariBuildMassSurfaceGpuSceneInstance(
    HikariMassRenderProxyGpuRecord record)
{
    HikariSurfaceGpuSceneInstance instance =
        (HikariSurfaceGpuSceneInstance)0;
    HikariBuildMassMatrices(
        record,
        instance.world,
        instance.normalMatrix);
    instance.clusterWorld = instance.world;
    instance.clusterNormalMatrix = instance.normalMatrix;
    const float4 localBounds = record.boundsCenterRadius;
    const float4 worldCenter = mul(
        instance.world,
        float4(localBounds.xyz, 1.0f));
    instance.boundsCenterRadius = float4(
        worldCenter.xyz,
        localBounds.w * max(
            abs(record.scale.x),
            max(abs(record.scale.y), abs(record.scale.z))));

    instance.sourceRecordIndex = record.sourceRecordIndex;
    instance.objectIdLow = record.objectIdLow;
    instance.objectIdHigh = record.objectIdHigh;
    instance.flags = record.flags;
    instance.materialDataIndex = record.materialDataIndex;
    instance.clusterRangeIndex = record.clusterRangeIndex;
    instance.clusterRangeCount = record.clusterRangeCount;
    instance.clusterGeometryMetadataSrvDescriptorIndex =
        record.clusterGeometryMetadataSrvDescriptorIndex;
    instance.resourceFlags = record.resourceFlags;
    instance.geometryBackend = record.geometryBackend;
    instance.clusterGeometrySrvDescriptorIndex =
        record.clusterGeometrySrvDescriptorIndex;
    instance.clusterSurfaceIndex = record.clusterSurfaceIndex;
    instance.clusterIndexCount = record.clusterIndexCount;
    instance.clusterLodRangeIndex = record.clusterLodRangeIndex;
    instance.clusterLodRangeCount = record.clusterLodRangeCount;
    instance.clusterSelectedLodIndex = record.clusterSelectedLodIndex;
    instance.clusterLodFlags = record.clusterLodFlags;
    instance.lightmapTextureDescriptorIndex = 0xffffffffu;
    instance.lightmapUvScaleBias = float4(1.0f, 1.0f, 0.0f, 0.0f);
    if ((record.objectIdHigh & HIKARI_MASS_INSTANCE_IDENTITY_BIT) != 0u)
    {
        const float wind = (float)(record.objectIdHigh & 255u) / 255.0f;
        if (wind > 0.0f)
        {
            instance.flags |= HIKARI_SURFACE_GPU_SCENE_FLAG_FOLIAGE_WIND;
            // w は根元から梢までの高さ（風の重みを高さで読む）。
            instance.fxUser[0] = float4(wind * HIKARI_MASS_INSTANCE_WIND_METERS, 1.3f, 0.2f,
                localBounds.w * 2.0f * abs(record.scale.y));
        }
        const float tintAmount = (float)((record.objectIdHigh >> 8u) & 255u) / 255.0f;
        const uint hash = record.objectIdLow * 2654435761u;
        instance.fxUser[1].x = ((float)((hash >> 16u) & 1023u) / 511.5f - 1.0f) * tintAmount;
    }
    return instance;
}

HikariSurfaceGpuSceneInstance HikariGetMassSurfaceGpuSceneInstanceAt(
    uint absoluteIndex)
{
    return HikariBuildMassSurfaceGpuSceneInstance(
        gMassRenderProxyBuffer[
            HikariDecodeMassGpuSceneIndex(absoluteIndex)]);
}

#endif
