#include "Scene/Components/Rendering/Decal/HIKARI_DecalComponent.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Authoring/HIKARI_FieldInspector.h"
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
    DecalSettings next = settings_;
    if (InspectFields(builder, next, kDecalSettingsFields)) {
        SetSettings(next);
    }
}
#endif

} // namespace HIKARI
