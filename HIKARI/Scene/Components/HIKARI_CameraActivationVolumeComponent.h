#pragma once

#include <string_view>

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    class CameraActivationVolumeComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "CameraActivationVolumeComponent";
        }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept { return enabled_; }
        SceneObjectId GetSubjectObjectId() const noexcept {
            return subjectObjectId_;
        }
        SceneObjectId GetCameraObjectId() const noexcept {
            return cameraObjectId_;
        }
        const MATH::Vec3& GetHalfExtents() const noexcept {
            return halfExtents_;
        }
        int GetPriority() const noexcept { return priority_; }
        float GetBlendSeconds() const noexcept { return blendSeconds_; }
        bool GetAffectsControlBasis() const noexcept {
            return affectsControlBasis_;
        }

    private:
        void ClampSettings() noexcept;

        bool enabled_ = true;
        SceneObjectId subjectObjectId_{};
        SceneObjectId cameraObjectId_{};
        MATH::Vec3 halfExtents_{ 4.0f, 3.0f, 4.0f };
        int priority_ = 50;
        float blendSeconds_ = 0.5f;
        bool affectsControlBasis_ = true;
    };

} // namespace HIKARI
