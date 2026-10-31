#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/HIKARI_AssetGuid.h"
#include "Assets/HIKARI_AssetTypeEnum.h"
#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_SceneObjectId.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

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

inline constexpr TEXT::EnumName<VfxSpawnTarget> kVfxSpawnTargetNames[] = {
    {VfxSpawnTarget::Owner, "Owner"},
    {VfxSpawnTarget::EventSource, "EventSource"},
    {VfxSpawnTarget::FixedObject, "FixedObject"},
    {VfxSpawnTarget::EventPayloadPosition, "EventPayloadPosition"},
};
inline constexpr TEXT::EnumName<VfxLoopMode> kVfxLoopModeNames[] = {
    {VfxLoopMode::AssetDefault, "AssetDefault"},
    {VfxLoopMode::ForceLoop, "ForceLoop"},
    {VfxLoopMode::ForceOneShot, "ForceOneShot"},
};
inline constexpr TEXT::EnumName<VfxRetriggerPolicy> kVfxRetriggerPolicyNames[] = {
    {VfxRetriggerPolicy::Parallel, "Parallel"},
    {VfxRetriggerPolicy::Restart, "Restart"},
    {VfxRetriggerPolicy::IgnoreIfPlaying, "IgnoreIfPlaying"},
};

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

struct AuthoringFieldDescriptor;

// 槽位の variableBindings の編集欄（kVfxPlayerSlotFields の Draw）。効果資産が公開する入力ごとに 1 行で、型の合う
// 実行時変数を選ぶ（空は結合なし）。bindings の追加・更新・削除はこの関数が行う。
bool DrawVfxVariableBindings(IInspectorBuilder& builder, const AuthoringFieldDescriptor& field,
                             std::vector<VfxVariableBinding>& bindings, const VfxPlayerSlot& slot);

// 作者値。シーン文書の properties と同じ形で、項目表（kVfxPlayerFields）が保存・読み取り・検査器・説明を決める。
struct VfxPlayerSettings {
    bool enabled = true;
    std::vector<VfxPlayerSlot> slots{};
};

inline constexpr auto kVfxOffsetFields = [](auto&& field) {
    using S = VFX::VfxTransformData;
    using namespace JSONFIELDS;
    field("position", &S::position, Label{"Offset Position"});
    field("rotationEulerDegrees", &S::rotationEulerDegrees, Label{"Offset Rotation"});
    field("scale", &S::scale, Label{"Offset Scale"});
};

inline constexpr auto kVfxVariableBindingFields = [](auto&& field) {
    using S = VfxVariableBinding;
    using namespace JSONFIELDS;
    field("inputId", &S::inputId, Label{"Exposed Input"});
    field("variableId", &S::variableId, Label{"Runtime Variable"}, VariableRef{});
};

inline constexpr auto kVfxPlayerSlotFields = [](auto&& field) {
    using S = VfxPlayerSlot;
    using namespace JSONFIELDS;
    field("name", &S::name, Label{"Name"}, Common{});
    field("enabled", &S::enabled, Label{"Slot Enabled"}, Common{});
    field("effectAssetGuid", &S::effectAssetGuid, Label{"Effect"}, AssetRef{AssetType::VfxEffect}, Common{});
    field("playOnStart", &S::playOnStart, Label{"Play On Start"}, Common{});
    field("triggerEventId", &S::triggerEventId, Label{"Trigger Event"}, EventRef{}, Common{});
    field("spawnTarget", &S::spawnTarget, kVfxSpawnTargetNames, Label{"Spawn Target"}, Common{});
    field("fixedObjectId", &S::fixedObjectId, Label{"Fixed Object"}, ObjectRef{}, Common{},
          Show{[](const S& slot) { return slot.spawnTarget == VfxSpawnTarget::FixedObject; }});
    field("followTarget", &S::followTarget, Label{"Follow Target"});
    field("offset", &S::offset, Nested{kVfxOffsetFields});
    field("variableBindings", &S::variableBindings, Label{"Input Binding"}, Elements{kVfxVariableBindingFields},
          Draw{&DrawVfxVariableBindings});
    field("loopMode", &S::loopMode, kVfxLoopModeNames, Label{"Loop Mode"}, Advanced{});
    field("retriggerPolicy", &S::retriggerPolicy, kVfxRetriggerPolicyNames, Label{"Retrigger"}, Advanced{});
    field("randomSeed", &S::randomSeed, Label{"Random Seed"}, Advanced{});
    // 槽位の安定した ID。智能体の slot.* コマンドが slotId で指す。
    field("id", &S::id, Label{"Slot ID"}, ReadOnly{}, Advanced{});
};

inline constexpr auto kVfxPlayerFields = [](auto&& field) {
    using S = VfxPlayerSettings;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Enabled"}, Common{});
    field("slots", &S::slots, Label{"VFX Slot"}, Elements{kVfxPlayerSlotFields});
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

    VfxPlayerSettings settings_{};
    uint64_t nextSlotId_ = 1u;
    std::vector<VfxSlotCommand> pendingCommands_{};
};

} // namespace HIKARI
