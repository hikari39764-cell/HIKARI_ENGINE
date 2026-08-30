#pragma once

#include <string>
#include <string_view>

#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

    class CharacterInputComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "CharacterInputComponent";
        }

        void Serialize(nlohmann::json& out) const override {
            out["enabled"] = enabled_;
            out["priority"] = priority_;
            out["moveAction"] = moveActionId_;
            out["lookAction"] = lookActionId_;
            out["jumpAction"] = jumpActionId_;
            out["sprintAction"] = sprintActionId_;
        }
        void Deserialize(const nlohmann::json& in) override {
            enabled_ = in.value("enabled", enabled_);
            priority_ = in.value("priority", priority_);
            moveActionId_ = in.value("moveAction", moveActionId_);
            lookActionId_ = in.value("lookAction", lookActionId_);
            jumpActionId_ = in.value("jumpAction", jumpActionId_);
            sprintActionId_ = in.value("sprintAction", sprintActionId_);
        }
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept { return enabled_; }
        int GetPriority() const noexcept { return priority_; }
        const std::string& GetMoveActionId() const noexcept {
            return moveActionId_;
        }
        const std::string& GetLookActionId() const noexcept {
            return lookActionId_;
        }
        const std::string& GetJumpActionId() const noexcept {
            return jumpActionId_;
        }
        const std::string& GetSprintActionId() const noexcept {
            return sprintActionId_;
        }

    private:
        bool enabled_ = true;
        int priority_ = 100;
        std::string moveActionId_{ "Gameplay.Move" };
        std::string lookActionId_{ "Gameplay.Look" };
        std::string jumpActionId_{ "Gameplay.Jump" };
        std::string sprintActionId_{ "Gameplay.Sprint" };
    };

} // namespace HIKARI
