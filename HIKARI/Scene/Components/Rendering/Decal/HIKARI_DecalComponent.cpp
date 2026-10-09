#include "Scene/Components/Rendering/Decal/HIKARI_DecalComponent.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Assets/HIKARI_AssetTypes.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#endif

namespace HIKARI {

using RENDER3D::DECAL::DecalSettings;

DecalSettings NormalizeDecalSettings(DecalSettings settings) noexcept {
    return JSONFIELDS::Sanitize(settings, kDecalSettingsFields);
}

DecalSettings ReadDecalSettings(const nlohmann::json& properties) {
    DecalSettings settings{};
    if (properties.is_object()) {
        JSONFIELDS::ReadLenient(properties, settings, kDecalSettingsFields);
    }
    return NormalizeDecalSettings(settings);
}

nlohmann::json WriteDecalSettings(const DecalSettings& settings) {
    nlohmann::json out = nlohmann::json::object();
    JSONFIELDS::Write(out, NormalizeDecalSettings(settings), kDecalSettingsFields);
    return out;
}

#if defined(HIKARI_WITH_EDITOR)
void DecalComponent::BuildInspector(IInspectorBuilder& builder) {
    using RENDER3D::DECAL::kMaximumDecalExtent;
    DecalSettings next = settings_;
    builder.Text("Decal (projects along local +Z)");
    builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled"}, next.enabled);
    builder.FloatRange(AuthoringFieldDescriptor{"width", "Width (m)", "Box size along local X."}, next.size.x, 0.01f,
                       kMaximumDecalExtent, 0.01f);
    builder.FloatRange(AuthoringFieldDescriptor{"height", "Height (m)", "Box size along local Y."}, next.size.y, 0.01f,
                       kMaximumDecalExtent, 0.01f);
    builder.FloatRange(AuthoringFieldDescriptor{"depth", "Projection Depth (m)",
                                                "Box size along local Z. Surfaces inside the box receive the decal; "
                                                "it fades near both ends."},
                       next.size.z, 0.01f, kMaximumDecalExtent, 0.01f);
    builder.AssetIdPicker(AuthoringFieldDescriptor{"baseColorTexture", "Base Color Texture",
                                                   "RGB colour and alpha coverage. Reads unmirrored when looking "
                                                   "along the projection (texture up = local +Y)."},
                          AssetType::Texture, next.baseColorTexture);
    builder.AssetIdPicker(AuthoringFieldDescriptor{"normalTexture", "Normal Texture",
                                                   "Tangent-space normal map (same convention as materials)."},
                          AssetType::Texture, next.normalTexture);
    builder.Color3(AuthoringFieldDescriptor{"color", "Color"}, next.color.x, next.color.y, next.color.z);
    builder.FloatRange(AuthoringFieldDescriptor{"opacity", "Opacity"}, next.opacity, 0.0f, 1.0f, 0.01f);
    builder.FloatRange(AuthoringFieldDescriptor{"colorWeight", "Color Weight",
                                                "Zero keeps the surface colour and applies only normal and "
                                                "roughness (wet patches, dents)."},
                       next.colorWeight, 0.0f, 1.0f, 0.01f);
    builder.FloatRange(AuthoringFieldDescriptor{"normalWeight", "Normal Weight"}, next.normalWeight, 0.0f, 1.0f, 0.01f);
    builder.FloatRange(AuthoringFieldDescriptor{"materialWeight", "Roughness / Metallic Weight"}, next.materialWeight,
                       0.0f, 1.0f, 0.01f);
    if (next.materialWeight > 0.0f) {
        builder.FloatRange(AuthoringFieldDescriptor{"roughness", "Roughness"}, next.roughness, 0.04f, 1.0f, 0.01f);
        builder.FloatRange(AuthoringFieldDescriptor{"metallic", "Metallic"}, next.metallic, 0.0f, 1.0f, 0.01f);
    }
    builder.FloatRange(AuthoringFieldDescriptor{"angleFadeDegrees", "Angle Fade Start",
                                                "Surfaces turned further than this from the projection fade out, "
                                                "reaching zero at 90 degrees."},
                       next.angleFadeDegrees, 0.0f, 89.0f, 0.5f);
    builder.Int(AuthoringFieldDescriptor{"sortOrder", "Sort Order", "Higher values draw over lower ones."},
                next.sortOrder);
    SetSettings(next);
}
#endif

} // namespace HIKARI
