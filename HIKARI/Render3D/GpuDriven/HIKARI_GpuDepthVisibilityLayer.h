#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDepthVisibilityStats {
        bool psoReady = false;
        bool resourcesReady = false;
        bool depthPrepassWritten = false;
        bool hzbBuilt = false;
        bool hzbBuildRequested = false;
        bool descriptorPoolReady = false;
        bool visibilityDepthReady = false;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t hzbWidth = 0;
        uint32_t hzbHeight = 0;
        uint32_t hzbMipCount = 0;
        uint32_t hzbDescriptorCount = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE visibilityDepthSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE hzbFinestSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE hzbCoarsestSrv{};
        MATH::Mat4 hzbViewProj{};
        bool hzbViewProjValid = false;
        HIKARI::RENDER3D::DEPTH::DepthPyramidView depthPyramid{};
    };

    class GpuDepthVisibilityLayer final {
    public:
        void ResetFrame();
        void Release();

        void RecordDepthPrepass(bool written);
        D3D12_CPU_DESCRIPTOR_HANDLE BeginDepthPrepass(
            ID3D12GraphicsCommandList* cmd,
            uint32_t width,
            uint32_t height);
        bool BuildDepthPyramidFromVisibilityPrepass(
            ID3D12GraphicsCommandList* cmd,
            uint32_t width,
            uint32_t height,
            const MATH::Mat4& viewProj);
        bool BuildDepthPyramidFromDepthSrv(
            ID3D12GraphicsCommandList* cmd,
            uint32_t width,
            uint32_t height,
            D3D12_GPU_DESCRIPTOR_HANDLE sourceDepthSrv,
            const MATH::Mat4& viewProj);

        const GpuDepthVisibilityStats& GetStats() const { return stats_; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetVisibilityDepthSrv() const { return visibilityDepthSrv_.gpu; }
        const HIKARI::RENDER3D::DEPTH::DepthPyramidView& GetDepthPyramidView() const {
            return stats_.depthPyramid;
        }

    private:
        bool EnsureDepthResource(uint32_t width, uint32_t height);
        void ReleaseDepthResource();
        void TransitionDepth(
            ID3D12GraphicsCommandList* cmd,
            D3D12_RESOURCE_STATES nextState);
        void PublishDepthPyramidStats(
            const HIKARI::RENDER3D::DEPTH::DepthPyramidView& view);

        HIKARI::RENDER3D::DEPTH::DepthPyramidLayer depthPyramid_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> visibilityDepth_{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> visibilityDsvHeap_{};
        RenderResourceView visibilityDepthSrv_{};
        D3D12_CPU_DESCRIPTOR_HANDLE visibilityDsv_{};
        D3D12_RESOURCE_STATES visibilityDepthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        uint32_t depthWidth_ = 0;
        uint32_t depthHeight_ = 0;
        GpuDepthVisibilityStats stats_{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
