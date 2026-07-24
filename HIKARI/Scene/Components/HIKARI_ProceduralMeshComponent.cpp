#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"

#include <algorithm>
#include <array>

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Scene/HIKARI_GameObject.h"

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
                return static_cast<uint32_t>(std::clamp(edited, minimum, maximum));
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

    void ProceduralMeshComponent::Serialize(nlohmann::json& out) const {
        const ProceduralMeshSettings settings =
            SanitizeProceduralMeshSettings(settings_);
        out = {
            { "kind", ToString(settings.kind) },
            { "width", settings.width },
            { "height", settings.height },
            { "depth", settings.depth },
            { "segmentsX", settings.segmentsX },
            { "segmentsY", settings.segmentsY },
            { "segmentsZ", settings.segmentsZ },
            { "sphereSlices", settings.sphereSlices },
            { "sphereStacks", settings.sphereStacks },
            { "doubleSided", settings.doubleSided },
            { "generateTangents", settings.generateTangents }
        };
    }

    void ProceduralMeshComponent::Deserialize(const nlohmann::json& in) {
        const nlohmann::json& source =
            in.contains("settings") && in["settings"].is_object()
                ? in["settings"]
                : in;
        settings_.kind = ParseProceduralMeshKind(
            source.value("kind", nlohmann::json{}), settings_.kind);
        settings_.width = source.value("width", settings_.width);
        settings_.height = source.value("height", settings_.height);
        settings_.depth = source.value("depth", settings_.depth);
        settings_.segmentsX = source.value("segmentsX", settings_.segmentsX);
        settings_.segmentsY = source.value("segmentsY", settings_.segmentsY);
        settings_.segmentsZ = source.value("segmentsZ", settings_.segmentsZ);
        settings_.sphereSlices = source.value("sphereSlices", settings_.sphereSlices);
        settings_.sphereStacks = source.value("sphereStacks", settings_.sphereStacks);
        settings_.doubleSided = source.value("doubleSided", settings_.doubleSided);
        settings_.generateTangents = source.value("generateTangents", settings_.generateTangents);
        settings_ = SanitizeProceduralMeshSettings(settings_);
        NotifyRenderStateDirty();
    }

    void ProceduralMeshComponent::BuildInspector(IInspectorBuilder& builder) {
        const ProceduralMeshSettings before = settings_;
        int kind = static_cast<int>(settings_.kind);
        if (builder.Choice("Shape", kind, kMeshKindNames)) {
            settings_.kind = static_cast<ProceduralMeshKind>(
                std::clamp(kind, 0, static_cast<int>(kMeshKindNames.size() - 1)));
        }

        switch (settings_.kind) {
        case ProceduralMeshKind::Plane:
            DrawDimension(builder, "Width", settings_.width);
            DrawDimension(builder, "Depth", settings_.depth);
            break;
        case ProceduralMeshKind::GridPlane:
            DrawDimension(builder, "Width", settings_.width);
            DrawDimension(builder, "Depth", settings_.depth);
            settings_.segmentsX = DrawSegmentCount(builder, "Columns", settings_.segmentsX, 1, 512);
            settings_.segmentsY = DrawSegmentCount(builder, "Rows", settings_.segmentsY, 1, 512);
            break;
        case ProceduralMeshKind::Box:
            DrawDimension(builder, "Width", settings_.width);
            DrawDimension(builder, "Height", settings_.height);
            DrawDimension(builder, "Depth", settings_.depth);
            break;
        case ProceduralMeshKind::Sphere:
            DrawDimension(builder, "Diameter", settings_.width);
            settings_.sphereSlices = DrawSegmentCount(builder, "Radial Segments", settings_.sphereSlices, 3, 512);
            settings_.sphereStacks = DrawSegmentCount(builder, "Vertical Segments", settings_.sphereStacks, 2, 256);
            break;
        case ProceduralMeshKind::Cylinder:
            DrawDimension(builder, "Diameter", settings_.width);
            DrawDimension(builder, "Height", settings_.height);
            settings_.sphereSlices = DrawSegmentCount(builder, "Radial Segments", settings_.sphereSlices, 3, 512);
            settings_.segmentsY = DrawSegmentCount(builder, "Height Segments", settings_.segmentsY, 1, 512);
            break;
        case ProceduralMeshKind::Capsule:
            DrawDimension(builder, "Diameter", settings_.width);
            DrawDimension(builder, "Total Height", settings_.height);
            settings_.sphereSlices = DrawSegmentCount(builder, "Radial Segments", settings_.sphereSlices, 3, 512);
            settings_.sphereStacks = DrawSegmentCount(builder, "Hemisphere Segments", settings_.sphereStacks, 2, 256);
            break;
        }

        builder.Bool("Double Sided", settings_.doubleSided);
        builder.Bool("Generate Tangents", settings_.generateTangents);
        settings_ = SanitizeProceduralMeshSettings(settings_);
        if (!(settings_ == before)) {
            NotifyRenderStateDirty();
        }
    }

    const ProceduralMeshSettings&
        ProceduralMeshComponent::GetSettings() const noexcept {
        return settings_;
    }

    void ProceduralMeshComponent::SetSettings(
        const ProceduralMeshSettings& settings) {
        const ProceduralMeshSettings sanitized =
            SanitizeProceduralMeshSettings(settings);
        if (settings_ == sanitized) {
            return;
        }
        settings_ = sanitized;
        NotifyRenderStateDirty();
    }

    int ProceduralMeshComponent::GetGeometryFitPriority() const noexcept {
        // Shape-aware procedural fitting is more precise than the generated
        // render model's aggregate AABB.
        return 100;
    }

    bool ProceduralMeshComponent::QueryGeometryFit(
        GeometryFitDesc& outFit) const noexcept {
        const ProceduralMeshSettings settings =
            SanitizeProceduralMeshSettings(settings_);
        outFit = {};
        switch (settings.kind) {
        case ProceduralMeshKind::Plane:
        case ProceduralMeshKind::GridPlane:
            outFit.shape = GeometryFitShape::Box;
            outFit.size = {
                settings.width,
                0.02f,
                settings.depth
            };
            outFit.radius = 0.01f;
            outFit.height = 0.02f;
            break;
        case ProceduralMeshKind::Sphere:
            outFit.shape = GeometryFitShape::Sphere;
            outFit.size = {
                settings.width,
                settings.width,
                settings.width
            };
            outFit.radius = settings.width * 0.5f;
            outFit.height = settings.width;
            break;
        case ProceduralMeshKind::Cylinder:
        case ProceduralMeshKind::Capsule:
            // The current physics shape contract has no cylinder, so a
            // vertical capsule remains the deterministic approximation.
            outFit.shape = GeometryFitShape::Capsule;
            outFit.size = {
                settings.width,
                settings.height,
                settings.width
            };
            outFit.radius = settings.width * 0.5f;
            outFit.height = settings.height;
            break;
        case ProceduralMeshKind::Box:
        default:
            outFit.shape = GeometryFitShape::Box;
            outFit.size = {
                settings.width,
                settings.height,
                settings.depth
            };
            outFit.radius = (std::min)({
                settings.width,
                settings.height,
                settings.depth
            }) * 0.5f;
            outFit.height = settings.height;
            break;
        }
        return true;
    }

    void ProceduralMeshComponent::NotifyRenderStateDirty() {
        if (GameObject* owner = GetOwner()) {
            owner->MarkRenderStateDirty();
        }
    }

} // namespace HIKARI
