#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI::BOUNDS {

    inline bool IsUsable(const Bounds& bounds) {
        if (!MATH::IsFinite(bounds.min) || !MATH::IsFinite(bounds.max)) {
            return false;
        }
        if (bounds.min.x > bounds.max.x || bounds.min.y > bounds.max.y || bounds.min.z > bounds.max.z) {
            return false;
        }

        const MATH::Vec3 extent = bounds.max - bounds.min;
        return (std::abs(extent.x) + std::abs(extent.y) + std::abs(extent.z)) > 1e-5f;
    }

    inline MATH::Vec4 ComputeCenterRadius(const Bounds& bounds) {
        const MATH::Vec3 center{
            (bounds.min.x + bounds.max.x) * 0.5f,
            (bounds.min.y + bounds.max.y) * 0.5f,
            (bounds.min.z + bounds.max.z) * 0.5f,
        };
        const MATH::Vec3 extent{
            bounds.max.x - center.x,
            bounds.max.y - center.y,
            bounds.max.z - center.z,
        };
        const float radius = std::sqrt(
            extent.x * extent.x +
            extent.y * extent.y +
            extent.z * extent.z);
        return { center.x, center.y, center.z, radius };
    }

    inline float ComputeVolume(const Bounds& bounds) noexcept {
        if (!IsUsable(bounds)) {
            return 0.0f;
        }
        const MATH::Vec3 size = bounds.max - bounds.min;
        return (std::max)(size.x, 0.0f) *
            (std::max)(size.y, 0.0f) *
            (std::max)(size.z, 0.0f);
    }

    inline Bounds EmptyBounds() {
        const float inf = std::numeric_limits<float>::infinity();
        return { { inf, inf, inf }, { -inf, -inf, -inf } };
    }

    inline void Encapsulate(Bounds& bounds, const MATH::Vec3& point) {
        bounds.min.x = (std::min)(bounds.min.x, point.x);
        bounds.min.y = (std::min)(bounds.min.y, point.y);
        bounds.min.z = (std::min)(bounds.min.z, point.z);
        bounds.max.x = (std::max)(bounds.max.x, point.x);
        bounds.max.y = (std::max)(bounds.max.y, point.y);
        bounds.max.z = (std::max)(bounds.max.z, point.z);
    }

    inline void Encapsulate(Bounds& bounds, const Bounds& other) {
        if (!IsUsable(other)) {
            return;
        }
        Encapsulate(bounds, other.min);
        Encapsulate(bounds, other.max);
    }

    inline Bounds ComputePrimitiveBounds(const MeshPrimitive& primitive) {
        Bounds bounds = EmptyBounds();
        bool hasPoint = false;
        for (const Vertex3D& vertex : primitive.staticVertices) {
            Encapsulate(bounds, vertex.position);
            hasPoint = true;
        }
        if (!hasPoint) {
            for (const SkinnedVertex3D& vertex : primitive.skinnedVertices) {
                Encapsulate(bounds, vertex.position);
                hasPoint = true;
            }
        }
        return hasPoint ? bounds : Bounds{};
    }

    inline Bounds ComputeMeshBounds(const MeshAsset& mesh) {
        Bounds bounds = EmptyBounds();
        bool hasBounds = false;
        for (const MeshPrimitive& primitive : mesh.primitives) {
            const Bounds primitiveBounds = IsUsable(primitive.bounds)
                ? primitive.bounds
                : ComputePrimitiveBounds(primitive);
            if (IsUsable(primitiveBounds)) {
                Encapsulate(bounds, primitiveBounds);
                hasBounds = true;
            }
        }
        return hasBounds ? bounds : Bounds{};
    }

    inline MATH::Mat4 GetNodeLocalMatrix(const ModelNode& node) {
        return node.hasLocalMatrix ? node.localMatrix : node.localTransform.GetLocalMatrix();
    }

    inline Bounds TransformBounds(const Bounds& bounds, const MATH::Mat4& matrix) {
        if (!IsUsable(bounds)) {
            return {};
        }

        const std::array<MATH::Vec3, 8> corners = {
            MATH::Vec3{ bounds.min.x, bounds.min.y, bounds.min.z },
            MATH::Vec3{ bounds.max.x, bounds.min.y, bounds.min.z },
            MATH::Vec3{ bounds.min.x, bounds.max.y, bounds.min.z },
            MATH::Vec3{ bounds.max.x, bounds.max.y, bounds.min.z },
            MATH::Vec3{ bounds.min.x, bounds.min.y, bounds.max.z },
            MATH::Vec3{ bounds.max.x, bounds.min.y, bounds.max.z },
            MATH::Vec3{ bounds.min.x, bounds.max.y, bounds.max.z },
            MATH::Vec3{ bounds.max.x, bounds.max.y, bounds.max.z },
        };

        Bounds out = EmptyBounds();
        for (const MATH::Vec3& corner : corners) {
            const MATH::Vec4 transformed = matrix.TransformPoint({ corner.x, corner.y, corner.z, 1.0f });
            out.min.x = (std::min)(out.min.x, transformed.x);
            out.min.y = (std::min)(out.min.y, transformed.y);
            out.min.z = (std::min)(out.min.z, transformed.z);
            out.max.x = (std::max)(out.max.x, transformed.x);
            out.max.y = (std::max)(out.max.y, transformed.y);
            out.max.z = (std::max)(out.max.z, transformed.z);
        }
        return out;
    }

    inline void BuildNodeGlobalMatricesRecursive(
        const ModelAsset& model,
        int nodeIndex,
        const MATH::Mat4& parent,
        std::vector<MATH::Mat4>& globals,
        std::vector<uint8_t>& visited) {

        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) {
            return;
        }
        const size_t index = static_cast<size_t>(nodeIndex);
        if (visited[index]) {
            return;
        }
        visited[index] = 1u;

        const ModelNode& node = model.nodes[index];
        globals[index] = parent * GetNodeLocalMatrix(node);
        for (int child : node.children) {
            BuildNodeGlobalMatricesRecursive(model, child, globals[index], globals, visited);
        }
    }

    inline std::vector<MATH::Mat4> BuildModelNodeGlobals(const ModelAsset& model) {
        std::vector<MATH::Mat4> globals(model.nodes.size(), MATH::Mat4::Identity());
        std::vector<uint8_t> visited(model.nodes.size(), 0u);
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            if (model.nodes[i].parent < 0) {
                BuildNodeGlobalMatricesRecursive(model, static_cast<int>(i), MATH::Mat4::Identity(), globals, visited);
            }
        }
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            if (!visited[i]) {
                BuildNodeGlobalMatricesRecursive(model, static_cast<int>(i), MATH::Mat4::Identity(), globals, visited);
            }
        }
        return globals;
    }

    inline Bounds ComputeModelNodeBounds(
        const ModelAsset& model,
        size_t nodeIndex,
        const std::vector<MATH::Mat4>& globals) {

        if (nodeIndex >= model.nodes.size() || nodeIndex >= globals.size()) {
            return {};
        }
        const ModelNode& node = model.nodes[nodeIndex];
        if (node.meshIndex < 0 ||
            node.meshIndex >= static_cast<int>(model.meshes.size())) {
            return {};
        }
        const MeshAsset& mesh = model.meshes[
            static_cast<size_t>(node.meshIndex)];
        const Bounds localBounds = IsUsable(mesh.bounds)
            ? mesh.bounds
            : ComputeMeshBounds(mesh);
        return TransformBounds(localBounds, globals[nodeIndex]);
    }

    inline Bounds ComputeModelBounds(const ModelAsset& model) {
        Bounds bounds = EmptyBounds();
        bool hasBounds = false;

        if (!model.nodes.empty()) {
            const std::vector<MATH::Mat4> globals = BuildModelNodeGlobals(model);
            for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
                const ModelNode& node = model.nodes[nodeIndex];
                if (node.meshIndex < 0 || node.meshIndex >= static_cast<int>(model.meshes.size())) {
                    continue;
                }
                const Bounds meshBounds = IsUsable(model.meshes[static_cast<size_t>(node.meshIndex)].bounds)
                    ? model.meshes[static_cast<size_t>(node.meshIndex)].bounds
                    : ComputeMeshBounds(model.meshes[static_cast<size_t>(node.meshIndex)]);
                const Bounds worldBounds = TransformBounds(meshBounds, globals[nodeIndex]);
                if (IsUsable(worldBounds)) {
                    Encapsulate(bounds, worldBounds);
                    hasBounds = true;
                }
            }
        }

        if (!hasBounds) {
            for (const MeshAsset& mesh : model.meshes) {
                const Bounds meshBounds = IsUsable(mesh.bounds) ? mesh.bounds : ComputeMeshBounds(mesh);
                if (IsUsable(meshBounds)) {
                    Encapsulate(bounds, meshBounds);
                    hasBounds = true;
                }
            }
        }

        return hasBounds ? bounds : Bounds{};
    }

    inline void EnsureModelBounds(ModelAsset& model) {
        for (MeshAsset& mesh : model.meshes) {
            for (MeshPrimitive& primitive : mesh.primitives) {
                if (!IsUsable(primitive.bounds)) {
                    primitive.bounds = ComputePrimitiveBounds(primitive);
                }
            }
            if (!IsUsable(mesh.bounds)) {
                mesh.bounds = ComputeMeshBounds(mesh);
            }
        }
        if (!IsUsable(model.bounds)) {
            model.bounds = ComputeModelBounds(model);
        }
    }

} // namespace HIKARI::BOUNDS
