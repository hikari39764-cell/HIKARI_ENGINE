#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"

#include <algorithm>
#include <array>

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    namespace {
        constexpr std::array<const char*, 6> kMeshKindNames{
            "Plane", "Grid Plane", "Box", "Sphere", "Cylinder", "Capsule"
        };

        uint32_t DrawSegmentCount(
            IInspectorBuilder& builder,
            const char* label,
            uint32_t value,
            int minimum,
            int maximum) {

            int edited = static_cast<int>(value);
            if (builder.Int(label, edited)) {
                return static_cast<uint32_t>(
                    std::clamp(edited, minimum, maximum));
            }
            return value;
        }

        void DrawDimension(
            IInspectorBuilder& builder,
            const char* label,
            float& value) {

            (void)builder.FloatRange(
                label,
                value,
                0.001f,
                100000.0f,
                0.05f);
        }
    }

    void ProceduralMeshComponent::BuildInspector(
        IInspectorBuilder& builder) {
        const ProceduralMeshSettings before = settings_;
        int kind = static_cast<int>(settings_.kind);
        if (builder.Choice("Shape", kind, kMeshKindNames)) {
            settings_.kind = static_cast<ProceduralMeshKind>(
                std::clamp(
                    kind,
                    0,
                    static_cast<int>(kMeshKindNames.size() - 1)));
        }

        switch (settings_.kind) {
        case ProceduralMeshKind::Plane:
            DrawDimension(builder, "Width", settings_.width);
            DrawDimension(builder, "Depth", settings_.depth);
            break;
        case ProceduralMeshKind::GridPlane:
            DrawDimension(builder, "Width", settings_.width);
            DrawDimension(builder, "Depth", settings_.depth);
            settings_.segmentsX = DrawSegmentCount(
                builder, "Columns", settings_.segmentsX, 1, 512);
            settings_.segmentsY = DrawSegmentCount(
                builder, "Rows", settings_.segmentsY, 1, 512);
            break;
        case ProceduralMeshKind::Box:
            DrawDimension(builder, "Width", settings_.width);
            DrawDimension(builder, "Height", settings_.height);
            DrawDimension(builder, "Depth", settings_.depth);
            break;
        case ProceduralMeshKind::Sphere:
            DrawDimension(builder, "Diameter", settings_.width);
            settings_.sphereSlices = DrawSegmentCount(
                builder,
                "Radial Segments",
                settings_.sphereSlices,
                3,
                512);
            settings_.sphereStacks = DrawSegmentCount(
                builder,
                "Vertical Segments",
                settings_.sphereStacks,
                2,
                256);
            break;
        case ProceduralMeshKind::Cylinder:
            DrawDimension(builder, "Diameter", settings_.width);
            DrawDimension(builder, "Height", settings_.height);
            settings_.sphereSlices = DrawSegmentCount(
                builder,
                "Radial Segments",
                settings_.sphereSlices,
                3,
                512);
            settings_.segmentsY = DrawSegmentCount(
                builder,
                "Height Segments",
                settings_.segmentsY,
                1,
                512);
            break;
        case ProceduralMeshKind::Capsule:
            DrawDimension(builder, "Diameter", settings_.width);
            DrawDimension(builder, "Total Height", settings_.height);
            settings_.sphereSlices = DrawSegmentCount(
                builder,
                "Radial Segments",
                settings_.sphereSlices,
                3,
                512);
            settings_.sphereStacks = DrawSegmentCount(
                builder,
                "Hemisphere Segments",
                settings_.sphereStacks,
                2,
                256);
            break;
        }

        builder.Bool("Double Sided", settings_.doubleSided);
        builder.Bool("Generate Tangents", settings_.generateTangents);
        builder.Text(
            TEXT::Localize(
                "ui.inspector.procedural.materialEntryHint"));
        settings_ = SanitizeProceduralMeshSettings(settings_);
        if (!(settings_ == before)) {
            NotifyRenderStateDirty();
        }
    }

} // namespace HIKARI
