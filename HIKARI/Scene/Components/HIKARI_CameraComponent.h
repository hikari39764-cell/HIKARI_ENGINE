#pragma once

#include <string_view>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "HIKARI_IComponent.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_CameraOpticsFields.h"

namespace HIKARI {

// 作者値（シーン文書の properties と同じ形）。項目表（kCameraFields）が保存・読み取り・検査器・説明を決める。
struct CameraSettings {
    bool enabled = true;
    float verticalFovDegrees = 60.0f;
    bool orthographic = false;
    float orthographicHeight = 10.0f;
    bool pixelSnap = false;
    float pixelsPerUnit = 32.0f;
    float nearClip = Camera3D::kDefaultNearZ;
    float farClip = Camera3D::kDefaultFarZ;
    CAMERA::DepthOfFieldSettings depthOfField{};
    CAMERA::MotionBlurSettings motionBlur{};
};

inline constexpr auto kCameraFields = [](auto&& field) {
    using S = CameraSettings;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Enabled"}, Common{});
    field("verticalFovDegrees", &S::verticalFovDegrees, Slider(1.0, 179.0), Label{"Vertical FOV"},
          Help{"Vertical field of view in degrees."}, Common{});
    field("orthographic", &S::orthographic, Label{"Orthographic"}, Common{});
    field("orthographicHeight", &S::orthographicHeight, Range{0.01, 100000.0}, Label{"Vertical size"},
          Show{[](const S& s) { return s.orthographic; }});
    field("pixelSnap", &S::pixelSnap, Label{"Snap camera to pixel grid"},
          Show{[](const S& s) { return s.orthographic; }});
    field("pixelsPerUnit", &S::pixelsPerUnit, Range{1.0, 4096.0}, Label{"Pixels per world unit"},
          Show{[](const S& s) { return s.orthographic && s.pixelSnap; }});
    field("nearClip", &S::nearClip, Range{0.001, 1000.0}, Label{"Near Clip"},
          Help{"Near clipping distance in world units."});
    field("farClip", &S::farClip, Range{0.001, 1000000.0}, Label{"Far Clip"},
          Help{"Far clipping distance in world units."});
    field("depthOfField", &S::depthOfField, Nested{CAMERA::kDepthOfFieldFields}, Group{"Depth of Field"});
    field("motionBlur", &S::motionBlur, Nested{CAMERA::kMotionBlurFields}, Group{"Motion Blur"});
};

class CameraComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "CameraComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept { return settings_.enabled; }
    float GetFovYRad() const noexcept;
    float GetNearClip() const noexcept { return settings_.nearClip; }
    float GetFarClip() const noexcept { return settings_.farClip; }
    const CAMERA::DepthOfFieldSettings& GetDepthOfFieldSettings() const noexcept { return settings_.depthOfField; }
    const CAMERA::MotionBlurSettings& GetMotionBlurSettings() const noexcept { return settings_.motionBlur; }
    bool IsOrthographic() const noexcept { return settings_.orthographic; }
    float GetOrthographicHeight() const noexcept { return settings_.orthographicHeight; }
    float GetPixelGridSize() const noexcept { return settings_.pixelSnap ? 1.0f / settings_.pixelsPerUnit : 0.0f; }

private:
    CameraSettings settings_{};
};

} // namespace HIKARI
