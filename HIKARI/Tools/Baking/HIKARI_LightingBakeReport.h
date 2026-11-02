#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace HIKARI::TOOLS::BAKING {

enum class LightingBakeTarget {
    All,
    ReflectionProbesOnly,
    LightProbesOnly,
    LightmapsOnly,
};

enum class LightingBakeAction {
    ClearBake,
    BakeReflectionProbes,
    BakeLightProbes,
    BakeLightmaps,
    BakeLighting,
};

enum class LightingBakeJobState {
    Idle,
    Requested,
    Capturing,
    Tracing,
    WaitingGpu,
    ProjectingSH,
    Saving,
    Finalizing,
    Completed,
    Failed,
};

inline bool IsLightingBakeJobRunning(LightingBakeJobState state) noexcept {
    switch (state) {
    case LightingBakeJobState::Requested:
    case LightingBakeJobState::Capturing:
    case LightingBakeJobState::Tracing:
    case LightingBakeJobState::WaitingGpu:
    case LightingBakeJobState::ProjectingSH:
    case LightingBakeJobState::Saving:
    case LightingBakeJobState::Finalizing: return true;
    default: return false;
    }
}

struct LightingBakeReport {
    bool success = true;
    bool manifestWritten = false;
    bool bakeFolderCreated = false;
    bool bakeFolderCleared = false;
    bool reflectionProbeCaptured = false;
    bool reflectionProbePrefiltered = false;
    bool reflectionProbeRecordWritten = false;
    bool reflectionProbeSceneCaptured = false;
    bool reflectionProbeUsedSourceOverride = false;
    bool reflectionProbeCaptureValidated = false;
    bool reflectionProbePrefilterValidated = false;
    bool lightProbeBaked = false;
    bool lightProbeRuntimeLoaded = false;
    bool lightProbeVolumeWritten = false;
    bool lightProbeRecordWritten = false;
    bool lightProbeDebugJsonWritten = false;
    bool lightmapBaked = false;
    bool lightmapRecordWritten = false;
    bool lightmapRuntimeLoaded = false;
    bool dxrBackendAvailable = false;
    LightingBakeAction action = LightingBakeAction::BakeLighting;
    LightingBakeTarget target = LightingBakeTarget::All;
    LightingBakeJobState jobState = LightingBakeJobState::Idle;
    std::filesystem::path bakeRoot{};
    std::filesystem::path manifestPath{};
    std::filesystem::path reflectionProbeCapturePath{};
    std::filesystem::path reflectionProbePrefilteredPath{};
    std::filesystem::path reflectionProbeBrdfLutPath{};
    std::filesystem::path lightProbeVolumePath{};
    std::filesystem::path lightProbeDebugJsonPath{};
    std::filesystem::path lightmapPath{};
    std::string reflectionProbeCaptureMode{};
    uint64_t gpuFenceValue = 0;
    uint32_t reflectionProbeCapturedFaceCount = 0;
    uint32_t reflectionProbeQueuedReadbackFaceCount = 0;
    uint32_t reflectionProbeCaptureResolution = 0;
    uint32_t reflectionProbeCaptureMipCount = 0;
    std::string reflectionProbeCaptureFormat{};
    uint32_t reflectionProbePrefilteredMipCount = 0;
    std::string reflectionProbePrefilteredFormat{};
    uint32_t reflectionProbeRecordCount = 0;
    uint32_t lightProbeRecordCount = 0;
    uint32_t lightmapRecordCount = 0;
    uint32_t lightProbeProbeCount = 0;
    uint32_t lightProbeBuriedCount = 0; // 形状に埋まって無効にした探針
    uint32_t lightmapResolution = 0;
    // 光照贴图と探針の DXR の標本（今の項目の進み具合）。
    uint32_t transportCompletedSamples = 0;
    uint32_t transportRequestedSamples = 0;
    uint32_t lightmapCoveredTexelCount = 0;
    uint32_t lightmapOverlapTexelCount = 0;
    uint32_t bakeItemCount = 0;
    uint32_t dirtyItemCount = 0;
    uint32_t reusedItemCount = 0;
    uint32_t completedItemCount = 0;
    std::string currentItemName{};
    std::string lightingTransportBackend{};
    std::vector<std::string> dirtyReasons{};
    std::vector<std::string> reflectionProbeFaceSummaries{};
    std::vector<std::string> lightProbeMessages{};
    std::vector<std::string> messages{};
    std::vector<std::string> warnings{};
    std::vector<std::string> errors{};
};

inline const char* ToString(LightingBakeTarget target) {
    switch (target) {
    case LightingBakeTarget::All: return "All";
    case LightingBakeTarget::ReflectionProbesOnly: return "ReflectionProbesOnly";
    case LightingBakeTarget::LightProbesOnly: return "LightProbesOnly";
    case LightingBakeTarget::LightmapsOnly: return "LightmapsOnly";
    default: return "Unknown";
    }
}

inline const char* ToString(LightingBakeAction action) {
    switch (action) {
    case LightingBakeAction::ClearBake: return "ClearBake";
    case LightingBakeAction::BakeReflectionProbes: return "BakeReflectionProbes";
    case LightingBakeAction::BakeLightProbes: return "BakeLightProbes";
    case LightingBakeAction::BakeLightmaps: return "BakeLightmaps";
    case LightingBakeAction::BakeLighting: return "BakeLighting";
    default: return "Unknown";
    }
}

inline const char* ToString(LightingBakeJobState state) {
    switch (state) {
    case LightingBakeJobState::Idle: return "Idle";
    case LightingBakeJobState::Requested: return "Requested";
    case LightingBakeJobState::Capturing: return "Capturing";
    case LightingBakeJobState::Tracing: return "Tracing";
    case LightingBakeJobState::WaitingGpu: return "WaitingGpu";
    case LightingBakeJobState::ProjectingSH: return "ProjectingSH";
    case LightingBakeJobState::Saving: return "Saving";
    case LightingBakeJobState::Finalizing: return "Finalizing";
    case LightingBakeJobState::Completed: return "Completed";
    case LightingBakeJobState::Failed: return "Failed";
    default: return "Unknown";
    }
}

} // namespace HIKARI::TOOLS::BAKING
