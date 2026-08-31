#pragma once

#include <cstdint>
#include <string>

#include <d3d12.h>

#include "Render3D/Core/HIKARI_RenderFrameToken.h"

namespace HIKARI::GFX {
    struct Context;
}

namespace HIKARI::RENDER3D {
    struct RenderTextureView;
    namespace SURFACE {
        struct SceneSurfaceSet;
    }
}

namespace HIKARI::RENDER3D::PIPELINE {
    struct RenderTargetAccess;

namespace INTERNAL {

    struct ProjectDispatchOutputBinding {
        ID3D12Resource* resource = nullptr;
        D3D12_GPU_DESCRIPTOR_HANDLE table{};
        uint32_t frameSlot = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;
    };

    bool PrepareProjectDispatchOutput(
        const GFX::Context& graphics,
        const RenderTextureView& input,
        uint32_t width,
        uint32_t height,
        ProjectDispatchOutputBinding& outBinding,
        std::string& outDiagnostic) noexcept;

    bool PublishProjectDispatchOutput(
        const GFX::Context& graphics,
        SURFACE::SceneSurfaceSet& surfaces,
        RenderFrameToken frame,
        ProjectDispatchOutputBinding binding,
        std::string& outDiagnostic) noexcept;

    void ReleaseProjectDispatchOutputResources() noexcept;

} // namespace INTERNAL
} // namespace HIKARI::RENDER3D::PIPELINE
