#include "Render3D/Core/HIKARI_CameraOptics.h"

#include <limits>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Render3D/Core/HIKARI_CameraOpticsSerialization.h"

namespace HIKARI::CAMERA {

namespace {
inline constexpr auto kDepthOfFieldSettingsFields = [](auto&& field) {
    field("enabled", &DepthOfFieldSettings::enabled);
    field("focusDistance", &DepthOfFieldSettings::focusDistance,
          JSONFIELDS::Range{kMinDepthOfFieldFocusDistance, std::numeric_limits<float>::max()});
    field("focusRange", &DepthOfFieldSettings::focusRange, JSONFIELDS::Range{0.0, std::numeric_limits<float>::max()});
    field("nearTransition", &DepthOfFieldSettings::nearTransition,
          JSONFIELDS::Range{kMinDepthOfFieldTransition, std::numeric_limits<float>::max()});
    field("farTransition", &DepthOfFieldSettings::farTransition,
          JSONFIELDS::Range{kMinDepthOfFieldTransition, std::numeric_limits<float>::max()});
    field("nearBlurRadius1080pPixels", &DepthOfFieldSettings::nearBlurRadius1080pPixels,
          JSONFIELDS::Range{0.0, kMaxDepthOfFieldBlurRadius1080pPixels});
    field("farBlurRadius1080pPixels", &DepthOfFieldSettings::farBlurRadius1080pPixels,
          JSONFIELDS::Range{0.0, kMaxDepthOfFieldBlurRadius1080pPixels});
};
} // namespace

DepthOfFieldSettings NormalizeDepthOfFieldSettings(DepthOfFieldSettings settings) noexcept {
    return JSONFIELDS::Sanitize(settings, kDepthOfFieldSettingsFields);
}

bool IsValidDepthOfFieldSettings(const DepthOfFieldSettings& settings) noexcept {
    return NormalizeDepthOfFieldSettings(settings) == settings;
}

void ReadDepthOfFieldSettings(const nlohmann::json& node, DepthOfFieldSettings& settings) {
    if (node.is_object()) {
        JSONFIELDS::ReadLenient(node, settings, kDepthOfFieldSettingsFields);
    }
    settings = NormalizeDepthOfFieldSettings(settings);
}

nlohmann::json WriteDepthOfFieldSettings(const DepthOfFieldSettings& settings) {
    nlohmann::json node = nlohmann::json::object();
    JSONFIELDS::Write(node, NormalizeDepthOfFieldSettings(settings), kDepthOfFieldSettingsFields);
    return node;
}

} // namespace HIKARI::CAMERA
