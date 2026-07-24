#include "Render3D/Resources/HIKARI_RenderResourceSystem.h"

#include <utility>

namespace HIKARI::RENDER3D {

    namespace {

        struct RenderResourceSystemState {
            RenderResourcePool pool{};
        };

        RenderResourceSystemState& State() {
            static RenderResourceSystemState state{};
            return state;
        }

        RenderResourceDesc BuildVirtualDesc(
            RenderResourceKind kind,
            RenderResourceUsageFlags usage,
            const std::string& sourceKey,
            const std::string& debugName) {

            RenderResourceDesc desc{};
            desc.kind = kind;
            desc.usage = usage;
            desc.lifetime = RenderResourceLifetime::ImportedAsset;
            desc.sourceKey = sourceKey;
            desc.debugName = debugName.empty() ? sourceKey : debugName;
            return desc;
        }

        RenderResourceHandle FindOrRegisterVirtualResource(
            RenderResourceKind kind,
            RenderResourceUsageFlags usage,
            const std::string& sourceKey,
            const std::string& debugName) {

            if (sourceKey.empty()) {
                return {};
            }

            RenderResourcePool& pool = GetRenderResourcePool();
            if (const RenderResourceHandle cached = pool.FindBySourceKey(kind, sourceKey)) {
                return cached;
            }

            // 実体アップロード前の mesh/material も、以後の GPU-driven 経路が参照できる安定 handle に集約する。
            return pool.RegisterVirtual(
                kind,
                BuildVirtualDesc(kind, usage, sourceKey, debugName));
        }

    } // namespace

    RenderResourcePool& GetRenderResourcePool() {
        return State().pool;
    }

    RenderResourceSystemStats GetRenderResourceSystemStats() {
        RenderResourceSystemStats stats{};
        stats.pool = GetRenderResourcePool().GetStats();
        return stats;
    }

    MeshResourceHandle RegisterVirtualMeshResource(
        const std::string& sourceKey,
        const std::string& debugName) {

        return MeshResourceHandle::FromUntyped(
            FindOrRegisterVirtualResource(
                RenderResourceKind::Mesh,
                RenderResourceUsageFlags::VertexBuffer |
                    RenderResourceUsageFlags::IndexBuffer |
                    RenderResourceUsageFlags::ShaderResource,
                sourceKey,
                debugName));
    }

    MaterialResourceHandle RegisterVirtualMaterialResource(
        const std::string& sourceKey,
        const std::string& debugName) {

        return MaterialResourceHandle::FromUntyped(
            FindOrRegisterVirtualResource(
                RenderResourceKind::Material,
                RenderResourceUsageFlags::ShaderResource,
                sourceKey,
                debugName));
    }

    ClusterGeometryResourceHandle RegisterVirtualClusterGeometryResource(
        const std::string& sourceKey,
        const std::string& debugName) {

        return ClusterGeometryResourceHandle::FromUntyped(
            FindOrRegisterVirtualResource(
                RenderResourceKind::ClusterGeometry,
                RenderResourceUsageFlags::ShaderResource |
                    RenderResourceUsageFlags::IndirectArgument,
                sourceKey,
                debugName));
    }

} // namespace HIKARI::RENDER3D
