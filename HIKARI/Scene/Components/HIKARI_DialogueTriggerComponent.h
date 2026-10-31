#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Core/HIKARI_AssetGuid.h"
#include "Assets/HIKARI_AssetTypeEnum.h"
#include "Authoring/HIKARI_FieldDraw.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "HIKARI_IComponent.h"
#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI {

enum class DialogueTriggerActivation : uint8_t {
    OnEnter = 0u,
    Interact = 1u,
};

inline constexpr TEXT::EnumName<DialogueTriggerActivation> kDialogueTriggerActivationNames[] = {
    {DialogueTriggerActivation::OnEnter, "OnEnter"},
    {DialogueTriggerActivation::Interact, "Interact"},
};

// 作者値（シーン文書の properties と同じ形）。項目表（kDialogueTriggerFields）が保存・読み取り・検査器・説明を決める。
struct DialogueTriggerSettings {
    bool enabled = true;
    AssetGuid dialogueAssetGuid{};
    std::string locale{"ja-JP"};
    DialogueTriggerActivation activation = DialogueTriggerActivation::OnEnter;
    std::string interactAction{"Gameplay.Interact"};
    bool pauseWorld = true;
    bool oneShot = false;
};

inline constexpr auto kDialogueTriggerFields = [](auto&& field) {
    using S = DialogueTriggerSettings;
    using namespace JSONFIELDS;
    constexpr auto interact = [](const S& s) { return s.activation == DialogueTriggerActivation::Interact; };
    field("enabled", &S::enabled, Label{"Enabled"});
    field("dialogueAssetGuid", &S::dialogueAssetGuid, Label{"Dialogue Asset"}, AssetRef{AssetType::Dialogue});
    field("locale", &S::locale, Label{"Locale"});
    field("activation", &S::activation, kDialogueTriggerActivationNames, Label{"Activation"});
    field("interactAction", &S::interactAction, Label{"Interact Action"},
          Draw{FIELD_DRAW::InputActionPicker<INPUT::InputActionValueType::Button>{}}, Show{interact});
    field("pauseWorld", &S::pauseWorld, Label{"Pause World During Dialogue"});
    field("oneShot", &S::oneShot, Label{"One Shot"});
};

class DialogueTriggerComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "DialogueTriggerComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept { return settings_.enabled; }
    const AssetGuid& GetDialogueAssetGuid() const noexcept { return settings_.dialogueAssetGuid; }
    const std::string& GetLocale() const noexcept { return settings_.locale; }
    DialogueTriggerActivation GetActivation() const noexcept { return settings_.activation; }
    const std::string& GetInteractActionId() const noexcept { return settings_.interactAction; }
    bool ShouldPauseWorld() const noexcept { return settings_.pauseWorld; }
    bool IsOneShot() const noexcept { return settings_.oneShot; }
    bool IsConsumed() const noexcept { return consumed_; }
    void MarkConsumed() noexcept { consumed_ = true; }
    void SetConsumed(bool consumed) noexcept { consumed_ = consumed; }

private:
    DialogueTriggerSettings settings_{};
    bool consumed_ = false;
};

} // namespace HIKARI
