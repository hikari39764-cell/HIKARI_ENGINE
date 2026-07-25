#include "Scene/Components/HIKARI_CameraActivationVolumeComponent.h"

#include <algorithm>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
namespace HIKARI {

    namespace JsonMath = SERIALIZATION::JSON::MATH;

    void CameraActivationVolumeComponent::Serialize(
        nlohmann::json& out) const {

        out["enabled"] = enabled_;
        out["subjectObjectId"] = subjectObjectId_.value;
        out["cameraObjectId"] = cameraObjectId_.value;
        out["halfExtents"] = JsonMath::ToJsonArray(halfExtents_);
        out["priority"] = priority_;
        out["blendSeconds"] = blendSeconds_;
        out["affectsControlBasis"] = affectsControlBasis_;
    }

    void CameraActivationVolumeComponent::Deserialize(
        const nlohmann::json& in) {

        enabled_ = in.value("enabled", enabled_);
        subjectObjectId_.value = in.value(
            "subjectObjectId",
            subjectObjectId_.value);
        cameraObjectId_.value = in.value(
            "cameraObjectId",
            cameraObjectId_.value);
        if (in.contains("halfExtents")) {
            halfExtents_ = JSONREAD::Vec3Or(
                in["halfExtents"],
                halfExtents_);
        }
        priority_ = in.value("priority", priority_);
        blendSeconds_ = in.value("blendSeconds", blendSeconds_);
        affectsControlBasis_ = in.value(
            "affectsControlBasis",
            affectsControlBasis_);
        ClampSettings();
    }

    void CameraActivationVolumeComponent::ClampSettings() noexcept {
        halfExtents_.x = (std::max)(halfExtents_.x, 0.01f);
        halfExtents_.y = (std::max)(halfExtents_.y, 0.01f);
        halfExtents_.z = (std::max)(halfExtents_.z, 0.01f);
        blendSeconds_ = (std::max)(blendSeconds_, 0.0f);
    }

} // namespace HIKARI
