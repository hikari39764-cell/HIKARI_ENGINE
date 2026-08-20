#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"
#include "Render3D/Vfx/HIKARI_CombatPresentationFrameSource.h"

namespace HIKARI::RENDER3D::COMBAT_VFX {

    enum class WeaponTrailRendererFailure : uint32_t {
        None = 0u,
        MissingGraphicsContext = 1u,
        InvalidFrameSource = 2u,
        MissingRenderInputs = 3u,
        DegenerateGeometry = 4u,
        PipelineInitialization = 5u,
        DepthRead = 6u,
        FrameResources = 7u,
        MaskDescriptor = 8u,
        RenderTargetRebind = 9u,
        InvalidMaskView = 10u,
    };

    // Value-only handoff for the later post composite. The renderer owns the
    // resource; consumers receive borrowed descriptor/resource views tagged
    // with the producing absolute presentation frame.
    struct WeaponTrailDistortionMaskView {
        RenderTextureView texture{};
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
        uint64_t frameIndex = 0u;
        float strengthScale = 1.0f;
        float blurScale = 1.0f;
        bool valid = false;

        bool IsValid() const noexcept {
            return valid && texture.IsSampleable() && srvCpu.ptr != 0u;
        }
    };

    struct WeaponTrailRendererDebugState {
        bool requested = false;
        bool maskExtentReady = false;
        bool pipelineReady = false;
        bool rendered = false;
        bool composited = false;
        bool maskReady = false;
        uint32_t vertexCount = 0u;
        uint32_t triangleCount = 0u;
        uint32_t nonDegenerateTriangleCount = 0u;
        uint32_t activeTrailCount = 0u;
        uint32_t maskWidth = 0u;
        uint32_t maskHeight = 0u;
        uint64_t maskFrame = 0u;
        WeaponTrailRendererFailure failureReason =
            WeaponTrailRendererFailure::None;
    };

    class WeaponTrailRenderer final {
    public:
        bool Render(
            const PIPELINE::ScreenSpacePassContext& pass,
            const CombatPresentationFrameSource& source,
            const MATH::Mat4& viewProj);
        void Release();

        // The trail pass owns this render target. It is intentionally a
        // borrowed value view so the post stack can consume the vector /
        // coverage mask without coupling ownership to the combat service.
        WeaponTrailDistortionMaskView GetDistortionMask() const noexcept {
            return {
                distortionMaskView_,
                distortionMaskSrvCpu_,
                distortionMaskFrame_,
                1.0f,
                1.0f,
                distortionMaskView_.valid };
        }

        // Kept as a narrow SRV-only accessor for callers that only need the
        // shader resource view and dimensions.
        RenderTextureView GetDistortionMaskView() const noexcept {
            return distortionMaskView_;
        }

        uint64_t GetDistortionMaskFrame() const noexcept {
            return distortionMaskFrame_;
        }

        const WeaponTrailRendererDebugState&
            GetDebugState() const noexcept {
            return debugState_;
        }

    private:
        struct FrameResources {
            Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer{};
            Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer{};
            WeaponTrailVertex* vertexMapped = nullptr;
            uint8_t* constantMapped = nullptr;
            size_t vertexCapacity = 0u;
        };

        bool EnsurePipeline(ID3D12Device* device);
        bool EnsureFrameResources(
            ID3D12Device* device,
            FrameResources& frame,
            size_t vertexCount);
        bool EnsureDistortionMask(
            ID3D12Device* device,
            uint32_t width,
            uint32_t height);
        bool ClearDistortionMask(const GFX::Context& graphics) noexcept;
        D3D12_CPU_DESCRIPTOR_HANDLE GetDistortionMaskRtv(
            uint32_t frameSlot) const noexcept;
        void ReleaseDistortionMask() noexcept;
        void ReleaseFrame(FrameResources& frame) noexcept;
        void SetFailure(WeaponTrailRendererFailure failure) noexcept;
        void ClearFailure() noexcept;

        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_{};
        std::array<FrameResources, GFX::kFrameResourceCount> frames_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> distortionMaskTarget_{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> distortionMaskRtvHeap_{};
        UINT distortionMaskRtvDescriptorSize_ = 0u;
        D3D12_CPU_DESCRIPTOR_HANDLE distortionMaskSrvCpu_{};
        TextureResourceHandle distortionMaskTexture_{};
        D3D12_RESOURCE_STATES distortionMaskState_ =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        RenderTextureView distortionMaskView_{};
        uint32_t distortionMaskWidth_ = 0u;
        uint32_t distortionMaskHeight_ = 0u;
        uint64_t distortionMaskFrame_ = 0u;
        WeaponTrailRendererDebugState debugState_{};
        WeaponTrailRendererFailure lastLoggedFailure_ =
            WeaponTrailRendererFailure::None;
        bool readyLogged_ = false;
    };

} // namespace HIKARI::RENDER3D::COMBAT_VFX
