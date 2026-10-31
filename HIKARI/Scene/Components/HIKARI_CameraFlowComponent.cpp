#include "Scene/Components/HIKARI_CameraFlowComponent.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Authoring/HIKARI_FieldInspector.h"
#endif

namespace HIKARI {
const char* CameraFlowStageId(CameraFlowStage stage) noexcept {
    switch (stage) {
    case CameraFlowStage::Armed: return "armed";
    case CameraFlowStage::Playing: return "playing";
    case CameraFlowStage::Returning: return "returning";
    case CameraFlowStage::Completed: return "completed";
    case CameraFlowStage::Interrupted: return "interrupted";
    case CameraFlowStage::Invalid: return "invalid";
    default: return "disabled";
    }
}
void CameraFlowComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kCameraFlowFields);
}
void CameraFlowComponent::Deserialize(const nlohmann::json& in) {
    CameraFlowSettings decoded{};
    JSONFIELDS::Read(in, decoded, kCameraFlowFields);
    settings_ = JSONFIELDS::Sanitize(decoded, kCameraFlowFields);
}
#if defined(HIKARI_WITH_EDITOR)
void CameraFlowComponent::BuildInspector(IInspectorBuilder& builder) {
    CameraFlowSettings next = settings_;
    if (InspectFields(builder, next, kCameraFlowFields)) {
        settings_ = JSONFIELDS::Sanitize(next, kCameraFlowFields);
    }
}
#endif
} // namespace HIKARI
