#include "Scene/Components/Lighting/HIKARI_LightComponent.h"

namespace HIKARI {

void LightComponent::Serialize(nlohmann::json& out) const { out = WriteLightSettings(settings_); }

void LightComponent::Deserialize(const nlohmann::json& in) { Apply(ReadLightSettings(in)); }

void LightComponent::Apply(LightSettings next) noexcept {
    next = NormalizeLightSettings(next);
    if (next == settings_) {
        return;
    }
    settings_ = next;
    ++stateRevision_;
    if (stateRevision_ == 0u) {
        stateRevision_ = 1u;
    }
}

void LightComponent::SetEnabled(bool enabled) noexcept {
    LightSettings next = settings_;
    next.enabled = enabled;
    Apply(next);
}

void LightComponent::SetLightType(RENDER3D::LIGHTING::SceneLightType type) noexcept {
    LightSettings next = settings_;
    next.type = type;
    Apply(next);
}

void LightComponent::SetColor(const MATH::Vec3& color) noexcept {
    LightSettings next = settings_;
    next.color = color;
    Apply(next);
}

void LightComponent::SetIntensity(float intensity) noexcept {
    LightSettings next = settings_;
    next.intensity = intensity;
    Apply(next);
}

void LightComponent::SetRange(float range) noexcept {
    LightSettings next = settings_;
    next.range = range;
    Apply(next);
}

void LightComponent::SetSourceRadius(float radius) noexcept {
    LightSettings next = settings_;
    next.sourceRadius = radius;
    Apply(next);
}

void LightComponent::SetCastsShadows(bool value) noexcept {
    LightSettings next = settings_;
    next.castsShadows = value;
    Apply(next);
}

void LightComponent::SetSpotConeAnglesDegrees(float innerAngleDegrees, float outerAngleDegrees) noexcept {
    LightSettings next = settings_;
    next.innerConeAngleDegrees = innerAngleDegrees;
    next.outerConeAngleDegrees = outerAngleDegrees;
    Apply(next);
}

void LightComponent::SetAngularFalloff(float angularFalloff) noexcept {
    LightSettings next = settings_;
    next.angularFalloff = angularFalloff;
    Apply(next);
}

} // namespace HIKARI
