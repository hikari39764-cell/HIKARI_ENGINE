#pragma once

#include <cstdint>
#include <limits>

#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI::RENDER3D::RUNTIME {

    constexpr uint32_t kInvalidRenderSurfaceIndex = (std::numeric_limits<uint32_t>::max)();

    enum class RenderSurfaceGeometryKind {
        MeshPrimitive,
    };

    enum class RenderSurfaceSkinningMode {
        Static,
        Skinned,
    };

    // Surface は asset 内の mesh primitive と material を結ぶ最小描画単位。
    struct RenderSurfaceRecord {
        uint32_t surfaceIndex = kInvalidRenderSurfaceIndex;

        RenderSurfaceGeometryKind geometryKind = RenderSurfaceGeometryKind::MeshPrimitive;
        uint32_t nodeIndex = kInvalidRenderSurfaceIndex;
        uint32_t meshIndex = kInvalidRenderSurfaceIndex;
        uint32_t primitiveIndex = kInvalidRenderSurfaceIndex;

        uint32_t materialIndex = 0;
        Bounds localBounds{};

        RenderSurfaceSkinningMode skinningMode = RenderSurfaceSkinningMode::Static;
        int skinIndex = -1;

        bool castShadowDefault = true;
        bool receiveShadowDefault = true;

        bool HasNodeBinding() const {
            return nodeIndex != kInvalidRenderSurfaceIndex;
        }

        bool HasMeshPrimitive() const {
            return
                geometryKind == RenderSurfaceGeometryKind::MeshPrimitive &&
                meshIndex != kInvalidRenderSurfaceIndex &&
                primitiveIndex != kInvalidRenderSurfaceIndex;
        }

        bool IsSkinned() const {
            return skinningMode == RenderSurfaceSkinningMode::Skinned;
        }
    };

} // namespace HIKARI::RENDER3D::RUNTIME
