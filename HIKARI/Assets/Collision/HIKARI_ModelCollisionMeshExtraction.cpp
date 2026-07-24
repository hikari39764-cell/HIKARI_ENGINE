#include "Assets/Collision/HIKARI_ModelCollisionMeshExtraction.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::COLLISION {
    namespace {
        MATH::Vec3 TransformPoint(
            const MATH::Mat4& transform,
            const MATH::Vec3& point) noexcept {

            const MATH::Vec4 value = transform.TransformPoint({
                point.x, point.y, point.z, 1.0f
            });
            return { value.x, value.y, value.z };
        }

        bool AppendPrimitive(
            const MeshPrimitive& primitive,
            const MATH::Mat4& transform,
            ModelCollisionMeshData& mesh) {

            const size_t vertexCount = primitive.layout ==
                    VertexLayoutKind::SkinnedPNTTJW
                ? primitive.skinnedVertices.size()
                : primitive.staticVertices.size();
            if (vertexCount < 3u ||
                mesh.vertices.size() + vertexCount >
                    (std::numeric_limits<uint32_t>::max)()) {
                return false;
            }
            const uint32_t baseVertex = static_cast<uint32_t>(
                mesh.vertices.size());
            mesh.vertices.reserve(mesh.vertices.size() + vertexCount);
            if (primitive.layout == VertexLayoutKind::SkinnedPNTTJW) {
                for (const SkinnedVertex3D& vertex :
                        primitive.skinnedVertices) {
                    mesh.vertices.push_back(TransformPoint(
                        transform,
                        vertex.position));
                }
            } else {
                for (const Vertex3D& vertex : primitive.staticVertices) {
                    mesh.vertices.push_back(TransformPoint(
                        transform,
                        vertex.position));
                }
            }

            if (!primitive.indices.empty()) {
                const size_t triangleIndexCount =
                    primitive.indices.size() - primitive.indices.size() % 3u;
                for (size_t index = 0u;
                    index < triangleIndexCount;
                    index += 3u) {
                    const uint32_t first = primitive.indices[index];
                    const uint32_t second = primitive.indices[index + 1u];
                    const uint32_t third = primitive.indices[index + 2u];
                    if (first >= vertexCount ||
                        second >= vertexCount ||
                        third >= vertexCount ||
                        first == second || second == third || first == third) {
                        continue;
                    }
                    mesh.indices.push_back(baseVertex + first);
                    mesh.indices.push_back(baseVertex + second);
                    mesh.indices.push_back(baseVertex + third);
                }
            } else {
                const size_t triangleVertexCount = vertexCount -
                    vertexCount % 3u;
                for (size_t index = 0u;
                    index < triangleVertexCount;
                    index += 3u) {
                    mesh.indices.push_back(baseVertex +
                        static_cast<uint32_t>(index));
                    mesh.indices.push_back(baseVertex +
                        static_cast<uint32_t>(index + 1u));
                    mesh.indices.push_back(baseVertex +
                        static_cast<uint32_t>(index + 2u));
                }
            }
            return true;
        }
    }

    bool ModelCollisionMeshData::IsUsable() const noexcept {
        return vertices.size() >= 3u &&
            indices.size() >= 3u &&
            indices.size() % 3u == 0u &&
            BOUNDS::IsUsable(bounds);
    }

    std::vector<int32_t> CollectRenderableModelNodeIndices(
        const ModelAsset& model) {

        std::vector<int32_t> result{};
        result.reserve(model.nodes.size());
        for (size_t index = 0u; index < model.nodes.size(); ++index) {
            const int meshIndex = model.nodes[index].meshIndex;
            if (meshIndex >= 0 &&
                meshIndex < static_cast<int>(model.meshes.size())) {
                result.push_back(static_cast<int32_t>(index));
            }
        }
        return result;
    }

    bool ExtractModelCollisionMesh(
        const ModelAsset& model,
        std::span<const int32_t> sourceNodeIndices,
        ModelCollisionMeshData& outMesh,
        std::string& outMessage) {

        outMesh = {};
        const std::vector<int32_t> allNodes = sourceNodeIndices.empty()
            ? CollectRenderableModelNodeIndices(model)
            : std::vector<int32_t>(
                sourceNodeIndices.begin(),
                sourceNodeIndices.end());
        if (allNodes.empty()) {
            outMessage = "no renderable model parts were selected";
            return false;
        }
        std::unordered_set<int32_t> uniqueNodes{};
        const std::vector<MATH::Mat4> globals =
            BOUNDS::BuildModelNodeGlobals(model);
        for (int32_t nodeIndex : allNodes) {
            if (nodeIndex < 0 ||
                nodeIndex >= static_cast<int32_t>(model.nodes.size()) ||
                !uniqueNodes.insert(nodeIndex).second) {
                continue;
            }
            const ModelNode& node = model.nodes[
                static_cast<size_t>(nodeIndex)];
            if (node.meshIndex < 0 ||
                node.meshIndex >= static_cast<int>(model.meshes.size())) {
                continue;
            }
            const MeshAsset& sourceMesh = model.meshes[
                static_cast<size_t>(node.meshIndex)];
            const size_t indexCountBefore = outMesh.indices.size();
            for (const MeshPrimitive& primitive : sourceMesh.primitives) {
                (void)AppendPrimitive(
                    primitive,
                    globals[static_cast<size_t>(nodeIndex)],
                    outMesh);
            }
            if (outMesh.indices.size() != indexCountBefore) {
                outMesh.sourceNodeIndices.push_back(nodeIndex);
            }
        }

        Bounds bounds = BOUNDS::EmptyBounds();
        for (const MATH::Vec3& vertex : outMesh.vertices) {
            BOUNDS::Encapsulate(bounds, vertex);
        }
        outMesh.bounds = bounds;
        if (!outMesh.IsUsable()) {
            outMessage = "selected model parts contain no usable triangles";
            outMesh = {};
            return false;
        }
        outMessage.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::COLLISION
