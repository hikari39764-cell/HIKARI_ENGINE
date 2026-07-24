#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::ASSETS::COLLISION {

    class ModelCollisionGenerationControl {
    public:
        using CancellationQuery = std::function<bool()>;
        using ProgressReporter = std::function<void(
            std::string,
            uint32_t,
            uint32_t,
            bool)>;

        ModelCollisionGenerationControl() = default;
        ModelCollisionGenerationControl(
            CancellationQuery cancellationQuery,
            ProgressReporter progressReporter)
            : cancellationQuery_(std::move(cancellationQuery)),
              progressReporter_(std::move(progressReporter)) {
        }

        void RequestCancel() noexcept {
            cancellationRequested_.store(
                true,
                std::memory_order_relaxed);
        }

        bool IsCancellationRequested() const noexcept {
            return cancellationRequested_.load(
                    std::memory_order_relaxed) ||
                (cancellationQuery_ && cancellationQuery_());
        }

        void ReportProgress(
            std::string stage,
            uint32_t completed,
            uint32_t total,
            bool determinate = true) const {
            if (progressReporter_) {
                progressReporter_(
                    std::move(stage),
                    completed,
                    total,
                    determinate);
            }
        }

    private:
        std::atomic_bool cancellationRequested_{ false };
        CancellationQuery cancellationQuery_{};
        ProgressReporter progressReporter_{};
    };

    enum class ModelCollisionGenerationTarget : uint8_t {
        WholeModel,
        SelectedNodesCombined,
        SelectedNodesSpatialGroups,
        SelectedNodesIndividually,
    };

    enum class ModelCollisionGenerationMethod : uint8_t {
        Box,
        Sphere,
        Capsule,
        ConvexHull,
        ConvexDecomposition,
        TriangleMesh,
    };

    struct ModelCollisionGenerationRequest {
        ModelCollisionGenerationTarget target =
            ModelCollisionGenerationTarget::WholeModel;
        ModelCollisionGenerationMethod method =
            ModelCollisionGenerationMethod::Box;
        std::vector<int32_t> sourceNodeIndices{};
        bool replaceGeneratedShapes = true;
        bool preserveGaps = true;
        float mergeDistance = 0.1f;
        float accuracy = 0.05f;
        uint32_t maximumGeneratedShapes = 512u;
        uint32_t maximumHullVertices = 128u;
        uint32_t maximumTriangleCount = 1000000u;
    };

    struct ModelCollisionGenerationResult {
        bool success = false;
        bool truncated = false;
        uint32_t candidateCount = 0u;
        uint32_t generatedCount = 0u;
        uint32_t removedGeneratedCount = 0u;
        std::vector<uint64_t> generatedShapeIds{};
        std::string message{};
    };

    ModelCollisionGenerationResult GenerateModelCollisionShapes(
        const ModelAsset& model,
        const ModelCollisionGenerationRequest& request,
        ModelCollisionSetup& setup,
        const ModelCollisionGenerationControl* control = nullptr);

    ModelCollisionShape CreateFittedCollisionShape(
        ModelCollisionSetup& setup,
        CollisionGeometryShapeType type,
        const Bounds& bounds,
        std::string name,
        bool generated,
        std::vector<int32_t> sourceNodeIndices = {});

} // namespace HIKARI::ASSETS::COLLISION
