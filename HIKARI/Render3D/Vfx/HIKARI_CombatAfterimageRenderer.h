#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Core/MeshRenderer/Resources/HIKARI_MeshPrimitiveCache.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Vfx/HIKARI_CombatAfterimageFrameSource.h"

namespace HIKARI::RENDER3D::COMBAT_VFX {

    struct CombatAfterimageRendererDebugState {
        bool requested = false;
        bool pipelineReady = false;
        bool rendered = false;
        bool missingPose = false;
        bool layoutMismatch = false;
        uint32_t drawCount = 0u;
        uint32_t silhouetteCount = 0u;
        uint32_t skinnedDrawCount = 0u;
        uint32_t staticDrawCount = 0u;
        uint32_t skippedDrawCount = 0u;
    };

    class CombatAfterimageRenderer final {
    public:
        bool Render(
            const GFX::Context& graphics,
            const PIPELINE::RenderTargetAccess& targetAccess,
            const CombatAfterimageFrameSource& source,
            const MESHRENDERER::CameraCB& camera);

        void Release();

        const CombatAfterimageRendererDebugState&
            GetDebugState() const noexcept {
            return debugState_;
        }

    private:
        struct FrameResources {
            Microsoft::WRL::ComPtr<ID3D12Resource> cameraBuffer{};
            Microsoft::WRL::ComPtr<ID3D12Resource> drawBuffer{};
            uint8_t* cameraMapped = nullptr;
            uint8_t* drawMapped = nullptr;
            size_t drawCapacity = 0u;
        };

        bool EnsurePipeline(ID3D12Device* device);
        bool EnsureFrameResources(
            ID3D12Device* device,
            FrameResources& frame,
            size_t drawCount);
        void ReleaseFrame(FrameResources& frame) noexcept;

        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState>
            skinnedPipelineState_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState>
            staticPipelineState_{};
        std::array<FrameResources, GFX::kFrameResourceCount> frames_{};
        MESHRENDERER::MeshPrimitiveCache primitiveCache_{};
        CombatAfterimageRendererDebugState debugState_{};
    };

} // namespace HIKARI::RENDER3D::COMBAT_VFX
