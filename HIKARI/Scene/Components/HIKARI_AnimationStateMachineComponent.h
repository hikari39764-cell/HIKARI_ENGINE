#pragma once

#include <string>

#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

    class AnimationStateMachineComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "AnimationStateMachineComponent";
        }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;
        void RenderImGui() override;

        bool IsEnabled() const noexcept;
        void SetEnabled(bool enabled) noexcept;
        const std::string& GetAssetGuid() const noexcept;
        void SetAssetGuid(std::string guid);
        bool GetPlayOnStart() const noexcept;
        bool GetSyncCharacterMotion() const noexcept;

    private:
        bool enabled_ = true;
        std::string assetGuid_{};
        bool playOnStart_ = true;
        bool syncCharacterMotion_ = true;
        std::string lastDiagnostic_{};
    };

} // namespace HIKARI
