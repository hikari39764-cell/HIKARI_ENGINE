#pragma once

#include <string_view>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_SceneObjectId.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI {

// 作者値（シーン文書の properties と同じ形）。項目表（kCameraActivationVolumeFields）が保存・読み取り・検査器・説明を
// 決める。
struct CameraActivationVolumeSettings {
    bool enabled = true;
    SceneObjectId subjectObjectId{};
    SceneObjectId cameraObjectId{};
    MATH::Vec3 halfExtents{4.0f, 3.0f, 4.0f};
    int priority = 50;
    float blendSeconds = 0.5f;
    bool affectsControlBasis = true;
};

inline constexpr auto kCameraActivationVolumeFields = [](auto&& field) {
    using S = CameraActivationVolumeSettings;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Enabled"}, Common{});
    field("subjectObjectId", &S::subjectObjectId, Label{"Subject"}, ObjectRef{}, Common{});
    field("cameraObjectId", &S::cameraObjectId, Label{"Camera"}, ObjectRef{}, Common{});
    field("halfExtents", &S::halfExtents, Range{0.01, 100000.0}, Label{"Half Extents"}, Unit{"m"},
          Help{"Half size of the oriented box along local X, Y and Z."});
    field("priority", &S::priority, Label{"Activation Priority"});
    field("blendSeconds", &S::blendSeconds, Range{0.0, 60.0}, Label{"Blend Seconds"});
    field("affectsControlBasis", &S::affectsControlBasis, Label{"Affects Control Basis"});
};

class CameraActivationVolumeComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "CameraActivationVolumeComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept { return settings_.enabled; }
    SceneObjectId GetSubjectObjectId() const noexcept { return settings_.subjectObjectId; }
    SceneObjectId GetCameraObjectId() const noexcept { return settings_.cameraObjectId; }
    const MATH::Vec3& GetHalfExtents() const noexcept { return settings_.halfExtents; }
    int GetPriority() const noexcept { return settings_.priority; }
    float GetBlendSeconds() const noexcept { return settings_.blendSeconds; }
    bool GetAffectsControlBasis() const noexcept { return settings_.affectsControlBasis; }

private:
    CameraActivationVolumeSettings settings_{};
};

} // namespace HIKARI
