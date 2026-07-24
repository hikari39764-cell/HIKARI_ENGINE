#pragma once

#include <array>
#include <cstddef>

#include "Render3D/GpuDriven/HIKARI_GpuCommandBuildResult.h"
#include "Render3D/GpuDriven/Backend/HIKARI_GeometryBackendContext.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenDrawCommandStream.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenStats.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneFrame.h"
#include "Render3D/GpuDriven/HIKARI_GpuVisibilityResult.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    enum class GpuDrivenPassSourceMode {
        None,
        Own,
        Derived,
    };

    struct GpuDrivenBackendAvailability {
        bool meshShaderForwardPipelineReady = false;
        bool meshShaderGeometryAuxPipelineReady = false;
        bool traditionalIndirectPipelineReady = false;
    };

    struct GpuDrivenPassExecutionState {
        GpuDrivenPassKind pass = GpuDrivenPassKind::ForwardOpaque;
        GpuDrivenPassKind sourcePass = GpuDrivenPassKind::ForwardOpaque;
        GpuDrivenPassKind visibilityPass = GpuDrivenPassKind::ForwardOpaque;
        GpuDrivenPassSourceMode sourceMode = GpuDrivenPassSourceMode::None;

        bool hasSource = false;
        bool clusterEligible = false;
        bool hasTraditionalIndirectCommands = false;
        bool visibilityReady = false;
        bool commandBuildReady = false;
        bool hasDrawSeeds = false;
        bool overflowBlocked = false;
        bool gpuBackendReady = false;

        bool meshShaderConsumable = false;
        bool traditionalIndirectConsumable = false;

        size_t sourceInstanceCount = 0;
        size_t drawSeedCount = 0;
        size_t visibleCommandCount = 0;
        size_t visibleCommandOverflowCount = 0;
        size_t gpuCommandBucketCapacity = 0;
        size_t traditionalIndirectCommandCount = 0;
        size_t traditionalIndirectInstanceCount = 0;
        bool gpuCommandCounterBacked = false;
        bool visibleCommandCountKnown = false;
    };

    struct GpuDrivenWorkOwnershipStats {
        size_t ownedCommandCount = 0;
        size_t ownedRecordCount = 0;
        size_t geometryAuxCommandCount = 0;
        size_t geometryAuxRecordCount = 0;

        size_t eligibleCommandCount = 0;
        size_t rejectContextCommandCount = 0;
        size_t rejectMainlineCommandCount = 0;
        size_t rejectBackendCommandCount = 0;
        size_t rejectTransparentCommandCount = 0;
        size_t rejectMaterialFxCommandCount = 0;
        size_t rejectRangeCommandCount = 0;
        size_t rejectInstanceResourceCommandCount = 0;
        size_t rejectInstanceFlagCommandCount = 0;
        size_t rejectMaterialPatchCommandCount = 0;
    };

    struct GpuDrivenFrameContext {
        GpuSceneFrame scene{};
        GpuVisibilityResult visibility{};
        GpuCommandBuildResult commands{};
        GpuDrivenDrawCommandStream drawStream{};
        GpuDrivenStats stats{};
        GpuDrivenBackendAvailability backendAvailability{};
        std::array<GpuDrivenPassExecutionState, kGpuDrivenPassCount> passExecution{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
