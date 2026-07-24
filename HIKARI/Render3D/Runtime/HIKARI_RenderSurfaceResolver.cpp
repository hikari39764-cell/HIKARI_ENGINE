#include "Render3D/Runtime/HIKARI_RenderSurfaceResolver.h"

#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::RENDER3D::RUNTIME {

    namespace {
        MATH::Mat4 BuildNodeGlobalRecursive(
            const RenderModelAsset& renderModel,
            size_t nodeIndex,
            std::vector<MATH::Mat4>& globals,
            std::vector<uint8_t>& visited) {

            if (nodeIndex >= renderModel.nodes.size()) {
                return MATH::Mat4::Identity();
            }
            if (visited[nodeIndex]) {
                return globals[nodeIndex];
            }

            const RenderModelNodeRecord& node = renderModel.nodes[nodeIndex];
            MATH::Mat4 parent = MATH::Mat4::Identity();
            if (node.parentIndex >= 0 && node.parentIndex < static_cast<int>(renderModel.nodes.size())) {
                parent = BuildNodeGlobalRecursive(renderModel, static_cast<size_t>(node.parentIndex), globals, visited);
            }

            globals[nodeIndex] = parent * node.localMatrix;
            visited[nodeIndex] = 1u;
            return globals[nodeIndex];
        }

        MATH::Mat4 ResolveAnimatedRenderNodeLocalMatrix(
            const RenderModelNodeRecord& renderNode,
            const ModelAsset* model,
            const ANIMATION::AnimationLocalPose& pose) {

            if (model == nullptr || renderNode.nodeIndex >= model->nodes.size()) {
                return renderNode.localMatrix;
            }

            const ModelNode& sourceNode = model->nodes[renderNode.nodeIndex];
            if (sourceNode.hasLocalMatrix) {
                return sourceNode.localMatrix;
            }
            if (renderNode.nodeIndex < pose.nodes.size()) {
                return pose.nodes[renderNode.nodeIndex].GetLocalMatrix();
            }
            return sourceNode.localTransform.GetLocalMatrix();
        }

        MATH::Mat4 BuildNodeGlobalRecursive(
            const RenderModelAsset& renderModel,
            const std::vector<MATH::Mat4>& localMatrices,
            size_t nodeIndex,
            std::vector<MATH::Mat4>& globals,
            std::vector<uint8_t>& visited) {

            if (nodeIndex >= renderModel.nodes.size()) {
                return MATH::Mat4::Identity();
            }
            if (visited[nodeIndex]) {
                return globals[nodeIndex];
            }

            const RenderModelNodeRecord& node = renderModel.nodes[nodeIndex];
            MATH::Mat4 parent = MATH::Mat4::Identity();
            if (node.parentIndex >= 0 && node.parentIndex < static_cast<int>(renderModel.nodes.size())) {
                parent = BuildNodeGlobalRecursive(
                    renderModel,
                    localMatrices,
                    static_cast<size_t>(node.parentIndex),
                    globals,
                    visited);
            }

            globals[nodeIndex] = parent *
                (nodeIndex < localMatrices.size()
                    ? localMatrices[nodeIndex]
                    : node.localMatrix);
            visited[nodeIndex] = 1u;
            return globals[nodeIndex];
        }
    }

    std::vector<MATH::Mat4> BuildRenderModelNodeGlobals(const RenderModelAsset& renderModel) {
        std::vector<MATH::Mat4> globals(renderModel.nodes.size(), MATH::Mat4::Identity());
        std::vector<uint8_t> visited(renderModel.nodes.size(), 0u);
        for (size_t nodeIndex = 0; nodeIndex < renderModel.nodes.size(); ++nodeIndex) {
            (void)BuildNodeGlobalRecursive(renderModel, nodeIndex, globals, visited);
        }
        return globals;
    }

    std::vector<MATH::Mat4> BuildRenderModelNodeGlobals(
        const RenderModelAsset& renderModel,
        const ModelAsset* model,
        const ANIMATION::AnimationLocalPose& pose) {

        if (model == nullptr ||
            !pose.IsValidFor(model->nodes.size()) ||
            renderModel.nodes.empty()) {
            return BuildRenderModelNodeGlobals(renderModel);
        }

        std::vector<MATH::Mat4> localMatrices(
            renderModel.nodes.size(),
            MATH::Mat4::Identity());
        for (size_t nodeIndex = 0; nodeIndex < renderModel.nodes.size(); ++nodeIndex) {
            localMatrices[nodeIndex] =
                ResolveAnimatedRenderNodeLocalMatrix(
                    renderModel.nodes[nodeIndex],
                    model,
                    pose);
        }

        std::vector<MATH::Mat4> globals(renderModel.nodes.size(), MATH::Mat4::Identity());
        std::vector<uint8_t> visited(renderModel.nodes.size(), 0u);
        for (size_t nodeIndex = 0; nodeIndex < renderModel.nodes.size(); ++nodeIndex) {
            (void)BuildNodeGlobalRecursive(
                renderModel,
                localMatrices,
                nodeIndex,
                globals,
                visited);
        }
        return globals;
    }

    bool ResolveRenderSurfaceDrawWorldMatrix(
        const Transform3D& objectWorldTransform,
        const RenderSurfaceRecord& surface,
        const std::vector<MATH::Mat4>& nodeGlobals,
        MATH::Mat4& outDrawWorldMatrix) {

        outDrawWorldMatrix = objectWorldTransform.GetWorldMatrix();
        if (surface.nodeIndex == kInvalidRenderModelIndex) {
            return true;
        }
        if (surface.nodeIndex >= nodeGlobals.size()) {
            return false;
        }

        outDrawWorldMatrix = outDrawWorldMatrix * nodeGlobals[surface.nodeIndex];
        return true;
    }

    Bounds ResolveRenderSurfaceWorldBounds(
        const Bounds& fallbackWorldBounds,
        const RenderSurfaceRecord& surface,
        const MATH::Mat4& drawWorldMatrix,
        bool hasDrawWorldMatrix) {

        if (!hasDrawWorldMatrix || !BOUNDS::IsUsable(surface.localBounds)) {
            return fallbackWorldBounds;
        }

        const Bounds worldBounds = BOUNDS::TransformBounds(surface.localBounds, drawWorldMatrix);
        return BOUNDS::IsUsable(worldBounds) ? worldBounds : fallbackWorldBounds;
    }

    bool HasValidRenderSurfacePrimitive(
        const ModelAsset* model,
        const RenderSurfaceRecord& surface) {

        if (model == nullptr || !surface.HasMeshPrimitive() || surface.meshIndex >= model->meshes.size()) {
            return false;
        }

        const MeshAsset& mesh = model->meshes[surface.meshIndex];
        return surface.primitiveIndex < mesh.primitives.size();
    }

} // namespace HIKARI::RENDER3D::RUNTIME
