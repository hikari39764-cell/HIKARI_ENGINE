#pragma once

#include <cstdint>
#include <span>

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
} // namespace HIKARI

namespace HIKARI::RENDER3D::PIPELINE {

// Explicit render-target operations exposed to pipeline stages.
struct RenderTargetAccess {
    using RebindCallback = bool (*)(void* userData);
    using BindAdditionalColorsCallback = bool (*)(void*, std::span<const D3D12_CPU_DESCRIPTOR_HANDLE>);
    using BeginDepthReadCallback = bool (*)(void* userData);
    using EndDepthReadCallback = void (*)(void* userData);
    using ClearColorCallback = bool (*)(void* userData, float red, float green, float blue, float alpha) noexcept;
    using CopyColorFromCallback = bool (*)(void* userData, ID3D12Resource* source, DXGI_FORMAT format, uint32_t width,
                                           uint32_t height) noexcept;

    void* userData = nullptr;
    RebindCallback rebind = nullptr;
    BindAdditionalColorsCallback bindAdditionalColors = nullptr;
    BeginDepthReadCallback beginDepthRead = nullptr;
    EndDepthReadCallback endDepthRead = nullptr;
    ClearColorCallback clearColor = nullptr;
    CopyColorFromCallback copyColorFrom = nullptr;

    bool Rebind() const { return rebind != nullptr && rebind(userData); }

    bool BindAdditionalColors(std::span<const D3D12_CPU_DESCRIPTOR_HANDLE> colors) const {
        return bindAdditionalColors != nullptr && bindAdditionalColors(userData, colors);
    }

    bool BeginDepthRead() const { return beginDepthRead != nullptr && beginDepthRead(userData); }

    void EndDepthRead() const {
        if (endDepthRead != nullptr) {
            endDepthRead(userData);
        }
    }

    bool ClearColor(float red, float green, float blue, float alpha) const noexcept {
        return clearColor != nullptr && clearColor(userData, red, green, blue, alpha);
    }

    bool CopyColorFrom(ID3D12Resource* source, DXGI_FORMAT format, uint32_t width, uint32_t height) const noexcept {
        return copyColorFrom != nullptr && copyColorFrom(userData, source, format, width, height);
    }
};

struct SceneFrameResources {
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsv{};
    RenderTextureView depth{};
    RenderTextureView color{};
};

// Immutable per-frame contract shared by renderer stages.
struct RenderFrameContext {
    ScreenReadabilityFrame screenReadability{};
    // 場景キャプチャを映す面（画面に大きく写る順に上限まで）。時間方向の遮罩が使う。
    ScreenReadabilityFrame sceneCaptureSurfaces{};
    RenderViewId viewId{kPrimaryRenderViewId};
    RenderViewPurpose purpose = RenderViewPurpose::Game;
    bool cameraCut = false;
    bool continuousCameraSource = false;
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
    bool jitteredViewProj = false;
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
