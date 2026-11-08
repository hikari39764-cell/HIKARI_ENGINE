#pragma once

#include <cstdint>
#include <span>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Render3D/Lighting/HIKARI_LocalFogVolume.h"
#include "RenderCore/Resources/HIKARI_RenderTextureView.h"

namespace HIKARI {
class RenderTarget2D;
struct SceneEnvironment;
enum class RenderDebugView : uint32_t;
} // namespace HIKARI

namespace HIKARI::RENDER3D {
enum class VolumetricLightingQuality : uint8_t;
}

namespace HIKARI::RENDER3D::TEMPORAL {
struct TemporalFrameState;
}

namespace HIKARI::RENDER3D::WATER {
struct WaterVisibleDepthView;
}

namespace HIKARI::SHADOW {
struct LocalLightShadowFrame;
}

namespace HIKARI::RENDER3D::VOLUMETRIC {

struct VolumetricLightingStats {
    bool requested = false;
    bool active = false;
    bool sceneTargetsReady = false;
    bool historyValid = false;
    bool shadowed = false;
    uint32_t localShadowFaces = 0;
    bool waterDepthValid = false;
    bool waterDepthFallback = true;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t froxelWidth = 0;
    uint32_t froxelHeight = 0;
    uint32_t froxelDepth = 0;
    uint32_t froxelPixelSize = 0;
    uint32_t qualityLevel = 0;
    uint32_t localLightCount = 0;
    uint32_t pointLightCount = 0;
    uint32_t spotLightCount = 0;
    uint32_t dispatchCount = 0;
    float temporalWeight = 0.0f;
    uint64_t workingSetBytes = 0;
    uint64_t resizeCount = 0;
    uint64_t historyResetCount = 0;
};

// Borrowed only after this frame's volume integration and scene composite.
struct VolumetricFogSamplingView {
    D3D12_GPU_DESCRIPTOR_HANDLE integratedVolumeSrv{};
    float nearDistance = 0.0f;
    float farDistance = 0.0f;
    uint32_t depthSlices = 0u;
    uint64_t frameIndex = 0u;
    bool valid = false;
};

bool PrepareVolumetricLightingFrame(const GFX::Context& context, const TEMPORAL::TemporalFrameState& temporalFrame,
                                    const RenderTextureView& sceneColor, const RenderTextureView& sceneDepth,
                                    const SceneEnvironment& environment,
                                    const LIGHTING::LocalLightListGpuBindings& localLights,
                                    VolumetricLightingQuality quality, RenderDebugView debugView,
                                    const LIGHTING::CloudSolarVisibilityGpuData& smokeSolarVisibility,
                                    const SHADOW::LocalLightShadowFrame* localShadows = nullptr,
                                    std::span<const LocalFogVolumeGpu> localFogVolumes = {});

RenderTarget2D* ExecuteVolumetricLightingStage(RenderTarget2D& sceneTarget);
bool GetVolumetricFogSamplingView(uint64_t frameIndex, VolumetricFogSamplingView& outView) noexcept;
// Borrow a producer-owned water depth view for the prepared frame/view.
// Prepare clears this input every frame; Execute falls back to opaque
// SceneDepth when the token, dimensions, or frame do not match.
void SetVolumetricVisibleWaterDepth(const WATER::WaterVisibleDepthView& view);
// 流体の烟の覆い（全解像度、x 烟の平均距離 [m]、y 不透明度）を借りる。合成は烟のある画素で、烟の距離までの霧と不透明面
// までの霧を不透明度で混ぜる（烟に隠れた物の輪郭が霧で浮き出ない）。Prepare が毎フレーム外し、Execute は大きさが場面と
// 合わない時は使わない。
void SetVolumetricFluidCoverage(D3D12_GPU_DESCRIPTOR_HANDLE srv, uint32_t width, uint32_t height);
bool IsVolumetricLightingActive();
const VolumetricLightingStats& GetVolumetricLightingStats();
void ShutdownVolumetricLightingStage();

} // namespace HIKARI::RENDER3D::VOLUMETRIC
