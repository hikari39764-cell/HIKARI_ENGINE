#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "Authoring/HIKARI_FieldDraw.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

// 作者値（シーン文書の properties と同じ形）。項目表（kCharacterInputFields）が保存・読み取り・検査器・説明を決める。
struct CharacterInputSettings {
    bool enabled = true;
    int priority = 100;
    std::string moveAction{"Gameplay.Move"};
    std::string lookAction{"Gameplay.Look"};
    std::string jumpAction{"Gameplay.Jump"};
    std::string sprintAction{"Gameplay.Sprint"};
};

inline constexpr auto kCharacterInputFields = [](auto&& field) {
    using S = CharacterInputSettings;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Enabled"});
    field("priority", &S::priority, Label{"Control Priority"});
    field("moveAction", &S::moveAction, Label{"Move Action"},
          Draw{FIELD_DRAW::InputActionPicker<INPUT::InputActionValueType::Axis2D>{}});
    field("lookAction", &S::lookAction, Label{"Look Action"},
          Draw{FIELD_DRAW::InputActionPicker<INPUT::InputActionValueType::Axis2D>{}});
    field("jumpAction", &S::jumpAction, Label{"Jump Action"},
          Draw{FIELD_DRAW::InputActionPicker<INPUT::InputActionValueType::Button>{}});
    field("sprintAction", &S::sprintAction, Label{"Sprint Action"},
          Draw{FIELD_DRAW::InputActionPicker<INPUT::InputActionValueType::Button>{}});
};

class CharacterInputComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "CharacterInputComponent"; }

    void Serialize(nlohmann::json& out) const override {
        out = nlohmann::json::object();
        JSONFIELDS::Write(out, settings_, kCharacterInputFields);
    }
    void Deserialize(const nlohmann::json& in) override {
        CharacterInputSettings decoded{};
        JSONFIELDS::Read(in, decoded, kCharacterInputFields);
        settings_ = std::move(decoded);
    }
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept { return settings_.enabled; }
    int GetPriority() const noexcept { return settings_.priority; }
    const std::string& GetMoveActionId() const noexcept { return settings_.moveAction; }
    const std::string& GetLookActionId() const noexcept { return settings_.lookAction; }
    const std::string& GetJumpActionId() const noexcept { return settings_.jumpAction; }
    const std::string& GetSprintActionId() const noexcept { return settings_.sprintAction; }

private:
    CharacterInputSettings settings_{};
};

} // namespace HIKARI
