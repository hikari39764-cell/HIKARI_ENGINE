#pragma once

#include <cstdint>

#include <d3d12.h>

#include "Render3D/Core/HIKARI_RenderView.h"

namespace HIKARI {
    class Camera3D;
    struct SceneEnvironment;
}

namespace HIKARI::RENDER3D::PIPELINE {

    // Explicit render-target operations exposed to pipeline stages.
    struct RenderTargetAccess {
        using RebindCallback = bool (*)(void* userData);
        using BeginDepthReadCallback = bool (*)(void* userData);
        using EndDepthReadCallback = void (*)(void* userData);

        void* userData = nullptr;
        RebindCallback rebind = nullptr;
        BeginDepthReadCallback beginDepthRead = nullptr;
        EndDepthReadCallback endDepthRead = nullptr;

        bool Rebind() const {
            return rebind != nullptr && rebind(userData);
        }

        bool BeginDepthRead() const {
            return beginDepthRead != nullptr && beginDepthRead(userData);
        }

        void EndDepthRead() const {
            if (endDepthRead != nullptr) {
                endDepthRead(userData);
            }
        }
    };

    struct SceneFrameResources {
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsv{};
        D3D12_GPU_DESCRIPTOR_HANDLE depthSrv{};
        bool depthReadable = false;
    };

    // Immutable per-frame contract shared by renderer stages.
    struct RenderFrameContext {
        RenderViewId viewId{ kPrimaryRenderViewId };
        RenderViewPurpose purpose = RenderViewPurpose::Game;
        bool cameraCut = false;
        ID3D12GraphicsCommandList* cmd = nullptr;
        const Camera3D* camera = nullptr;
        const SceneEnvironment* environment = nullptr;
        uint32_t renderWidth = 1;
        uint32_t renderHeight = 1;
        uint32_t outputWidth = 1;
        uint32_t outputHeight = 1;
        SceneFrameResources scene{};
        RenderTargetAccess targetAccess{};
    };

    // Narrow view consumed by screen-space passes.
    struct ScreenSpacePassContext {
        ID3D12GraphicsCommandList* cmd = nullptr;
        uint32_t width = 1;
        uint32_t height = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv{};
        D3D12_CPU_DESCRIPTOR_HANDLE readOnlySceneDsv{};
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv{};
        bool depthReadable = false;
        RenderTargetAccess renderTargetAccess{};
    };

} // namespace HIKARI::RENDER3D::PIPELINE
