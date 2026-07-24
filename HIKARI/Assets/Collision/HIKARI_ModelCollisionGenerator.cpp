#include "Assets/Collision/HIKARI_ModelCollisionGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "Assets/Collision/HIKARI_CoacdCollisionGenerator.h"
#include "Assets/Collision/HIKARI_CollisionPrimitiveFitter.h"
#include "Assets/Collision/HIKARI_ModelCollisionMeshExtraction.h"
#include "Assets/Collision/HIKARI_ModelCollisionGrouping.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::COLLISION {
    namespace {
        MATH::Vec3 BoundsCenter(const Bounds& bounds) noexcept {
            return (bounds.min + bounds.max) * 0.5f;
        }

        MATH::Vec3 BoundsSize(const Bounds& bounds) noexcept {
            return bounds.max - bounds.min;
        }

        float PrimitiveVolume(const ModelCollisionShape& shape) noexcept {
            constexpr float kPi = 3.14159265358979323846f;
            switch (shape.type) {
            case CollisionGeometryShapeType::Sphere:
                return 4.0f / 3.0f * kPi *
                    shape.radius * shape.radius * shape.radius;
            case CollisionGeometryShapeType::Capsule: {
                const float cylinderHeight = (std::max)(
                    0.0f,
                    shape.height - shape.radius * 2.0f);
                return kPi * shape.radius * shape.radius * cylinderHeight +
                    4.0f / 3.0f * kPi *
                        shape.radius * shape.radius * shape.radius;
            }
            case CollisionGeometryShapeType::Box:
            default:
                return shape.size.x * shape.size.y * shape.size.z;
            }
        }

        std::string ShapeName(
            std::string_view prefix,
            uint64_t id) {

            return std::string(prefix) + " " + std::to_string(id);
        }

        void FitCapsuleToBounds(
            const Bounds& bounds,
            ModelCollisionShape& shape) noexcept {

            const MATH::Vec3 size = BoundsSize(bounds);
            const float largest = (std::max)({ size.x, size.y, size.z });
            if (largest == size.x) {
                shape.rotationEulerDegrees = { 0.0f, 0.0f, -90.0f };
                shape.radius = (std::max)(size.y, size.z) * 0.5f;
            } else if (largest == size.z) {
                shape.rotationEulerDegrees = { 90.0f, 0.0f, 0.0f };
                shape.radius = (std::max)(size.x, size.y) * 0.5f;
            } else {
                shape.rotationEulerDegrees = {};
                shape.radius = (std::max)(size.x, size.z) * 0.5f;
            }
            shape.radius = (std::max)(shape.radius, 0.001f);
            shape.height = (std::max)(largest, shape.radius * 2.0f);
        }

        const char* MethodName(
            ModelCollisionGenerationMethod method) noexcept {

            switch (method) {
            case ModelCollisionGenerationMethod::Sphere:
                return "Sphere";
            case ModelCollisionGenerationMethod::Capsule:
                return "Capsule";
            case ModelCollisionGenerationMethod::ConvexHull:
                return "Convex Hull";
            case ModelCollisionGenerationMethod::ConvexDecomposition:
                return "Convex Part";
            case ModelCollisionGenerationMethod::TriangleMesh:
                return "Static Mesh";
            case ModelCollisionGenerationMethod::Box:
            default:
                return "Box";
            }
        }

        CollisionGeometryShapeType PrimitiveType(
            ModelCollisionGenerationMethod method) noexcept {

            switch (method) {
            case ModelCollisionGenerationMethod::Sphere:
                return CollisionGeometryShapeType::Sphere;
            case ModelCollisionGenerationMethod::Capsule:
                return CollisionGeometryShapeType::Capsule;
            case ModelCollisionGenerationMethod::Box:
            default:
                return CollisionGeometryShapeType::Box;
            }
        }

        bool IsPrimitiveMethod(
            ModelCollisionGenerationMethod method) noexcept {

            return method == ModelCollisionGenerationMethod::Box ||
                method == ModelCollisionGenerationMethod::Sphere ||
                method == ModelCollisionGenerationMethod::Capsule;
        }

        ModelCollisionShape MakeGeometryShape(
            ModelCollisionSetup& setup,
            CollisionGeometryShapeType type,
            ModelCollisionMeshData mesh,
            std::string name,
            std::string generationMethod,
            float generationError) {

            ModelCollisionShape shape{};
            shape.id = setup.AllocateShapeId();
            shape.name = std::move(name);
            shape.type = type;
            shape.generated = true;
            shape.sourceNodeIndices = std::move(mesh.sourceNodeIndices);
            shape.vertices = std::move(mesh.vertices);
            shape.indices = std::move(mesh.indices);
            shape.generationMethod = std::move(generationMethod);
            shape.generationError = generationError;
            return shape;
        }

        bool GenerateGroupShapes(
            const ModelCollisionMeshData& mesh,
            const ModelCollisionGenerationRequest& request,
            float sourceBoundsVolume,
            ModelCollisionSetup& setup,
            std::vector<ModelCollisionShape>& outShapes,
            std::string& outMessage) {

            if (IsPrimitiveMethod(request.method)) {
                ModelCollisionShape shape{};
                shape.id = setup.AllocateShapeId();
                shape.type = PrimitiveType(request.method);
                shape.name = "Auto " + std::string(MethodName(request.method));
                shape.generated = true;
                shape.sourceNodeIndices = mesh.sourceNodeIndices;
                shape.generationMethod = "PrimitiveFit";
                if (!FitCollisionPrimitive(
                        mesh,
                        shape.type,
                        shape,
                        shape.generationError,
                        outMessage)) {
                    return false;
                }
                if (sourceBoundsVolume > 1.0e-8f) {
                    shape.generationError = (std::max)(
                        0.0f,
                        PrimitiveVolume(shape) / sourceBoundsVolume - 1.0f);
                }
                outShapes.push_back(std::move(shape));
                return true;
            }
            if (request.method ==
                    ModelCollisionGenerationMethod::TriangleMesh) {
                const uint64_t triangleCount = mesh.indices.size() / 3u;
                if (triangleCount > request.maximumTriangleCount) {
                    outMessage = "static mesh collision has " +
                        std::to_string(triangleCount) +
                        " triangles; select smaller parts or raise the triangle budget";
                    return false;
                }
                outShapes.push_back(MakeGeometryShape(
                    setup,
                    CollisionGeometryShapeType::TriangleMesh,
                    mesh,
                    "Auto Static Mesh",
                    "TriangleMesh",
                    0.0f));
                return true;
            }

            CoacdCollisionSettings settings{};
            settings.threshold = (std::clamp)(
                static_cast<double>(request.accuracy),
                0.001,
                1.0);
            settings.maximumConvexHulls = request.method ==
                    ModelCollisionGenerationMethod::ConvexHull
                ? 1
                : static_cast<int>((std::min)(
                    request.maximumGeneratedShapes,
                    static_cast<uint32_t>((std::numeric_limits<int>::max)())));
            settings.maximumHullVertices = static_cast<int>(
                request.maximumHullVertices);
            settings.mergeParts = !request.preserveGaps;

            std::vector<ModelCollisionMeshData> convexParts{};
            if (!GenerateCoacdCollisionParts(
                    mesh,
                    settings,
                    convexParts,
                    outMessage)) {
                if (request.method ==
                        ModelCollisionGenerationMethod::ConvexDecomposition) {
                    return false;
                }
                outMessage =
                    "single convex hull generation failed; no approximate point-sampling fallback was used because it could under-cover the source mesh";
                return false;
            }
            for (size_t partIndex = 0u;
                partIndex < convexParts.size();
                ++partIndex) {
                outShapes.push_back(MakeGeometryShape(
                    setup,
                    CollisionGeometryShapeType::ConvexHull,
                    std::move(convexParts[partIndex]),
                    "Auto " + std::string(MethodName(request.method)) +
                        " " + std::to_string(partIndex + 1u),
                    request.method ==
                            ModelCollisionGenerationMethod::ConvexHull
                        ? "CoACDConvexHull"
                        : "CoACDDecomposition",
                    request.accuracy));
            }
            return true;
        }
    }

    ModelCollisionShape CreateFittedCollisionShape(
        ModelCollisionSetup& setup,
        CollisionGeometryShapeType type,
        const Bounds& bounds,
        std::string name,
        bool generated,
        std::vector<int32_t> sourceNodeIndices) {

        ModelCollisionShape shape{};
        shape.id = setup.AllocateShapeId();
        shape.type = type;
        shape.name = name.empty()
            ? ShapeName("Collision", shape.id)
            : std::move(name);
        shape.center = BoundsCenter(bounds);
        shape.size = BoundsSize(bounds);
        shape.size.x = (std::max)(shape.size.x, 0.001f);
        shape.size.y = (std::max)(shape.size.y, 0.001f);
        shape.size.z = (std::max)(shape.size.z, 0.001f);
        shape.radius = (std::max)(
            0.001f,
            MATH::Length(shape.size) * 0.5f);
        shape.height = (std::max)(shape.size.y, shape.radius * 2.0f);
        shape.generated = generated;
        shape.sourceNodeIndices = std::move(sourceNodeIndices);
        if (type == CollisionGeometryShapeType::Capsule) {
            FitCapsuleToBounds(bounds, shape);
        }
        return shape;
    }

    ModelCollisionGenerationResult GenerateModelCollisionShapes(
        const ModelAsset& model,
        const ModelCollisionGenerationRequest& request,
        ModelCollisionSetup& setup,
        const ModelCollisionGenerationControl* control) {

        ModelCollisionGenerationResult result{};
        if (control != nullptr &&
            control->IsCancellationRequested()) {
            result.message = "collision generation canceled";
            return result;
        }
        if (request.maximumGeneratedShapes == 0u) {
            result.message = "generation shape budget is zero";
            return result;
        }
        if (request.target != ModelCollisionGenerationTarget::WholeModel &&
            request.sourceNodeIndices.empty()) {
            result.message = "select one or more source model parts first";
            return result;
        }

        ModelCollisionGroupingMode groupingMode =
            ModelCollisionGroupingMode::AllCombined;
        switch (request.target) {
        case ModelCollisionGenerationTarget::SelectedNodesCombined:
            groupingMode = ModelCollisionGroupingMode::SelectedCombined;
            break;
        case ModelCollisionGenerationTarget::SelectedNodesSpatialGroups:
            groupingMode = ModelCollisionGroupingMode::SelectedSpatialGroups;
            break;
        case ModelCollisionGenerationTarget::SelectedNodesIndividually:
            groupingMode = ModelCollisionGroupingMode::SelectedIndividually;
            break;
        case ModelCollisionGenerationTarget::WholeModel:
        default:
            break;
        }
        if (control != nullptr) {
            control->ReportProgress(
                "Collecting collision source groups",
                0u,
                0u,
                false);
        }
        const std::vector<ModelCollisionSourceGroup> groups =
            BuildModelCollisionSourceGroups(
                model,
                groupingMode,
                request.sourceNodeIndices,
                request.mergeDistance);
        if (groups.empty()) {
            result.message = "no usable source model parts were found";
            return result;
        }
        result.candidateCount = static_cast<uint32_t>(groups.size());
        if (control != nullptr) {
            control->ReportProgress(
                "Preparing collision source meshes",
                0u,
                result.candidateCount);
        }
        std::vector<ModelCollisionShape> generated{};
        std::string generationMessage{};
        for (size_t groupIndex = 0u;
             groupIndex < groups.size();
             ++groupIndex) {
            const ModelCollisionSourceGroup& group =
                groups[groupIndex];
            if (control != nullptr &&
                control->IsCancellationRequested()) {
                result.message = "collision generation canceled";
                return result;
            }
            if (generated.size() >= request.maximumGeneratedShapes) {
                result.truncated = true;
                break;
            }
            if (control != nullptr) {
                std::string stage = "Fitting collision shape";
                if (request.method ==
                    ModelCollisionGenerationMethod::ConvexHull) {
                    stage = "Building convex hull";
                } else if (request.method ==
                    ModelCollisionGenerationMethod::
                        ConvexDecomposition) {
                    stage = "Running convex decomposition";
                } else if (request.method ==
                    ModelCollisionGenerationMethod::TriangleMesh) {
                    stage = "Building triangle mesh collision";
                }
                control->ReportProgress(
                    std::move(stage),
                    static_cast<uint32_t>(groupIndex),
                    result.candidateCount,
                    request.method !=
                        ModelCollisionGenerationMethod::
                            ConvexDecomposition);
            }
            ModelCollisionMeshData mesh{};
            if (!ExtractModelCollisionMesh(
                    model,
                    group.nodeIndices,
                    mesh,
                    generationMessage)) {
                result.message = generationMessage;
                return result;
            }
            const size_t before = generated.size();
            std::vector<ModelCollisionShape> groupShapes{};
            if (!GenerateGroupShapes(
                    mesh,
                    request,
                    group.sourceBoundsVolume,
                    setup,
                    groupShapes,
                    generationMessage)) {
                result.message = generationMessage;
                return result;
            }
            if (control != nullptr &&
                control->IsCancellationRequested()) {
                result.message = "collision generation canceled";
                return result;
            }
            const bool unsafePrimitiveMerge =
                request.target == ModelCollisionGenerationTarget::
                    SelectedNodesSpatialGroups &&
                IsPrimitiveMethod(request.method) &&
                group.nodeIndices.size() > 1u &&
                !groupShapes.empty() &&
                groupShapes.front().generationError > request.accuracy;
            if (unsafePrimitiveMerge) {
                groupShapes.clear();
                for (int32_t nodeIndex : group.nodeIndices) {
                    if (control != nullptr &&
                        control->IsCancellationRequested()) {
                        result.message = "collision generation canceled";
                        return result;
                    }
                    ModelCollisionMeshData nodeMesh{};
                    const std::array<int32_t, 1> node{ nodeIndex };
                    if (!ExtractModelCollisionMesh(
                            model,
                            node,
                            nodeMesh,
                            generationMessage) ||
                        !GenerateGroupShapes(
                            nodeMesh,
                            request,
                            BOUNDS::ComputeVolume(nodeMesh.bounds),
                            setup,
                            groupShapes,
                            generationMessage)) {
                        result.message = generationMessage;
                        return result;
                    }
                }
            }
            generated.insert(
                generated.end(),
                std::make_move_iterator(groupShapes.begin()),
                std::make_move_iterator(groupShapes.end()));
            if (generated.size() > request.maximumGeneratedShapes) {
                generated.resize(request.maximumGeneratedShapes);
                result.truncated = true;
            }
            if (generated.size() == before) {
                result.message = "collision generation produced no shapes";
                return result;
            }
            if (control != nullptr) {
                control->ReportProgress(
                    "Collision source group complete",
                    static_cast<uint32_t>(groupIndex + 1u),
                    result.candidateCount);
            }
        }
        if (generated.empty()) {
            result.message = "no collision shapes could be generated";
            return result;
        }
        if (request.replaceGeneratedShapes) {
            const size_t before = setup.shapes.size();
            std::erase_if(
                setup.shapes,
                [](const ModelCollisionShape& shape) {
                    return shape.generated;
                });
            result.removedGeneratedCount = static_cast<uint32_t>(
                before - setup.shapes.size());
        }
        result.generatedCount = static_cast<uint32_t>(generated.size());
        result.generatedShapeIds.reserve(generated.size());
        for (const ModelCollisionShape& shape : generated) {
            result.generatedShapeIds.push_back(shape.id);
        }
        setup.shapes.insert(
            setup.shapes.end(),
            std::make_move_iterator(generated.begin()),
            std::make_move_iterator(generated.end()));
        if (control != nullptr) {
            control->ReportProgress(
                "Validating generated collision",
                result.candidateCount,
                result.candidateCount);
        }
        std::string validationMessage{};
        if (!ValidateModelCollisionSetup(setup, validationMessage)) {
            result.message = "generated collision is invalid: " +
                validationMessage;
            return result;
        }
        result.success = true;
        result.message = "generated " +
            std::to_string(result.generatedCount) +
            " collision shape(s) from " +
            std::to_string(groups.size()) + " source group(s)";
        if (result.truncated) {
            result.message += " (limited by shape budget)";
        }
        return result;
    }

} // namespace HIKARI::ASSETS::COLLISION
