#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Core/Numeric/HIKARI_NonZero.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Authoring/HIKARI_FieldInspector.h"
#endif

namespace HIKARI {

void LightComponent::Serialize(nlohmann::json& out) const { out = WriteLightSettings(settings_); }

void LightComponent::Deserialize(const nlohmann::json& in) { SetSettings(ReadLightSettings(in)); }

#if defined(HIKARI_WITH_EDITOR)
void LightComponent::BuildInspector(IInspectorBuilder& builder) {
    LightSettings next = settings_;
    if (!InspectFields(builder, next, kLightSettingsFields)) {
        return;
    }
    // 単位を変えても今の明るさを保つ（換算は単位を切り替える前の値から行う）。
    const LightIntensityUnit unit = next.intensityUnit;
    next.intensityUnit = settings_.intensityUnit;
    SetSettings(ConvertLightIntensityUnit(next, unit));
}
#endif

void LightComponent::SetSettings(LightSettings next) noexcept {
    next = NormalizeLightSettings(next);
    if (next == settings_) {
        return;
    }
    settings_ = next;
    NUMERIC::AdvanceNonZero(stateRevision_);
}

} // namespace HIKARI
