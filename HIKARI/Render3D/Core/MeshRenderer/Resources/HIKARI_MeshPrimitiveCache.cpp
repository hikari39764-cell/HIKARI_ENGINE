#include "Render3D/Core/MeshRenderer/Resources/HIKARI_MeshPrimitiveCache.h"

#include <vector>

#include "Render3D/HIKARI_Mesh.h"
#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI::MESHRENDERER {

    namespace {
        MATH::Vec4 SanitizeTangent(const MATH::Vec4& tangent) {
            const float lenSq =
                tangent.x * tangent.x +
                tangent.y * tangent.y +
                tangent.z * tangent.z;
            if (lenSq <= 1e-8f) {
                return { 1.0f, 0.0f, 0.0f, 1.0f };
            }
            return tangent;
        }
    }

    void MeshPrimitiveCache::Clear() {
        primitiveMeshCache_.clear();
        primitiveSkinnedMeshCache_.clear();
    }

    Mesh* MeshPrimitiveCache::GetOrCreateStatic(
        ID3D12Device* device,
        const MeshPrimitive& primitive,
        MeshRendererDebugStats* stats) {
        auto found = primitiveMeshCache_.find(&primitive);
        if (found != primitiveMeshCache_.end()) {
            if (stats != nullptr) {
                ++stats->primitiveMeshCacheHitCount;
            }
            return found->second.get();
        }

        if (device == nullptr ||
            primitive.layout != VertexLayoutKind::StaticPNTT ||
            primitive.staticVertices.empty() ||
            primitive.indices.empty()) {
            return nullptr;
        }

        if (stats != nullptr) {
            ++stats->primitiveMeshCacheMissCount;
        }
        std::vector<VertexStatic3D> vertices;
        vertices.reserve(primitive.staticVertices.size());
        for (const Vertex3D& src : primitive.staticVertices) {
            VertexStatic3D dst{};
            dst.position = src.position;
            dst.normal = src.normal;
            dst.tangent = SanitizeTangent(src.tangent);
            dst.u = src.uv0.x;
            dst.v = src.uv0.y;
            dst.uv1 = src.uv1;
            vertices.push_back(dst);
        }

        auto mesh = std::make_unique<Mesh>();
        if (!mesh->CreateStatic(device, vertices, primitive.indices)) {
            return nullptr;
        }

        Mesh* raw = mesh.get();
        primitiveMeshCache_.emplace(&primitive, std::move(mesh));
        return raw;
    }

    Mesh* MeshPrimitiveCache::GetOrCreateSkinned(
        ID3D12Device* device,
        const MeshPrimitive& primitive,
        MeshRendererDebugStats* stats) {
        auto found = primitiveSkinnedMeshCache_.find(&primitive);
        if (found != primitiveSkinnedMeshCache_.end()) {
            if (stats != nullptr) {
                ++stats->primitiveSkinnedMeshCacheHitCount;
            }
            return found->second.get();
        }

        if (device == nullptr || primitive.skinnedVertices.empty() || primitive.indices.empty()) {
            return nullptr;
        }

        if (stats != nullptr) {
            ++stats->primitiveSkinnedMeshCacheMissCount;
        }
        std::vector<VertexSkinnedGpu3D> vertices;
        vertices.reserve(primitive.skinnedVertices.size());
        for (const SkinnedVertex3D& src : primitive.skinnedVertices) {
            VertexSkinnedGpu3D dst{};
            dst.position = src.position;
            dst.normal = src.normal;
            dst.tangent = src.tangent;
            dst.uv0 = src.uv0;
            dst.uv1 = src.uv1;
            dst.color0 = src.color0;
            for (size_t i = 0; i < 4; ++i) {
                dst.joints[i] = src.joints[i];
                dst.weights[i] = src.weights[i];
            }
            vertices.push_back(dst);
        }

        auto mesh = std::make_unique<Mesh>();
        if (!mesh->CreateSkinned(device, vertices, primitive.indices)) {
            return nullptr;
        }

        Mesh* raw = mesh.get();
        primitiveSkinnedMeshCache_.emplace(&primitive, std::move(mesh));
        return raw;
    }

} // namespace HIKARI::MESHRENDERER
