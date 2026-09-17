#pragma once

#include <algorithm>
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI::RENDER3D::PARTICLES {
    inline constexpr uint32_t kMaximumParticleShapeSegments = 64u;

    // AS/MS と VS fallback が共有する頂点数契約。実際の形状生成は GPU のみ。
    inline uint32_t ParticleGeometrySegments(const ParticleEmitterRenderParameters& render) noexcept {
        if (render.shape == ParticleRenderShape::Ribbon)
            return std::clamp(render.ribbonSegmentCount, 2u, kMaximumParticleShapeSegments);
        if (render.shape == ParticleRenderShape::Ring ||
            render.shape == ParticleRenderShape::MeshCylinder || render.shape == ParticleRenderShape::MeshCone)
            return std::clamp(render.ringSegmentCount, 3u, kMaximumParticleShapeSegments);
        return 1u;
    }

    inline uint32_t ParticleGeometryTriangleCount(const ParticleEmitterRenderParameters& render) noexcept {
        switch (render.shape) {
        case ParticleRenderShape::MeshBox: return 12u;
        case ParticleRenderShape::MeshSphere:
        case ParticleRenderShape::MeshHemisphere: return 96u;
        case ParticleRenderShape::CrossPlane: return 4u;
        case ParticleRenderShape::MeshCone: return ParticleGeometrySegments(render);
        case ParticleRenderShape::MeshCylinder:
        case ParticleRenderShape::Ribbon:
        case ParticleRenderShape::Ring: return ParticleGeometrySegments(render) * 2u;
        default: return 2u;
        }
    }
}
