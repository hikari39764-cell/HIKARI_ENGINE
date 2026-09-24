#include "HIKARI_CameraComponent.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace HIKARI {

namespace {
constexpr float kMinVerticalFovDegrees = 1.0f;
constexpr float kMaxVerticalFovDegrees = 179.0f;
constexpr float kMinNearClip = 0.001f;
constexpr float kMinClipRange = 0.001f;
} // namespace

void CameraComponent::Serialize(nlohmann::json& out) const {
    out["enabled"] = enabled_;
    out["orthographic"] = orthographic_;
    out["orthographicHeight"] = orthographicHeight_;
    out["pixelSnap"] = pixelSnap_;
    out["pixelsPerUnit"] = pixelsPerUnit_;
    out["verticalFovDegrees"] = verticalFovDegrees_;
    out["nearClip"] = nearClip_;
    out["farClip"] = farClip_;
}

void CameraComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = in.value("enabled", enabled_);
    orthographic_ = in.value("orthographic", false);
    orthographicHeight_ = in.value("orthographicHeight", 10.0f);
    pixelSnap_ = in.value("pixelSnap", false);
    pixelsPerUnit_ = in.value("pixelsPerUnit", 32.0f);
    verticalFovDegrees_ = in.value("verticalFovDegrees", verticalFovDegrees_);
    nearClip_ = in.value("nearClip", nearClip_);
    farClip_ = in.value("farClip", farClip_);
    ClampLens();
}

bool CameraComponent::IsEnabled() const noexcept { return enabled_; }

float CameraComponent::GetFovYRad() const noexcept { return verticalFovDegrees_ * std::numbers::pi_v<float> / 180.0f; }

float CameraComponent::GetNearClip() const noexcept { return nearClip_; }

float CameraComponent::GetFarClip() const noexcept { return farClip_; }

void CameraComponent::ClampLens() noexcept {
    orthographicHeight_ =
        std::isfinite(orthographicHeight_) ? std::clamp(orthographicHeight_, 0.01f, 100000.0f) : 10.0f;
    pixelsPerUnit_ = std::isfinite(pixelsPerUnit_) ? std::clamp(pixelsPerUnit_, 1.0f, 4096.0f) : 32.0f;
    if (!std::isfinite(verticalFovDegrees_)) {
        verticalFovDegrees_ = 60.0f;
    }
    if (!std::isfinite(nearClip_)) {
        nearClip_ = Camera3D::kDefaultNearZ;
    }
    if (!std::isfinite(farClip_)) {
        farClip_ = Camera3D::kDefaultFarZ;
    }
    verticalFovDegrees_ = std::clamp(verticalFovDegrees_, kMinVerticalFovDegrees, kMaxVerticalFovDegrees);
    nearClip_ = (std::max)(nearClip_, kMinNearClip);
    farClip_ = (std::max)(farClip_, nearClip_ + kMinClipRange);
}

} // namespace HIKARI
