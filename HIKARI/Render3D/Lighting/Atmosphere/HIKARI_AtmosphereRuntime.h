#pragma once

#include <cstdint>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::ATMOSPHERE {

    // Owns the reusable transmittance, multi-scattering, and sky-view LUTs.
    // The owner deliberately keeps the resources local to the atmosphere
    // stage: their SRV/UAV descriptors and default-heap textures are retired
    // through the frame fence before a configuration rebuild.
    class AtmosphereLutOwner final {
    public:
        AtmosphereLutOwner() = default;
        ~AtmosphereLutOwner();

        AtmosphereLutOwner(const AtmosphereLutOwner&) = delete;
        AtmosphereLutOwner& operator=(const AtmosphereLutOwner&) = delete;

        bool Configure(const AtmosphereSettings& settings);
        void SetContext(const GFX::Context& context) noexcept { lastContext_ = context; }
        void Invalidate();
        void Reset();

        // Allocates all three UAV/SRV pairs and the shared compute pipeline.
        // Returns false when the descriptor pool, device, or shader compiler
        // cannot provide a complete GPU path; callers must use analytic sky
        // in that case.
        bool EnsureGpuResources(const GFX::Context& context);

        // Dispatches only when the supplied parameter signature changed. The
        // final state is PIXEL_SHADER_RESOURCE so SkyRenderer can sample the
        // sky-view LUT in its pixel pass.
        bool Dispatch(
            const GFX::Context& context,
            D3D12_GPU_VIRTUAL_ADDRESS constantsGpu,
            uint64_t parameterSignature);

        const RENDER3D::RenderResourceView& GetSkyViewSrv() const noexcept {
            return skyViewGpu_.srv;
        }

    private:
        struct AtmosphereLutDimensions {
            uint32_t transmittanceWidth = 256u;
            uint32_t transmittanceHeight = 64u;
            uint32_t multiScatteringSize = 32u;
            uint32_t skyViewWidth = 192u;
            uint32_t skyViewHeight = 108u;
        };

        struct GpuLut {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource{};
            RENDER3D::RenderResourceView srv{};
            RENDER3D::RenderResourceView uav{};
            D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        };

        bool EnsurePipeline(const GFX::Context& context);
        bool CreateLut(
            const GFX::Context& context,
            GpuLut& lut,
            uint32_t width,
            uint32_t height,
            const wchar_t* debugName);
        void RetireView(
            const GFX::Context& context,
            RENDER3D::RenderResourceView& view,
            const char* debugName);
        void RetireResource(
            const GFX::Context& context,
            Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
            const char* debugName);
        void Transition(
            ID3D12GraphicsCommandList* commandList,
            GpuLut& lut,
            D3D12_RESOURCE_STATES next);

        AtmosphereLutDimensions dimensions_{};
        AtmosphereSettings settings_{};
        bool hasSettings_ = false;
        GpuLut transmittanceGpu_{};
        GpuLut multiScatteringGpu_{};
        GpuLut skyViewGpu_{};
        Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRoot_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> transmittancePso_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> multiScatteringPso_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> skyViewPso_{};
        Microsoft::WRL::ComPtr<ID3DBlob> transmittanceCs_{};
        Microsoft::WRL::ComPtr<ID3DBlob> multiScatteringCs_{};
        Microsoft::WRL::ComPtr<ID3DBlob> skyViewCs_{};
        GFX::Context lastContext_{};
        uint64_t lastDispatchSignature_ = 0u;
        bool resourcesReady_ = false;
        bool pipelineReady_ = false;
        bool configured_ = false;
        bool gpuLutsReady_ = false;
    };

} // namespace HIKARI::ATMOSPHERE
