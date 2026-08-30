#pragma once

#include "HIKARI_IComponent.h"

namespace HIKARI {

    class DialogueInteractorComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "DialogueInteractorComponent";
        }

        void Serialize(nlohmann::json& out) const override {
            out["enabled"] = enabled_;
        }
        void Deserialize(const nlohmann::json& in) override {
            enabled_ = in.value("enabled", enabled_);
        }
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept { return enabled_; }

    private:
        bool enabled_ = true;
    };

} // namespace HIKARI
