#pragma once

#include "HIKARI_IComponent.h"

namespace HIKARI {

    class DialogueInteractorComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "DialogueInteractorComponent";
        }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept { return enabled_; }

    private:
        bool enabled_ = true;
    };

} // namespace HIKARI
