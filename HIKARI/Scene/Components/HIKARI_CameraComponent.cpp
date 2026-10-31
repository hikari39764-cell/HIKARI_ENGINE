#include "HIKARI_CameraComponent.h"

#include <algorithm>
#include <numbers>

#if defined(HIKARI_WITH_EDITOR)
#include "Authoring/HIKARI_FieldInspector.h"
#endif

namespace HIKARI {

namespace {
// 遠面は近面より少なくこの距離だけ先に置く（項目間の関係なので表の範囲では表せない）。
constexpr float kMinClipRange = 0.001f;

// 範囲へ丸め（非有限値は既定値）、遠面を近面より先に置く。
CameraSettings NormalizeCameraSettings(CameraSettings settings) {
    settings = JSONFIELDS::Sanitize(settings, kCameraFields);
    settings.farClip = (std::max)(settings.farClip, settings.nearClip + kMinClipRange);
    return settings;
}
} // namespace

void CameraComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kCameraFields);
}

void CameraComponent::Deserialize(const nlohmann::json& in) {
    CameraSettings decoded{};
    JSONFIELDS::Read(in, decoded, kCameraFields);
    settings_ = NormalizeCameraSettings(decoded);
}

#if defined(HIKARI_WITH_EDITOR)
void CameraComponent::BuildInspector(IInspectorBuilder& builder) {
    CameraSettings next = settings_;
    if (InspectFields(builder, next, kCameraFields)) {
        settings_ = NormalizeCameraSettings(next);
    }
}
#endif

float CameraComponent::GetFovYRad() const noexcept {
    return settings_.verticalFovDegrees * std::numbers::pi_v<float> / 180.0f;
}

} // namespace HIKARI
