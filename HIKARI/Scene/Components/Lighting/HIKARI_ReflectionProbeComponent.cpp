#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"

#include <algorithm>
#include <cmath>

#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI {

void AlignReflectionProbeProjection(ReflectionProbeComponentSettings& settings) noexcept {
    settings.projectionShape = ReflectionProbeProjectionShape::Box;
    if (settings.influenceShape == ReflectionProbeInfluenceShape::Box) {
        settings.projectionBoxOffset = settings.influenceBoxOffset;
        settings.projectionBoxSize = settings.influenceBoxSize;
        return;
    }
    const float diameter = settings.radius * 2.0f;
    settings.projectionBoxOffset = {0.0f, 0.0f, 0.0f};
    settings.projectionBoxSize = {diameter, diameter, diameter};
}

ReflectionProbeComponentSettings NormalizeReflectionProbeSettings(ReflectionProbeComponentSettings settings) {
    settings = JSONFIELDS::Sanitize(settings, kReflectionProbeFields);
    settings.captureResolution =
        settings.captureResolution <= 64u ? 64u : (settings.captureResolution <= 128u ? 128u : 256u);
    if (settings.bakeSet.empty()) {
        settings.bakeSet = "Default";
    }
    if (settings.scenario.empty()) {
        settings.scenario = "Default";
    }
    if (settings.projectionFollowsInfluence) {
        AlignReflectionProbeProjection(settings);
    }
    return settings;
}

void ReflectionProbeComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kReflectionProbeFields);
}

void ReflectionProbeComponent::Deserialize(const nlohmann::json& in) {
    ReflectionProbeComponentSettings decoded{};
    JSONFIELDS::Read(in, decoded, kReflectionProbeFields);
    settings_ = NormalizeReflectionProbeSettings(decoded);
}

bool ReflectionProbeComponent::SetRadius(float radius) noexcept {
    if (!std::isfinite(radius)) {
        return false;
    }
    const float clamped = (std::max)(radius, 0.05f);
    if (settings_.radius == clamped) {
        return false;
    }
    settings_.radius = clamped;
    if (settings_.projectionFollowsInfluence && settings_.projectionShape == ReflectionProbeProjectionShape::Box) {
        AlignReflectionProbeProjection(settings_);
    }
    return true;
}

bool ReflectionProbeComponent::SetInfluenceBoxSize(const MATH::Vec3& size) noexcept {
    if (!MATH::IsFinite(size)) {
        return false;
    }
    const MATH::Vec3 clamped{(std::max)(size.x, 0.05f), (std::max)(size.y, 0.05f), (std::max)(size.z, 0.05f)};
    const MATH::Vec3& current = settings_.influenceBoxSize;
    if (current.x == clamped.x && current.y == clamped.y && current.z == clamped.z) {
        return false;
    }
    settings_.influenceBoxSize = clamped;
    if (settings_.projectionFollowsInfluence) {
        AlignReflectionProbeProjection(settings_);
    }
    return true;
}

} // namespace HIKARI
