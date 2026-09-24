#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

struct VfxSlotId {
    uint64_t value = 0u;

    bool IsValid() const noexcept { return value != 0u; }
    bool operator==(const VfxSlotId&) const noexcept = default;
};

enum class VfxSpawnTarget : uint8_t {
    Owner,
    EventSource,
    FixedObject,
    EventPayloadPosition,
};

enum class VfxLoopMode : uint8_t {
    AssetDefault,
    ForceLoop,
    ForceOneShot,
};

enum class VfxRetriggerPolicy : uint8_t {
    Parallel,
    Restart,
    IgnoreIfPlaying,
};

std::string_view ToString(VfxSpawnTarget value) noexcept;
std::string_view ToString(VfxLoopMode value) noexcept;
std::string_view ToString(VfxRetriggerPolicy value) noexcept;
bool TryParseVfxSpawnTarget(std::string_view text, VfxSpawnTarget& outValue) noexcept;
bool TryParseVfxLoopMode(std::string_view text, VfxLoopMode& outValue) noexcept;
bool TryParseVfxRetriggerPolicy(std::string_view text, VfxRetriggerPolicy& outValue) noexcept;

struct VfxVariableBinding {
    std::string inputId{};
    std::string variableId{};
};

struct VfxPlayerSlot {
    VfxSlotId id{};
    std::string name{};
    bool enabled = true;
    AssetGuid effectAssetGuid{};
    bool playOnStart = false;
    std::string triggerEventId{};
    VfxSpawnTarget spawnTarget = VfxSpawnTarget::Owner;
    SceneObjectId fixedObjectId{};
    bool followTarget = true;
    VFX::VfxTransformData offset{};
    VfxLoopMode loopMode = VfxLoopMode::AssetDefault;
    VfxRetriggerPolicy retriggerPolicy = VfxRetriggerPolicy::Parallel;
    uint32_t randomSeed = 0u;
    std::vector<VfxVariableBinding> variableBindings{};
};

enum class VfxSlotCommandKind : uint8_t {
    Play,
    Stop,
};

struct VfxSlotCommand {
    VfxSlotCommandKind kind = VfxSlotCommandKind::Play;
    VfxSlotId slotId{};
};

class VfxPlayerComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "VfxPlayerComponent"; }

    void OnAttach() override;
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    bool TryDeserialize(const nlohmann::json& in, std::string& outDiagnostic) override;
    void BuildInspector(IInspectorBuilder& builder) override;

    bool IsEnabled() const noexcept;
    void SetEnabled(bool enabled) noexcept;

    const std::vector<VfxPlayerSlot>& GetSlots() const noexcept;
    VfxPlayerSlot* FindSlot(VfxSlotId id) noexcept;
    const VfxPlayerSlot* FindSlot(VfxSlotId id) const noexcept;
    VfxPlayerSlot& AddSlot(std::string name = {});
    VfxPlayerSlot& AppendSlot(VfxPlayerSlot slot);
    VfxPlayerSlot* DuplicateSlot(VfxSlotId id);
    bool RemoveSlot(VfxSlotId id);
    bool MoveSlot(VfxSlotId id, size_t targetIndex);

    bool RequestPlay(VfxSlotId id);
    bool RequestStop(VfxSlotId id);
    std::vector<VfxSlotCommand> ConsumePendingCommands();

private:
    VfxSlotId AllocateSlotId() noexcept;
    void NormalizeSlots();

    bool enabled_ = true;
    std::vector<VfxPlayerSlot> slots_{};
    uint64_t nextSlotId_ = 1u;
    std::vector<VfxSlotCommand> pendingCommands_{};
};

} // namespace HIKARI
