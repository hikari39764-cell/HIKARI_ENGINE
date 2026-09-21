#include "Scene/Components/HIKARI_CameraFlowComponent.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"
#include <algorithm>
#include <cmath>
namespace HIKARI {
const char *CameraFlowStageId(CameraFlowStage stage) noexcept {
    switch (stage) {
    case CameraFlowStage::Armed:
        return "armed";
    case CameraFlowStage::Playing:
        return "playing";
    case CameraFlowStage::Returning:
        return "returning";
    case CameraFlowStage::Completed:
        return "completed";
    case CameraFlowStage::Interrupted:
        return "interrupted";
    case CameraFlowStage::Invalid:
        return "invalid";
    default:
        return "disabled";
    }
}
void CameraFlowComponent::Serialize(nlohmann::json &out) const {
    const auto &s = settings_;
    out = {{"enabled", s.enabled},
           {"subjectObjectId", SCENE::SERIALIZATION::WriteSceneObjectReference(s.subjectObjectId.value)},
           {"cameraObjectId", SCENE::SERIALIZATION::WriteSceneObjectReference(s.cameraObjectId.value)},
           {"halfExtents", SERIALIZATION::JSON::MATH::ToJsonArray(s.halfExtents)},
           {"sequenceAssetGuid", s.sequence.value},
           {"afterMode", s.afterMode},
           {"priority", s.priority},
           {"requiredMode", s.requiredMode},
           {"triggerOnExit", s.triggerOnExit},
           {"repeatOnReentry", s.repeatOnReentry},
           {"finishOnInterrupt", s.finishOnInterrupt}};
}
void CameraFlowComponent::Deserialize(const nlohmann::json &in) {
    auto &s = settings_;
    s.enabled = in.value("enabled", true);
    s.subjectObjectId.value =
        SCENE::SERIALIZATION::SceneObjectReferenceOr(in.value("subjectObjectId", nlohmann::json{}), 0);
    s.cameraObjectId.value =
        SCENE::SERIALIZATION::SceneObjectReferenceOr(in.value("cameraObjectId", nlohmann::json{}), 0);
    s.halfExtents = JSONREAD::Vec3Or(in.value("halfExtents", nlohmann::json{}), {2, 2, 2});
    for (auto *value : {&s.halfExtents.x, &s.halfExtents.y, &s.halfExtents.z})
        *value = std::isfinite(*value) ? std::clamp(*value, 0.01f, 10000.0f) : 2.0f;
    s.sequence = {in.value("sequenceAssetGuid", std::string{})};
    s.afterMode = in.value("afterMode", std::string{"first_person"});
    s.requiredMode = in.value("requiredMode", std::string{});
    s.triggerOnExit = in.value("triggerOnExit", false);
    s.priority = in.value("priority", 50);
    s.repeatOnReentry = in.value("repeatOnReentry", false);
    s.finishOnInterrupt = in.value("finishOnInterrupt", false);
}
} // namespace HIKARI
