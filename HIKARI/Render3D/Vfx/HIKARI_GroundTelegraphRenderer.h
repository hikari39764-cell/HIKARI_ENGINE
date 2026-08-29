#pragma once

#include <array>
#include <cstddef>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Vfx/HIKARI_CombatPresentationFrameSource.h"

namespace HIKARI::RENDER3D::COMBAT_VFX {

    struct GroundTelegraphRendererDebugState {
        bool requested = false;
        bool pipelineReady = false;
        bool rendered = false;
        uint32_t telegraphCount = 0u;
        uint32_t vertexCount = 0u;
    };

    class GroundTelegraphRenderer final {
    public:
        bool Initialize(ID3D12Device* device);
        bool Render(
            const GFX::Context& graphics,
            const PIPELINE::RenderTargetAccess& targetAccess,
            const CombatPresentationFrameSource& source,
            const MESHRENDERER::CameraCB& camera);

        void Release();

        const GroundTelegraphRendererDebugState&
            GetDebugState() const noexcept {
            return debugState_;
        }

    private:
        struct FrameResources {
            Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer{};
            Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer{};
            uint8_t* constantMapped = nullptr;
            uint8_t* vertexMapped = nullptr;
            size_t vertexCapacity = 0u;
        };

        bool EnsurePipeline(ID3D12Device* device);
        bool EnsureFrameResources(
            ID3D12Device* device,
            FrameResources& frame,
            size_t vertexCount);
        void ReleaseFrame(FrameResources& frame) noexcept;

        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_{};
        std::array<FrameResources, GFX::kFrameResourceCount> frames_{};
        GroundTelegraphRendererDebugState debugState_{};
        bool initializationAttempted_ = false;
        bool initializationFailed_ = false;
    };

} // namespace HIKARI::RENDER3D::COMBAT_VFX
