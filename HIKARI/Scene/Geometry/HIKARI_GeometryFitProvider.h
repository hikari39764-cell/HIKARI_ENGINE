#pragma once

#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    enum class GeometryFitShape : uint8_t {
        Box,
        Sphere,
        Capsule,
    };

    struct GeometryFitDesc {
        GeometryFitShape shape = GeometryFitShape::Box;
        MATH::Vec3 center{};
        MATH::Vec3 rotationEulerDegrees{};
        MATH::Vec3 size{ 1.0f, 1.0f, 1.0f };
        float radius = 0.5f;
        float height = 1.0f;
    };

    // Components that own authoring geometry can expose a physics-neutral
    // fitting description without depending on ColliderComponent.
    class IGeometryFitProvider {
    public:
        virtual ~IGeometryFitProvider() = default;
        virtual int GetGeometryFitPriority() const noexcept {
            return 0;
        }
        virtual bool QueryGeometryFit(
            GeometryFitDesc& outFit) const noexcept = 0;
    };

} // namespace HIKARI
