#pragma once

#include <cstdint>
#include <string>

#include <d3d12.h>
#include <dxgiformat.h>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::REFLECTION {

    enum class RuntimeReflectionProbeInfluenceShape {
        Sphere,
        Box,
    };

    enum class RuntimeReflectionProbeProjectionShape {
        Infinite,
        Box,
    };

    struct ReflectionProbeRuntimeData {
        bool enabled = false;
        bool valid = false;
        bool hasPrefiltered = false;
        bool hasBrdfLut = false;

        RENDER3D::TextureResourceHandle prefilteredResource{};
        RENDER3D::TextureResourceHandle brdfLutResource{};

        int prefilteredHandle = -1;
        int brdfLutHandle = -1;

        D3D12_GPU_DESCRIPTOR_HANDLE prefilteredSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE brdfLutSrv{};

        uint32_t prefilteredMipCount = 1;
        uint32_t prefilteredActualMipCount = 0;
        uint32_t brdfLutMipCount = 0;
        DXGI_FORMAT prefilteredFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT brdfLutFormat = DXGI_FORMAT_UNKNOWN;
        bool prefilteredMipMismatch = false;

        MATH::Vec3 position{ 0.0f, 2.0f, 0.0f };
        float radius = 8.0f;
        float intensity = 1.0f;
        RuntimeReflectionProbeInfluenceShape influenceShape = RuntimeReflectionProbeInfluenceShape::Sphere;
        RuntimeReflectionProbeProjectionShape projectionShape = RuntimeReflectionProbeProjectionShape::Infinite;
        MATH::Vec3 influenceBoxCenter{ 0.0f, 2.0f, 0.0f };
        MATH::Vec3 influenceBoxSize{ 8.0f, 4.0f, 8.0f };
        MATH::Vec3 projectionBoxCenter{ 0.0f, 2.0f, 0.0f };
        MATH::Vec3 projectionBoxSize{ 8.0f, 4.0f, 8.0f };
        float blendDistance = 1.0f;
        int priority = 0;

        std::string sourceAssetId{};
        std::string prefilteredPath{};
        std::string brdfLutPath{};
    };

    void Reset();

    void SetActiveProbeResources(
        bool enabled,
        RENDER3D::TextureResourceHandle prefilteredResource,
        RENDER3D::TextureResourceHandle brdfLutResource,
        uint32_t prefilteredMipCount,
        const MATH::Vec3& position,
        float radius,
        float intensity,
        RuntimeReflectionProbeInfluenceShape influenceShape,
        RuntimeReflectionProbeProjectionShape projectionShape,
        const MATH::Vec3& influenceBoxCenter,
        const MATH::Vec3& influenceBoxSize,
        const MATH::Vec3& projectionBoxCenter,
        const MATH::Vec3& projectionBoxSize,
        float blendDistance,
        int priority,
        std::string sourceAssetId,
        std::string prefilteredPath,
        std::string brdfLutPath);

    void SetActiveProbe(
        bool enabled,
        int prefilteredHandle,
        int brdfLutHandle,
        uint32_t prefilteredMipCount,
        const MATH::Vec3& position,
        float radius,
        float intensity,
        RuntimeReflectionProbeInfluenceShape influenceShape,
        RuntimeReflectionProbeProjectionShape projectionShape,
        const MATH::Vec3& influenceBoxCenter,
        const MATH::Vec3& influenceBoxSize,
        const MATH::Vec3& projectionBoxCenter,
        const MATH::Vec3& projectionBoxSize,
        float blendDistance,
        int priority,
        std::string sourceAssetId,
        std::string prefilteredPath,
        std::string brdfLutPath);

    const ReflectionProbeRuntimeData& GetActiveProbe();

    bool IsReflectionProbeSamplingSuppressed();

    class ScopedReflectionProbeSamplingSuppress {
    public:
        ScopedReflectionProbeSamplingSuppress();
        ~ScopedReflectionProbeSamplingSuppress();

        ScopedReflectionProbeSamplingSuppress(const ScopedReflectionProbeSamplingSuppress&) = delete;
        ScopedReflectionProbeSamplingSuppress& operator=(const ScopedReflectionProbeSamplingSuppress&) = delete;
    };

    D3D12_GPU_DESCRIPTOR_HANDLE GetPrefilteredSrv();
    D3D12_GPU_DESCRIPTOR_HANDLE GetBrdfLutSrv();

} // namespace HIKARI::REFLECTION
