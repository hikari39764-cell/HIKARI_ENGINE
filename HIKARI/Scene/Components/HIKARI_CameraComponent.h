#pragma once

#include <string_view>

#include "HIKARI_IComponent.h"

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
        float nearClip_ = 0.1f;
        float farClip_ = 100.0f;
    };

} // namespace HIKARI
