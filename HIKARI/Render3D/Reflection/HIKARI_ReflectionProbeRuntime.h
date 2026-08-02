#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>
#include <dxgiformat.h>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeContract.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::REFLECTION {

    inline constexpr size_t kReflectionProbeViewSlotCount =
        RENDER3D::LIGHTING::kLightingProviderViewSlotCount;

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

        uint32_t requestedPrefilteredMipCount = 1;
        uint32_t prefilteredMipCount = 1;
        uint32_t prefilteredActualMipCount = 0;
        uint32_t brdfLutMipCount = 0;
        DXGI_FORMAT prefilteredFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT brdfLutFormat = DXGI_FORMAT_UNKNOWN;
        bool prefilteredMipMismatch = false;

        MATH::Vec3 position{ 0.0f, 2.0f, 0.0f };
        float radius = 8.0f;
        float intensity = 1.0f;
        RuntimeReflectionProbeInfluenceShape influenceShape =
            RuntimeReflectionProbeInfluenceShape::Sphere;
        RuntimeReflectionProbeProjectionShape projectionShape =
            RuntimeReflectionProbeProjectionShape::Infinite;
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

    struct ReflectionProbeViewSelection {
        std::array<size_t, kReflectionProbeViewSlotCount> providerIndices{};
        std::array<float, kReflectionProbeViewSlotCount> viewInfluences{};
        size_t count = 0;
    };

    void Reset();
    void SetProbeSet(std::vector<ReflectionProbeRuntimeData> probes);

    const std::vector<ReflectionProbeRuntimeData>& GetProbeSet();
    void PrepareView(
        const MATH::Vec3& cameraPosition,
        const MATH::Vec3& viewFocusPosition);
    const ReflectionProbeViewSelection& GetViewSelection();
    const ReflectionProbeRuntimeData* GetViewProbe(size_t slot);

    bool IsReflectionProbeSamplingSuppressed();

    class ScopedReflectionProbeSamplingSuppress {
    public:
        ScopedReflectionProbeSamplingSuppress();
        ~ScopedReflectionProbeSamplingSuppress();

        ScopedReflectionProbeSamplingSuppress(
            const ScopedReflectionProbeSamplingSuppress&) = delete;
        ScopedReflectionProbeSamplingSuppress& operator=(
            const ScopedReflectionProbeSamplingSuppress&) = delete;

    private:
        ReflectionProbeViewSelection previousSelection_{};
    };

    D3D12_GPU_DESCRIPTOR_HANDLE GetViewPrefilteredSrv(size_t slot);
    D3D12_GPU_DESCRIPTOR_HANDLE GetSharedBrdfLutSrv();

} // namespace HIKARI::REFLECTION
