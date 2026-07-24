#include "HIKARI_CameraComponent.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    namespace {
        constexpr float kMinVerticalFovDegrees = 1.0f;
        constexpr float kMaxVerticalFovDegrees = 179.0f;
        constexpr float kMinNearClip = 0.001f;
        constexpr float kMinClipRange = 0.001f;
    }

    void CameraComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["verticalFovDegrees"] = verticalFovDegrees_;
        out["nearClip"] = nearClip_;
        out["farClip"] = farClip_;
    }

    void CameraComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        verticalFovDegrees_ = in.value("verticalFovDegrees", verticalFovDegrees_);
        nearClip_ = in.value("nearClip", nearClip_);
        farClip_ = in.value("farClip", farClip_);
        ClampLens();
    }

    void CameraComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        builder.Float("Vertical FOV", verticalFovDegrees_);
        builder.Float("Near Clip", nearClip_);
        builder.Float("Far Clip", farClip_);
        ClampLens();
    }

    bool CameraComponent::IsEnabled() const noexcept {
        return enabled_;
    }

    float CameraComponent::GetFovYRad() const noexcept {
        return verticalFovDegrees_ * std::numbers::pi_v<float> / 180.0f;
    }

    float CameraComponent::GetNearClip() const noexcept {
        return nearClip_;
    }

    float CameraComponent::GetFarClip() const noexcept {
        return farClip_;
    }

    void CameraComponent::ClampLens() noexcept {
        if (!std::isfinite(verticalFovDegrees_)) {
            verticalFovDegrees_ = 60.0f;
        }
        if (!std::isfinite(nearClip_)) {
            nearClip_ = 0.1f;
        }
        if (!std::isfinite(farClip_)) {
            farClip_ = 100.0f;
        }
        verticalFovDegrees_ = std::clamp(
            verticalFovDegrees_,
            kMinVerticalFovDegrees,
            kMaxVerticalFovDegrees);
        nearClip_ = (std::max)(nearClip_, kMinNearClip);
        farClip_ = (std::max)(farClip_, nearClip_ + kMinClipRange);
    }

} // namespace HIKARI
