#pragma once

#include <cstdint>

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_RenderFrameToken.h"
#include "Render3D/Surface/HIKARI_RenderFrameSurfaceRegistry.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"

namespace HIKARI {
    class AssetRegistry;
    class Camera3D;
    struct SceneEnvironment;
}

namespace HIKARI::RENDER3D::PIPELINE {

    // Explicit render-target operations exposed to pipeline stages.
    struct RenderTargetAccess {
        using RebindCallback = bool (*)(void* userData);
        using BeginDepthReadCallback = bool (*)(void* userData);
        using EndDepthReadCallback = void (*)(void* userData);
        using ClearColorCallback = bool (*)(
            void* userData,
            float red,
            float green,
            float blue,
            float alpha) noexcept;

        void* userData = nullptr;
        RebindCallback rebind = nullptr;
        BeginDepthReadCallback beginDepthRead = nullptr;
        EndDepthReadCallback endDepthRead = nullptr;
        ClearColorCallback clearColor = nullptr;

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

        bool ClearColor(
            float red,
            float green,
            float blue,
            float alpha) const noexcept {
            return clearColor != nullptr && clearColor(
                userData, red, green, blue, alpha);
        }
    };

    struct SceneFrameResources {
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsv{};
        RenderTextureView depth{};
    };

    // Immutable per-frame contract shared by renderer stages.
    struct RenderFrameContext {
        RenderViewId viewId{ kPrimaryRenderViewId };
        RenderViewPurpose purpose = RenderViewPurpose::Game;
        bool cameraCut = false;
        bool projectionChanged = false;
        uint64_t cameraSourceObjectId = 0;
        uint64_t cameraRevision = 0;
        uint64_t frameIndex = 0;
        RenderFrameToken token{};
        SURFACE::RenderFrameSurfaceReceipt surfaceReceipt{};
        GFX::Context graphics{};
        const Camera3D* camera = nullptr;
        const SceneEnvironment* environment = nullptr;
        const AssetRegistry* assetRegistry = nullptr;
        RenderFrameProjectExtensionSink projectRenderExtensions{};
        uint32_t renderWidth = 1;
        uint32_t renderHeight = 1;
        uint32_t outputWidth = 1;
        uint32_t outputHeight = 1;
        SceneFrameResources scene{};
        RenderTargetAccess targetAccess{};
    };

    // Narrow view consumed by screen-space passes.
    struct ScreenSpacePassContext {
        RenderFrameToken frame{};
        ID3D12GraphicsCommandList* cmd = nullptr;
        const GFX::Context* graphics = nullptr;
        uint32_t width = 1;
        uint32_t height = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv{};
        D3D12_CPU_DESCRIPTOR_HANDLE readOnlySceneDsv{};
        RenderTextureView sceneDepth{};
        RenderTargetAccess renderTargetAccess{};
    };

} // namespace HIKARI::RENDER3D::PIPELINE
