#include "Scene/Components/HIKARI_CameraActivationVolumeComponent.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Authoring/HIKARI_FieldInspector.h"
#endif

namespace HIKARI {

namespace {
CameraActivationVolumeSettings Normalize(const CameraActivationVolumeSettings& settings) {
    return JSONFIELDS::Sanitize(settings, kCameraActivationVolumeFields);
}
} // namespace

void CameraActivationVolumeComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kCameraActivationVolumeFields);
}

void CameraActivationVolumeComponent::Deserialize(const nlohmann::json& in) {
    CameraActivationVolumeSettings decoded{};
    JSONFIELDS::Read(in, decoded, kCameraActivationVolumeFields);
    settings_ = Normalize(decoded);
}

#if defined(HIKARI_WITH_EDITOR)
void CameraActivationVolumeComponent::BuildInspector(IInspectorBuilder& builder) {
    CameraActivationVolumeSettings next = settings_;
    if (InspectFields(builder, next, kCameraActivationVolumeFields)) {
        settings_ = Normalize(next);
    }
}
#endif

} // namespace HIKARI
