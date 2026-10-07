#pragma once

#include <string_view>

#include "HIKARI_IComponent.h"
#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI {

class CameraComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "CameraComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept;
    float GetFovYRad() const noexcept;
    float GetNearClip() const noexcept;
    float GetFarClip() const noexcept;
    const CAMERA::DepthOfFieldSettings& GetDepthOfFieldSettings() const noexcept { return depthOfFieldSettings_; }
    bool IsOrthographic() const noexcept { return orthographic_; }
    float GetOrthographicHeight() const noexcept { return orthographicHeight_; }
    float GetPixelGridSize() const noexcept { return pixelSnap_ ? 1.0f / pixelsPerUnit_ : 0.0f; }

private:
    void ClampLens() noexcept;

    bool enabled_ = true;
    bool orthographic_ = false, pixelSnap_ = false;
    float orthographicHeight_ = 10.0f, pixelsPerUnit_ = 32.0f;
    float verticalFovDegrees_ = 60.0f;
    float nearClip_ = Camera3D::kDefaultNearZ;
    float farClip_ = Camera3D::kDefaultFarZ;
    CAMERA::DepthOfFieldSettings depthOfFieldSettings_{};
};

} // namespace HIKARI
