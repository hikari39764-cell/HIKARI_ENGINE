#pragma once

#include <algorithm>
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI::RENDER3D::PARTICLES {
    inline constexpr uint32_t kMaximumParticleShapeSegments = 64u;

    // 球/半球の分割数。メッシュシェーダの 256 図元上限から決まる上限を持ち、
    // シェーダ側 ParticleSphereSegments と同じ規則でなければ添字が合わない。
    inline uint32_t ParticleSphereSegments(uint32_t segments) noexcept {
        return std::clamp(segments, 8u, 16u);
    }

    // AS/MS と VS fallback が共有する頂点数契約。実際の形状生成は GPU のみ。
    inline uint32_t ParticleGeometrySegments(const ParticleEmitterRenderParameters& render) noexcept {
        if (render.shape == ParticleRenderShape::Ribbon)
            return std::clamp(render.ribbonSegmentCount, 2u, kMaximumParticleShapeSegments);
        if (render.shape == ParticleRenderShape::Ring ||
            render.shape == ParticleRenderShape::MeshCylinder ||
            render.shape == ParticleRenderShape::MeshCone ||
            render.shape == ParticleRenderShape::MeshSphere ||
            render.shape == ParticleRenderShape::MeshHemisphere ||
            render.shape == ParticleRenderShape::MeshTorus ||
            render.shape == ParticleRenderShape::MeshCapsule)
            return std::clamp(render.ringSegmentCount, 3u, kMaximumParticleShapeSegments);
        return 1u;
    }

    inline uint32_t ParticleGeometryTriangleCount(const ParticleEmitterRenderParameters& render) noexcept {
        if (!render.spatialEffects.empty()) {
            if (render.shape==ParticleRenderShape::MeshBox) return 192u;
            if (render.shape==ParticleRenderShape::CrossPlane) return 256u;
            if (render.shape==ParticleRenderShape::Sprite || render.shape==ParticleRenderShape::HistoryRibbon) return 128u;
        }
        switch (render.shape) {
        case ParticleRenderShape::MeshBox: return 12u;
        case ParticleRenderShape::MeshSphere:
        case ParticleRenderShape::MeshHemisphere: {
            const uint32_t s =
                ParticleSphereSegments(ParticleGeometrySegments(render));
            return s * (s / 2u) * 2u;
        }
        case ParticleRenderShape::MeshTorus: {
            const uint32_t s = std::clamp(ParticleGeometrySegments(render) / 2u, 8u, 16u);
            return s * (s / 2u) * 2u;
        }
        case ParticleRenderShape::MeshCapsule:
            return std::clamp(ParticleGeometrySegments(render) / 2u, 8u, 12u) * 20u;
        case ParticleRenderShape::CrossPlane: return 4u;
        case ParticleRenderShape::MeshCone:
        case ParticleRenderShape::MeshCylinder: {
            const uint32_t s = std::clamp(ParticleGeometrySegments(render), 8u, 32u);
            const uint32_t rows = 128u / s;
            return s * (rows * 2u - (render.shape == ParticleRenderShape::MeshCone ? 1u : 0u));
        }
        case ParticleRenderShape::Ribbon:
        case ParticleRenderShape::Ring: return ParticleGeometrySegments(render) * 2u;
        default: return 2u;
        }
    }
}
