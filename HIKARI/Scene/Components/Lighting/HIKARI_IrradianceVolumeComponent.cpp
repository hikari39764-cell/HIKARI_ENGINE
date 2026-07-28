#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"

#include <algorithm>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    namespace JsonMath = SERIALIZATION::JSON::MATH;

    void IrradianceVolumeComponent::Serialize(nlohmann::json& out) const {
        out = {
            { "enabled", enabled_ },
            { "size", JsonMath::ToJsonArray(size_) },
            { "countX", countX_ },
            { "countY", countY_ },
            { "countZ", countZ_ },
            { "intensity", intensity_ },
            { "captureResolution", captureResolution_ },
            { "shOrder", shOrder_ },
            { "priority", priority_ },
            { "bakeSet", bakeSet_ },
            { "scenario", scenario_ }
        };
    }

    void IrradianceVolumeComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        size_ = JSONREAD::Vec3Or(
            in.value("size", nlohmann::json::array()),
            size_);
        countX_ = in.value("countX", countX_);
        countY_ = in.value("countY", countY_);
        countZ_ = in.value("countZ", countZ_);
        intensity_ = in.value("intensity", intensity_);
        captureResolution_ = in.value(
            "captureResolution",
            captureResolution_);
        shOrder_ = in.value("shOrder", shOrder_);
        priority_ = in.value("priority", priority_);
        bakeSet_ = in.value("bakeSet", bakeSet_);
        scenario_ = in.value("scenario", scenario_);
        ClampSettings();
    }

    void IrradianceVolumeComponent::ClampSettings() noexcept {
        LightProbeVolumeSettings settings{};
        settings.size = size_;
        settings.countX = countX_;
        settings.countY = countY_;
        settings.countZ = countZ_;
        settings.intensity = intensity_;
        settings.captureResolution = captureResolution_;
        settings.shOrder = shOrder_;
        ClampLightProbeVolumeSettings(settings);
        size_ = settings.size;
        countX_ = settings.countX;
        countY_ = settings.countY;
        countZ_ = settings.countZ;
        intensity_ = settings.intensity;
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
