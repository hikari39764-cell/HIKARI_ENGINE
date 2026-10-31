#pragma once

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "HIKARI_IComponent.h"

namespace HIKARI {

// 作者値（シーン文書の properties と同じ形）。項目表（kDialogueInteractorFields）が保存・読み取り・検査器・説明を決める。
struct DialogueInteractorSettings {
    bool enabled = true;
};

inline constexpr auto kDialogueInteractorFields = [](auto&& field) {
    using S = DialogueInteractorSettings;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Enabled"}, Help{"This actor can activate Dialogue Trigger volumes."});
};

class DialogueInteractorComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "DialogueInteractorComponent"; }

    void Serialize(nlohmann::json& out) const override {
        out = nlohmann::json::object();
        JSONFIELDS::Write(out, settings_, kDialogueInteractorFields);
    }
    void Deserialize(const nlohmann::json& in) override {
        DialogueInteractorSettings decoded{};
        JSONFIELDS::Read(in, decoded, kDialogueInteractorFields);
        settings_ = decoded;
    }
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept { return settings_.enabled; }

private:
    DialogueInteractorSettings settings_{};
};

} // namespace HIKARI
