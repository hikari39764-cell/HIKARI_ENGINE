#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Assets/HIKARI_AssetGuid.h"
#include "HIKARI_IComponent.h"

namespace HIKARI {

enum class DialogueTriggerActivation : uint8_t {
    OnEnter = 0u,
    Interact = 1u,
};

const char* ToString(DialogueTriggerActivation value) noexcept;
bool TryParseDialogueTriggerActivation(std::string_view text, DialogueTriggerActivation& outValue) noexcept;

class DialogueTriggerComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "DialogueTriggerComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void BuildInspector(IInspectorBuilder& builder) override;

    bool IsEnabled() const noexcept { return enabled_; }
    const AssetGuid& GetDialogueAssetGuid() const noexcept { return dialogueAssetGuid_; }
    const std::string& GetLocale() const noexcept { return locale_; }
    DialogueTriggerActivation GetActivation() const noexcept { return activation_; }
    const std::string& GetInteractActionId() const noexcept { return interactActionId_; }
    bool ShouldPauseWorld() const noexcept { return pauseWorld_; }
    bool IsOneShot() const noexcept { return oneShot_; }
    bool IsConsumed() const noexcept { return consumed_; }
    void MarkConsumed() noexcept { consumed_ = true; }
    void SetConsumed(bool consumed) noexcept { consumed_ = consumed; }

private:
    AssetGuid dialogueAssetGuid_{};
    std::string locale_{"ja-JP"};
    std::string interactActionId_{"Gameplay.Interact"};
    DialogueTriggerActivation activation_ = DialogueTriggerActivation::OnEnter;
    bool enabled_ = true;
    bool pauseWorld_ = true;
    bool oneShot_ = false;
    bool consumed_ = false;
};

} // namespace HIKARI
