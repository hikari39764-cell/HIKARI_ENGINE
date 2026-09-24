#pragma once

#include <cstdint>
#include <d3d12.h>
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

namespace HIKARI {
struct StylizedCloudSettings;
}

namespace HIKARI::RENDER3D::PIPELINE {
struct RenderFrameContext;
}

namespace HIKARI::RENDER3D::TEMPORAL {
struct TemporalFrameState;
}

namespace HIKARI::RENDER3D::CLOUDS {

struct StylizedCloudStageStats {
    bool requested = false;
    bool active = false;
    bool pipelineReady = false;
    bool resourcesReady = false;
    bool noiseReady = false;
    bool historyValid = false;
    bool rendered = false;
    bool temporalRendered = false;
    bool compositeRendered = false;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t traceWidth = 0;
    uint32_t traceHeight = 0;
    uint32_t primarySteps = 0;
    uint32_t lightSteps = 0;
    uint32_t resolutionDivisor = 0;
    uint32_t qualityIndex = 0;
    uint32_t noiseDispatchCount = 0;
    uint32_t traceDispatchCount = 0;
    uint32_t temporalDispatchCount = 0;
    uint32_t compositeDispatchCount = 0;
    uint64_t workingSetBytes = 0;
    uint64_t resizeCount = 0;
    uint64_t historyResetCount = 0;
    uint64_t resourceFailureCount = 0;
    bool solarVisibilityReady = false;
    uint32_t solarResolution = 0;
    uint32_t solarSteps = 0;
    uint64_t solarBytes = 0;
};

struct CloudSolarVisibilityFrame {
    LIGHTING::CloudSolarVisibilityGpuData data{};
    D3D12_GPU_DESCRIPTOR_HANDLE srv{};
    uint64_t fenceValue = 0;
    explicit operator bool() const noexcept { return data.originDescriptor.w > 0 && srv.ptr != 0; }
};

CloudSolarVisibilityFrame PrepareCloudSolarVisibilityFrame(const PIPELINE::RenderFrameContext& frame,
                                                           const TEMPORAL::TemporalFrameState& temporalFrame,
                                                           const StylizedCloudSettings& settings,
                                                           StylizedCloudQuality quality);

bool ExecuteStylizedCloudStage(const PIPELINE::RenderFrameContext& frame,
                               const TEMPORAL::TemporalFrameState& temporalFrame, const StylizedCloudSettings& settings,
                               StylizedCloudQuality quality);

const StylizedCloudStageStats& GetStylizedCloudStageStats();
void ShutdownStylizedCloudStage();

} // namespace HIKARI::RENDER3D::CLOUDS
