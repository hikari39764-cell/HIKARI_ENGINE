#include "Assets/Vfx/HIKARI_VfxGeometryPresetCatalog.h"

#include <array>

namespace HIKARI::VFX {
    namespace {
        VfxRenderShapeModuleData Shape(VfxRenderShapeType type) {
            VfxRenderShapeModuleData shape{};
            shape.type = type;
            return shape;
        }

        // VfxRenderShapeModuleData は std::string を持つため constexpr 不可。
        const std::array<VfxGeometryPresetDescriptor, 18u> kPresets{ {
            // 立体。
            { "quad", "Quad", "Bodies", Shape(VfxRenderShapeType::Sprite) },
            { "disc", "Disc", "Bodies",
                [] { auto s = Shape(VfxRenderShapeType::Ring);
                     s.ringInnerRadius = 0.0f; s.ringOuterRadius = 0.5f;
                     return s; }() },
            { "box", "Box", "Bodies", Shape(VfxRenderShapeType::MeshBox) },
            { "sphere", "Sphere", "Bodies",
                Shape(VfxRenderShapeType::MeshSphere) },
            { "hemisphere", "Hemisphere", "Bodies",
                Shape(VfxRenderShapeType::MeshHemisphere) },
            { "capsule", "Capsule", "Bodies",
                [] { auto s = Shape(VfxRenderShapeType::MeshCapsule);
                     s.capsuleRadius = 0.35f; return s; }() },
            { "torus", "Torus", "Bodies",
                [] { auto s = Shape(VfxRenderShapeType::MeshTorus);
                     s.torusMajorRadius = 0.4f; s.torusMinorRadius = 0.12f;
                     return s; }() },
            { "tube", "Tube", "Bodies", Shape(VfxRenderShapeType::MeshCylinder) },
            { "ngon", "N-Gon Column", "Bodies",
                [] { auto s = Shape(VfxRenderShapeType::MeshCylinder);
                     s.ringSegmentCount = 6u; return s; }() },
            { "cone", "Cone", "Bodies", Shape(VfxRenderShapeType::MeshCone) },
            { "spike", "Spike", "Bodies",
                [] { auto s = Shape(VfxRenderShapeType::MeshCone);
                     s.coneBaseRadius = 0.35f; s.ringSegmentCount = 8u;
                     return s; }() },
            { "cross_plane", "Cross Planes", "Bodies",
                Shape(VfxRenderShapeType::CrossPlane) },
            // 帯・曲面。
            { "ribbon", "Ribbon", "Strips", Shape(VfxRenderShapeType::Ribbon) },
            { "blade", "Blade", "Strips",
                [] { auto s = Shape(VfxRenderShapeType::Ribbon);
                     s.ribbonWidth = 0.35f; s.taperStart = 1.0f;
                     s.taperEnd = 0.15f; return s; }() },
            { "scythe", "Curved Blade", "Strips",
                [] { auto s = Shape(VfxRenderShapeType::Ribbon);
                     s.ribbonWidth = 0.3f; s.taperStart = 1.0f;
                     s.taperEnd = 0.2f; s.bendAmount = 0.5f; return s; }() },
            { "helix", "Helix", "Strips",
                [] { auto s = Shape(VfxRenderShapeType::Ribbon);
                     s.ribbonWidth = 0.15f; s.twistDegrees = 720.0f;
                     s.bendAmount = 0.25f; return s; }() },
            { "arc", "Arc Band", "Strips",
                [] { auto s = Shape(VfxRenderShapeType::Ring);
                     s.ringInnerRadius = 0.8f; s.ringOuterRadius = 1.0f;
                     s.ringStartAngleDegrees = -70.0f;
                     s.ringEndAngleDegrees = 90.0f; return s; }() },
            { "beam_tube", "Tapered Beam Tube", "Strips",
                [] { auto s = Shape(VfxRenderShapeType::MeshCylinder);
                     s.cylinderTopRadius = 0.1f; s.cylinderBottomRadius = 0.5f;
                     s.taperStart = 1.0f; s.taperEnd = 0.3f; return s; }() },
        } };
    }

    std::span<const VfxGeometryPresetDescriptor>
        GetVfxGeometryPresetCatalog() noexcept {
        return kPresets;
    }

    const VfxGeometryPresetDescriptor* FindVfxGeometryPreset(
        std::string_view id) noexcept {
        for (const VfxGeometryPresetDescriptor& preset : kPresets) {
            if (preset.id == id) {
                return &preset;
            }
        }
        return nullptr;
    }

    void ApplyVfxGeometryPreset(
        const VfxGeometryPresetDescriptor& preset,
        VfxRenderShapeModuleData& outShape) noexcept {
        const std::string id = outShape.id;
        outShape = preset.shape;
        outShape.id = id;
    }

} // namespace HIKARI::VFX
