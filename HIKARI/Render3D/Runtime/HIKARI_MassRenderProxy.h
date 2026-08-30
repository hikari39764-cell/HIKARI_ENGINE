#pragma once

#include <cstddef>
#include <cstdint>

namespace HIKARI::RENDER3D::RUNTIME {

    inline constexpr uint32_t kMassRenderProxyGpuVersion = 1u;
    inline constexpr uint32_t kMassRenderProxyIndexBit = 0x80000000u;
    inline constexpr uint32_t kMassRenderProxyIndexMask = 0x7fffffffu;
    inline constexpr uint32_t kMassRenderProxyMaximumInstanceCount =
        kMassRenderProxyIndexMask;
    inline constexpr uint32_t kInvalidMassRenderProxyIndex = 0xffffffffu;

    struct MassRenderFloat3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct MassRenderFloat4 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;
    };

    struct MassRenderTransform {
        MassRenderFloat3 position{};
        uint32_t objectIdLow = 0u;
        MassRenderFloat4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
        MassRenderFloat3 scale{ 1.0f, 1.0f, 1.0f };
        uint32_t objectIdHigh = 0u;
    };

    struct MassRenderDirtyRange {
        uint32_t firstInstance = 0u;
        uint32_t instanceCount = 0u;
    };

    // Compact immutable-per-frame record consumed by the existing Cluster
    // culling and Mesh Shader backend. It intentionally excludes skinning,
    // per-instance MaterialFX, water/sand deformation and raw API handles.
    struct MassRenderProxyGpuRecord {
        MassRenderFloat3 position{};
        uint32_t objectIdLow = 0u;
        MassRenderFloat4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
        MassRenderFloat3 scale{ 1.0f, 1.0f, 1.0f };
        uint32_t objectIdHigh = 0u;
        MassRenderFloat4 boundsCenterRadius{};

        uint32_t sourceRecordIndex = kInvalidMassRenderProxyIndex;
        uint32_t flags = 0u;
        uint32_t materialDataIndex = kInvalidMassRenderProxyIndex;
        uint32_t clusterGeometrySrvDescriptorIndex =
            kInvalidMassRenderProxyIndex;

        uint32_t clusterGeometryMetadataSrvDescriptorIndex =
            kInvalidMassRenderProxyIndex;
        uint32_t clusterSurfaceIndex = kInvalidMassRenderProxyIndex;
        uint32_t clusterRangeIndex = kInvalidMassRenderProxyIndex;
        uint32_t clusterRangeCount = 0u;

        uint32_t clusterIndexCount = 0u;
        uint32_t resourceFlags = 0u;
        uint32_t geometryBackend = 0u;
        uint32_t clusterLodRangeIndex = kInvalidMassRenderProxyIndex;

        uint32_t clusterLodRangeCount = 0u;
        uint32_t clusterSelectedLodIndex = 0u;
        uint32_t clusterLodFlags = 0u;
        uint32_t reserved = 0u;
    };

    static_assert(sizeof(MassRenderTransform) == 48u);
    static_assert(offsetof(MassRenderTransform, rotation) == 16u);
    static_assert(offsetof(MassRenderTransform, scale) == 32u);
    static_assert(sizeof(MassRenderProxyGpuRecord) == 128u);
    static_assert(offsetof(
        MassRenderProxyGpuRecord,
        boundsCenterRadius) == 48u);
    static_assert(offsetof(
        MassRenderProxyGpuRecord,
        sourceRecordIndex) == 64u);
    static_assert(offsetof(
        MassRenderProxyGpuRecord,
        clusterGeometryMetadataSrvDescriptorIndex) == 80u);
    static_assert(offsetof(
        MassRenderProxyGpuRecord,
        clusterIndexCount) == 96u);
    static_assert(offsetof(
        MassRenderProxyGpuRecord,
        clusterLodRangeCount) == 112u);

} // namespace HIKARI::RENDER3D::RUNTIME
