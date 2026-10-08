#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"

#include <algorithm>
#include <cmath>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

namespace JsonMath = SERIALIZATION::JSON::MATH;

void IrradianceVolumeComponent::Serialize(nlohmann::json& out) const {
    out = {{"enabled", enabled_},
           {"mode", TEXT::EnumToName(kLightProbeVolumeModeNames, mode_)},
           {"size", JsonMath::ToJsonArray(size_)},
           {"countX", countX_},
           {"countY", countY_},
           {"countZ", countZ_},
           {"intensity", intensity_},
           {"blendDistance", blendDistance_},
           {"normalBias", normalBias_},
           {"captureResolution", captureResolution_},
           {"shOrder", shOrder_},
           {"priority", priority_},
           {"bakeSet", bakeSet_},
           {"scenario", scenario_}};
}

void IrradianceVolumeComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = in.value("enabled", enabled_);
    TEXT::TryParseEnum(kLightProbeVolumeModeNames, in.value("mode", std::string{}), mode_);
    size_ = JSONREAD::Vec3Or(in.value("size", nlohmann::json::array()), size_);
    countX_ = in.value("countX", countX_);
    countY_ = in.value("countY", countY_);
    countZ_ = in.value("countZ", countZ_);
    intensity_ = in.value("intensity", intensity_);
    blendDistance_ = in.value("blendDistance", blendDistance_);
    normalBias_ = in.value("normalBias", normalBias_);
    captureResolution_ = in.value("captureResolution", captureResolution_);
    shOrder_ = in.value("shOrder", shOrder_);
    priority_ = in.value("priority", priority_);
    bakeSet_ = in.value("bakeSet", bakeSet_);
    scenario_ = in.value("scenario", scenario_);
    ClampSettings();
}

bool IrradianceVolumeComponent::SetSize(const MATH::Vec3& size) noexcept {
    if (!MATH::IsFinite(size)) {
        return false;
    }
    const MATH::Vec3 clamped{(std::max)(size.x, 0.1f), (std::max)(size.y, 0.1f), (std::max)(size.z, 0.1f)};
    if (size_.x == clamped.x && size_.y == clamped.y && size_.z == clamped.z) {
        return false;
    }
    size_ = clamped;
    ClampSettings();
    return true;
}

void IrradianceVolumeComponent::ClampSettings() noexcept {
    LightProbeVolumeSettings settings{};
    settings.mode = mode_;
    settings.size = size_;
    settings.countX = countX_;
    settings.countY = countY_;
    settings.countZ = countZ_;
    settings.intensity = intensity_;
    settings.blendDistance = blendDistance_;
    settings.normalBias = normalBias_;
    settings.captureResolution = captureResolution_;
    settings.shOrder = shOrder_;
    ClampLightProbeVolumeSettings(settings);
    size_ = settings.size;
    countX_ = settings.countX;
    countY_ = settings.countY;
    countZ_ = settings.countZ;
    intensity_ = settings.intensity;
    blendDistance_ = settings.blendDistance;
    normalBias_ = settings.normalBias;
    captureResolution_ = settings.captureResolution;
    shOrder_ = settings.shOrder;
    if (bakeSet_.empty()) {
        bakeSet_ = "Default";
    }
    if (scenario_.empty()) {
        scenario_ = "Default";
    }
}

} // namespace HIKARI
