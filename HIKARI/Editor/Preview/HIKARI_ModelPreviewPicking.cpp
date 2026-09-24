#include "Editor/Preview/HIKARI_ModelPreviewPicking.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Editor/Preview/HIKARI_ModelPreviewScene.h"
#include "Render3D/Core/HIKARI_Camera3D.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::EDITOR {
namespace {

MATH::Vec3 TransformPoint(const MATH::Mat4& transform, const MATH::Vec3& point) noexcept {
    const MATH::Vec4 result = transform.TransformPoint({point.x, point.y, point.z, 1.0f});
    if (std::abs(result.w) <= 1.0e-7f) {
        return {result.x, result.y, result.z};
    }
    const float inverseW = 1.0f / result.w;
    return {result.x * inverseW, result.y * inverseW, result.z * inverseW};
}

bool IntersectTriangle(const EditorPointerRay& ray, const MATH::Vec3& a, const MATH::Vec3& b, const MATH::Vec3& c,
                       float& outDistance) noexcept {
    const MATH::Vec3 edgeA = b - a;
    const MATH::Vec3 edgeB = c - a;
    const MATH::Vec3 p = MATH::Cross(ray.direction, edgeB);
    const float determinant = MATH::Dot(edgeA, p);
    if (std::abs(determinant) <= 1.0e-7f) {
        return false;
    }
    const float inverseDeterminant = 1.0f / determinant;
    const MATH::Vec3 fromA = ray.origin - a;
    const float u = MATH::Dot(fromA, p) * inverseDeterminant;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    const MATH::Vec3 q = MATH::Cross(fromA, edgeA);
    const float v = MATH::Dot(ray.direction, q) * inverseDeterminant;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    const float distance = MATH::Dot(edgeB, q) * inverseDeterminant;
    if (!std::isfinite(distance) || distance < 0.0f) {
        return false;
    }
    outDistance = distance;
    return true;
}

} // namespace

ModelPreviewSurfacePick PickModelPreviewSurface(const EditorPointerRay& ray, const ModelPreviewScene& scene) noexcept {
    ModelPreviewSurfacePick result{};
    const ModelAsset* model = scene.GetModel();
    if (model == nullptr) {
        return result;
    }
    float bestDistance = (std::numeric_limits<float>::max)();
    for (const ModelPreviewNode& node : scene.GetSourceNodes()) {
        float boundsDistance = 0;
        if (!IntersectEditorBounds(ray, node.bounds.min, node.bounds.max, boundsDistance) || node.meshIndex < 0 ||
            static_cast<size_t>(node.meshIndex) >= model->meshes.size()) {
            continue;
        }
        const MeshAsset& mesh = model->meshes[node.meshIndex];
        for (size_t primitiveIndex = 0u; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
            const MeshPrimitive& primitive = mesh.primitives[primitiveIndex];
            const bool skinned = primitive.layout == VertexLayoutKind::SkinnedPNTTJW;
            const size_t vertexCount = skinned ? primitive.skinnedVertices.size() : primitive.staticVertices.size();
            const size_t indexCount = primitive.indices.empty()
                                          ? vertexCount - vertexCount % 3u
                                          : primitive.indices.size() - primitive.indices.size() % 3u;
            for (size_t base = 0u; base < indexCount; base += 3u) {
                const auto indexAt = [&](size_t offset) {
                    return primitive.indices.empty() ? static_cast<uint32_t>(base + offset)
                                                     : primitive.indices[base + offset];
                };
                const uint32_t ia = indexAt(0u);
                const uint32_t ib = indexAt(1u);
                const uint32_t ic = indexAt(2u);
                if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount) {
                    continue;
                }
                const auto position = [&](uint32_t index) {
                    return skinned ? primitive.skinnedVertices[index].position
                                   : primitive.staticVertices[index].position;
                };
                float distance = 0.0f;
                if (IntersectTriangle(ray, TransformPoint(node.globalTransform, position(ia)),
                                      TransformPoint(node.globalTransform, position(ib)),
                                      TransformPoint(node.globalTransform, position(ic)), distance) &&
                    distance < bestDistance) {
                    bestDistance = distance;
                    result.materialIndex = primitive.materialIndex;
                    result.nodeIndex = node.nodeIndex;
                    result.meshIndex = node.meshIndex;
                    result.primitiveIndex = static_cast<uint32_t>(primitiveIndex);
                    result.distance = distance;
                    result.valid = true;
                }
            }
        }
    }
    return result;
}

} // namespace HIKARI::EDITOR
