#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::RENDER3D::DEPTH {

    enum class DepthPyramidDepthConvention : uint32_t {
        StandardD3DLessMax,
    };

    enum class DepthPyramidSourceKind : uint32_t {
        Unknown,
        VisibilityPrepass,
        SceneDepth,
    };

    enum class DepthPyramidViewKind : uint32_t {
        Invalid,
        CurrentFrame,
        History,
    };

    struct DepthPyramidView {
        bool valid = false;
        DepthPyramidSourceKind sourceKind = DepthPyramidSourceKind::Unknown;
        DepthPyramidDepthConvention depthConvention =
            DepthPyramidDepthConvention::StandardD3DLessMax;
        DepthPyramidViewKind viewKind = DepthPyramidViewKind::Invalid;
        D3D12_GPU_DESCRIPTOR_HANDLE pyramidSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE mip0Srv{};
        D3D12_GPU_DESCRIPTOR_HANDLE coarsestSrv{};
        uint32_t sourceWidth = 0;
        uint32_t sourceHeight = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipCount = 0;
        uint32_t descriptorCount = 0;
        uint32_t frameIndex = 0;
        MATH::Mat4 viewProj{};
        bool viewProjValid = false;
        bool jitteredViewProj = false;
    };

    struct DepthPyramidBuildDesc {
        ID3D12GraphicsCommandList* commandList = nullptr;
        uint32_t sourceWidth = 0;
        uint32_t sourceHeight = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE sourceDepthSrv{};
        MATH::Mat4 viewProj{};
        bool viewProjValid = false;
        bool jitteredViewProj = false;
        uint32_t frameIndex = 0;
        DepthPyramidSourceKind sourceKind = DepthPyramidSourceKind::Unknown;
        DepthPyramidViewKind viewKind = DepthPyramidViewKind::CurrentFrame;
        const char* pixEventName = nullptr;
    };

    struct DepthPyramidStats {
        bool psoReady = false;
        bool resourcesReady = false;
        bool buildRequested = false;
        bool built = false;
        bool descriptorPoolReady = false;
        DepthPyramidView currentView{};
    };

    RenderResourceView AllocateDepthPyramidTransientDescriptor();
    void RetireDepthPyramidTransientDescriptor(
        RenderResourceView view,
        const char* debugName);

    class DepthPyramidLayer final {
    public:
        void ResetFrame();
        void Release();

        bool BuildFromDepthSrv(const DepthPyramidBuildDesc& desc);

        const DepthPyramidStats& GetStats() const { return stats_; }
        const DepthPyramidView& GetCurrentView() const { return currentView_; }

    private:
        struct MipView {
            RenderResourceView srv{};
            RenderResourceView uav{};
            D3D12_RESOURCE_STATES state =
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            uint32_t width = 0;
            uint32_t height = 0;
        };

        bool EnsurePipeline();
        bool EnsureResources(uint32_t sourceWidth, uint32_t sourceHeight);
        void ReleaseResources();
        void TransitionMip(
            ID3D12GraphicsCommandList* cmd,
            uint32_t mipIndex,
            D3D12_RESOURCE_STATES nextState);
        void PublishCurrentView(const DepthPyramidBuildDesc& desc);

        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> texture_{};
        RenderResourceView pyramidSrv_{};
        std::vector<MipView> mips_{};
        uint32_t sourceWidth_ = 0;
        uint32_t sourceHeight_ = 0;
        DepthPyramidView currentView_{};
        DepthPyramidStats stats_{};
    };

} // namespace HIKARI::RENDER3D::DEPTH
