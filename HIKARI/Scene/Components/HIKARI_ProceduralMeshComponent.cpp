#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"

#include <algorithm>
#include "Assets/Material/HIKARI_MaterialBuiltInAssets.h"

#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {
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
            { "generateTangents", settings.generateTangents },
            { "uvTilesX", settings.uvTilesX },
            { "uvTilesY", settings.uvTilesY },
            { "materialSelection", materialSelection_ }
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
        settings_.uvTilesX = source.value("uvTilesX", settings_.uvTilesX);
        settings_.uvTilesY = source.value("uvTilesY", settings_.uvTilesY);
        materialSelection_ = ASSETS::MATERIAL::CanonicalizeProceduralMaterialSelection(
            source.value("materialSelection", std::string{}));
        settings_ = SanitizeProceduralMeshSettings(settings_);
        NotifyRenderStateDirty();
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

    const std::string&
        ProceduralMeshComponent::GetMaterialSelection() const noexcept {
        return materialSelection_;
    }

    void ProceduralMeshComponent::SetMaterialSelection(
        std::string selection) {
        selection = ASSETS::MATERIAL::CanonicalizeProceduralMaterialSelection(selection);
        if (materialSelection_ == selection) {
            return;
        }
        materialSelection_ = std::move(selection);
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
