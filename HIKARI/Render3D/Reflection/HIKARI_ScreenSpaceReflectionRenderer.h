#pragma once

#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"

namespace HIKARI::RENDER3D::REFLECTION {

    struct ScreenSpaceReflectionDebugState {
        bool requested = false;
        bool sceneSurfaceReady = false;
        bool currentHzbReady = false;
        bool pipelineReady = false;
        bool rendered = false;
        uint32_t maxSteps = 0;
        uint32_t hzbMipCount = 0;
        uint32_t traceWidth = 0;
        uint32_t traceHeight = 0;
        float maxDistance = 0.0f;
        float maxRoughness = 0.0f;
    };

    class ScreenSpaceReflectionRenderer final {
    public:
        bool Render(
            const GFX::Context& graphics,
            const PIPELINE::RenderTargetAccess& targetAccess,
            const SURFACE::SceneSurfaceSet& surfaces,
            const MESHRENDERER::CameraCB& camera,
            const ScreenSpaceReflectionSettings& settings,
            RenderDebugView debugView);
        void Release();

        const ScreenSpaceReflectionDebugState& GetDebugState() const {
            return debugState_;
        }

    private:
        bool EnsurePipeline(ID3D12Device* device);
        bool EnsureConstantBuffer(ID3D12Device* device);
        bool EnsureTraceTarget(
            ID3D12Device* device,
            uint32_t width,
            uint32_t height);
        void ReleaseTraceTarget();

        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> tracePipelineState_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> resolvePipelineState_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> traceTarget_{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> traceRtvHeap_{};
        TextureResourceHandle traceTexture_{};
        D3D12_CPU_DESCRIPTOR_HANDLE traceRtv_{};
        D3D12_GPU_DESCRIPTOR_HANDLE traceSrv_{};
        D3D12_RESOURCE_STATES traceTargetState_ =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        uint8_t* constantMapped_ = nullptr;
        uint32_t constantStride_ = 0;
        uint32_t traceWidth_ = 0;
        uint32_t traceHeight_ = 0;
        ScreenSpaceReflectionDebugState debugState_{};
    };

} // namespace HIKARI::RENDER3D::REFLECTION
