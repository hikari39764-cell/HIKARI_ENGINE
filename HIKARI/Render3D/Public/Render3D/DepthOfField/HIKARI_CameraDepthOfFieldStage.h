#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_CameraOptics.h"
#include "RenderCore/Resources/HIKARI_RenderTextureView.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

namespace HIKARI {
class Camera3D;
class RenderTarget2D;
} // namespace HIKARI

namespace HIKARI::RENDER3D::TEMPORAL {
struct TemporalFrameState;
}

namespace HIKARI::RENDER3D::VOLUMETRIC {
struct VolumetricFogSamplingView;
}

namespace HIKARI::RENDER3D::DOF {

struct WorldSpriteLayerCapture {
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 6u> renderTargets{};
    CAMERA::DepthOfFieldSettings settings{};
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t frameSlot = 0u;
    uint64_t frameIndex = 0u;
};

bool IsCameraDepthOfFieldActive(const CAMERA::DepthOfFieldSettings& settings, DepthOfFieldQuality quality) noexcept;

// Prepare all resources and shader state before any WorldSprite is redirected
// to the separate layer targets. Failure leaves the ordinary sprite path active.
bool PrepareCameraDepthOfFieldFrame(const GFX::Context& context, uint64_t frameIndex,
                                    const RenderTextureView& sceneColor, const RenderTextureView& sceneDepth,
                                    const CAMERA::DepthOfFieldSettings& settings, DepthOfFieldQuality quality,
                                    bool captureWorldSprites, bool compositeSpriteFog);

const WorldSpriteLayerCapture* BeginWorldSpriteLayerCapture(uint64_t frameIndex) noexcept;
void MarkWorldSpriteLayerCapture(uint64_t frameIndex) noexcept;
void EndWorldSpriteLayerCapture(uint64_t frameIndex) noexcept;

bool ExecuteCameraDepthOfField(const GFX::Context& context, uint64_t frameIndex, RenderTarget2D& sceneColor,
                               const RenderTextureView& sceneDepth, const TEMPORAL::TemporalFrameState& temporalFrame,
                               const Camera3D& camera, const CAMERA::DepthOfFieldSettings& settings,
                               DepthOfFieldQuality quality, const VOLUMETRIC::VolumetricFogSamplingView& fogView);

void ShutdownCameraDepthOfFieldStage() noexcept;

} // namespace HIKARI::RENDER3D::DOF
