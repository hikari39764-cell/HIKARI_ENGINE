#pragma once

// Spectral band/Stockham design cross-checked against
// siliconjungle/inkwell-webgpu-water @ 9b9a5ebfaf1600fee0a2e6c56da0720a54bfbcd2
// (MIT, Copyright (c) 2026 James Addison). No third-party source is vendored;
// this owner adapts the algorithm to HIKARI's DX12 resource contracts.

#include <array>
#include <cstdint>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"

namespace HIKARI::RENDER3D::WATER::SPECTRUM {

    constexpr uint32_t kOceanSpectrumCascadeCount = 3u;
    constexpr uint32_t kOceanSpectrumMinResolution = 64u;
    constexpr uint32_t kOceanSpectrumMaxResolution = 128u;
    constexpr uint32_t kOceanSpectrumDisplacementTableOffset = 0u;
    constexpr uint32_t kOceanSpectrumSlopeTableOffset =
        kOceanSpectrumCascadeCount;

    enum class OceanSpectrumCascadeKind : uint8_t {
        Long = 0,
        Wind = 1,
        Capillary = 2,
    };

    // One independent wavelength band. The cutoffs are expressed in radians
    // per metre and are intentionally disjoint so the bands do not double
    // count the same energy.
    struct OceanSpectrumCascadeDesc {
        float lengthScale = 240.0f;
        float cutoffLow = 0.024f;
        float cutoffHigh = 0.36f;
        float amplitudeScale = 0.45f;
        float choppiness = 1.18f;
        float secondaryScale = 0.22f;
        uint32_t seed = 0x0051f15eu;
    };

    struct OceanSpectrumRuntimeConfig {
        uint32_t resolution = kOceanSpectrumMaxResolution;
        std::array<OceanSpectrumCascadeDesc, kOceanSpectrumCascadeCount> cascades{
            OceanSpectrumCascadeDesc{
                240.0f, 0.024f, 0.30f, 0.45f, 1.18f, 0.22f, 0x0051f15eu},
            OceanSpectrumCascadeDesc{
                64.0f, 0.30f, 1.22f, 0.45f, 1.05f, 0.08f, 0x0072a93bu},
            OceanSpectrumCascadeDesc{
                12.0f, 1.22f, 24.0f, 0.82f, 0.40f, 0.0f, 0x0019ce47u},
        };
    };

    // Producer-owned, sampleable output for one cascade. The views remain
    // valid until the next Initialize/Shutdown call on the owner. The
    // displacement channels are (horizontal X, height, horizontal Z, 1).
    // The slope channels are (dHeight/dX, dHeight/dZ, horizontal Jacobian,
    // 1). All values are scene-linear and periodic over the cascade domain.
    struct OceanSpectrumCascadeOutput {
        RenderTextureView displacement{};
        RenderTextureView slope{};
        float lengthScale = 0.0f;
        float choppiness = 0.0f;
        uint32_t resolution = 0u;
        uint64_t frameIndex = 0u;
        bool valid = false;
    };

    struct OceanSpectrumRuntimeStats {
        bool initialized = false;
        bool pipelineReady = false;
        bool outputValid = false;
        uint32_t resolution = 0u;
        uint32_t cascadeCount = 0u;
        uint32_t dispatchCount = 0u;
        uint64_t frameIndex = 0u;
        uint64_t workingSetBytes = 0u;
        uint64_t resizeCount = 0u;
        uint64_t historyResetCount = 0u;
        bool contextConflict = false;
    };

    // Six SRVs are allocated as one contiguous range in the global
    // shader-visible heap: displacement[Long, Wind, Capillary], followed by
    // slope[Long, Wind, Capillary]. The range is borrowed from the owner and
    // remains valid until the next Initialize/Shutdown call.
    struct OceanSpectrumOutputDescriptorTable {
        D3D12_GPU_DESCRIPTOR_HANDLE base{};
        uint32_t descriptorCount = 0u;
        bool valid = false;
    };

    // Render3D owner for a multi-scale frequency-domain ocean. It owns only
    // spectrum resources and compute passes; Scene water data, mesh geometry,
    // material shading, and RenderFramePipeline ordering remain external.
    class OceanSpectrumRuntime final {
    public:
        OceanSpectrumRuntime() = default;
        ~OceanSpectrumRuntime();

        OceanSpectrumRuntime(const OceanSpectrumRuntime&) = delete;
        OceanSpectrumRuntime& operator=(const OceanSpectrumRuntime&) = delete;

        bool Initialize(
            const GFX::Context& context,
            const OceanSpectrumRuntimeConfig& config = {});

        // Records one simulation update for frameIndex. Calling this more
        // than once for the same frame is idempotent only when the device,
        // queue, command list, descriptor heap, and resource/fence context
        // identity all match. A conflicting context invalidates the frame
        // and is rejected until the next frame so unsynchronized output is
        // never reused or recorded twice concurrently.
        bool RecordCompute(
            const GFX::Context& context,
            uint64_t frameIndex,
            float timeSeconds);

        // Retires GPU objects through the supplied frame context when one is
        // available. A null context is safe only after the GPU is idle.
        void Shutdown(const GFX::Context* context = nullptr);

        bool IsInitialized() const noexcept;
        bool IsOutputValid() const noexcept;
        bool IsOutputValidForFrame(uint64_t frameIndex) const noexcept;
        uint32_t GetResolution() const noexcept;

        const OceanSpectrumCascadeOutput& GetOutput(
            OceanSpectrumCascadeKind cascade) const noexcept;
        const std::array<OceanSpectrumCascadeOutput, kOceanSpectrumCascadeCount>&
        GetOutputs() const noexcept;
        const OceanSpectrumOutputDescriptorTable&
        GetOutputDescriptorTable() const noexcept;
        const OceanSpectrumRuntimeStats& GetStats() const noexcept;

        // Opaque implementation state; public only so translation-unit-local
        // Render3D helpers can keep the header free of DX12 implementation
        // details.
        struct State;

    private:
        State* state_ = nullptr;
    };

} // namespace HIKARI::RENDER3D::WATER::SPECTRUM
