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
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept;
        float GetFovYRad() const noexcept;
        float GetNearClip() const noexcept;
        float GetFarClip() const noexcept;

    private:
        void ClampLens() noexcept;

        bool enabled_ = true;
        float verticalFovDegrees_ = 60.0f;
        float nearClip_ = Camera3D::kDefaultNearZ;
        float farClip_ = Camera3D::kDefaultFarZ;
    };

} // namespace HIKARI
