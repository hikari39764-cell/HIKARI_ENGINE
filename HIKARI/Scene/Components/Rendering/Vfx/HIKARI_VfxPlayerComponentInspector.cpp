#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponent.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetTypes.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponentAuthoringInternal.h"

namespace HIKARI {
namespace {
bool InputBoundByOther(const VfxPlayerSlot& slot, std::string_view inputId, size_t bindingIndex) noexcept {
    for (size_t index = 0u; index < slot.variableBindings.size(); ++index) {
        if (index != bindingIndex && slot.variableBindings[index].inputId == inputId) {
            return true;
        }
    }
    return false;
}
} // namespace

void VfxPlayerComponent::BuildInspector(IInspectorBuilder& builder) {
    using VfxPlayerAuthoringInternal::FindProgramInput;
    using VfxPlayerAuthoringInternal::ResolveProgram;
    using VfxPlayerAuthoringInternal::ToBindingType;
    using D = AuthoringFieldDescriptor;

    (void)builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled"}, enabled_);
    if (builder.Button("Add VFX Slot")) {
        (void)AddSlot();
    }

    VfxSlotId removeId{};
    VfxSlotId duplicateId{};
    VfxSlotId moveId{};
    size_t moveTarget = 0u;
    for (size_t slotIndex = 0u; slotIndex < slots_.size(); ++slotIndex) {
        VfxPlayerSlot& slot = slots_[slotIndex];
        const std::string suffix = "##vfx-slot-" + std::to_string(slot.id.value);
        if (!builder.Section(slot.name + suffix, true)) {
            continue;
        }
        // 槽位の項目 ID は slots.<槽位 ID>.<キー名>（並べ替えても変わらない）。一時文字列は呼び出しの間だけ使う。
        const std::string prefix = "slots." + std::to_string(slot.id.value) + ".";
        const auto id = [&prefix](std::string_view key) { return prefix + std::string(key); };
        (void)builder.String(D{id("name"), "Name"}, slot.name);
        (void)builder.Bool(D{id("enabled"), "Slot Enabled"}, slot.enabled);
        (void)builder.AssetIdPicker(D{id("effectAssetGuid"), "Effect"}, AssetType::VfxEffect,
                                    slot.effectAssetGuid.value);
        (void)builder.Bool(D{id("playOnStart"), "Play On Start"}, slot.playOnStart);
        const AuthoringEventPayloadPolicy eventPolicy = slot.spawnTarget == VfxSpawnTarget::EventPayloadPosition
                                                            ? AuthoringEventPayloadPolicy::Exact
                                                            : AuthoringEventPayloadPolicy::Any;
        (void)builder.RuntimeEventIdPicker(D{id("triggerEventId"), "Trigger Event"}, eventPolicy,
                                           BINDING::ValueType::Float3, slot.triggerEventId);

        static constexpr std::array<std::string_view, std::size(kVfxSpawnTargetNames)> spawnLabels{
            "Owner", "Event Source", "Fixed Object", "Event Payload Position"};
        (void)EnumChoice(builder, D{id("spawnTarget"), "Spawn Target"}, slot.spawnTarget, kVfxSpawnTargetNames,
                         &spawnLabels);
        if (slot.spawnTarget == VfxSpawnTarget::FixedObject) {
            (void)builder.SceneObjectIdPicker(D{id("fixedObjectId"), "Fixed Object"}, slot.fixedObjectId);
        }
        (void)builder.Bool(D{id("followTarget"), "Follow Target"}, slot.followTarget);
        MATH::Vec3& position = slot.offset.position;
        MATH::Vec3& rotation = slot.offset.rotationEulerDegrees;
        MATH::Vec3& scale = slot.offset.scale;
        (void)builder.Vec3(D{id("offset.position"), "Offset Position"}, position.x, position.y, position.z);
        (void)builder.Vec3(D{id("offset.rotationEulerDegrees"), "Offset Rotation"}, rotation.x, rotation.y, rotation.z);
        (void)builder.Vec3(D{id("offset.scale"), "Offset Scale"}, scale.x, scale.y, scale.z);

        static constexpr std::array<std::string_view, std::size(kVfxLoopModeNames)> loopLabels{
            "Asset Default", "Force Loop", "Force One Shot"};
        (void)EnumChoice(builder, D{id("loopMode"), "Loop Mode"}, slot.loopMode, kVfxLoopModeNames, &loopLabels);
        static constexpr std::array<std::string_view, std::size(kVfxRetriggerPolicyNames)> retriggerLabels{
            "Parallel", "Restart", "Ignore If Playing"};
        (void)EnumChoice(builder, D{id("retriggerPolicy"), "Retrigger"}, slot.retriggerPolicy, kVfxRetriggerPolicyNames,
                         &retriggerLabels);
        int randomSeed =
            static_cast<int>((std::min)(slot.randomSeed, static_cast<uint32_t>((std::numeric_limits<int>::max)())));
        if (builder.Int(D{id("randomSeed"), "Random Seed"}, randomSeed)) {
            slot.randomSeed = static_cast<uint32_t>((std::max)(randomSeed, 0));
        }

        const VFX::VfxProgram* program = ResolveProgram(builder.GetContext().assetRegistry, slot);
        if (program == nullptr) {
            builder.Text("Import a VFX asset to expose its typed inputs.");
        } else if (builder.Button("Add Input Binding" + suffix)) {
            const auto available = std::find_if(
                program->inputs.begin(), program->inputs.end(), [&slot](const VFX::VfxProgramInput& input) {
                    return std::none_of(
                        slot.variableBindings.begin(), slot.variableBindings.end(),
                        [&input](const VfxVariableBinding& binding) { return binding.inputId == input.id; });
                });
            if (available != program->inputs.end()) {
                slot.variableBindings.push_back({available->id, {}});
            }
        }
        size_t removeBinding = slot.variableBindings.size();
        for (size_t bindingIndex = 0u; bindingIndex < slot.variableBindings.size(); ++bindingIndex) {
            VfxVariableBinding& binding = slot.variableBindings[bindingIndex];
            const std::string bindingSuffix = suffix + "-binding-" + std::to_string(bindingIndex);
            const std::string bindingId = id("variableBindings." + std::to_string(bindingIndex) + ".");
            const VFX::VfxProgramInput* selectedInput = FindProgramInput(program, binding.inputId);
            if (program != nullptr && !program->inputs.empty()) {
                std::vector<std::string> labels{};
                std::vector<const VFX::VfxProgramInput*> choices{};
                for (const VFX::VfxProgramInput& input : program->inputs) {
                    if (input.id != binding.inputId && InputBoundByOther(slot, input.id, bindingIndex)) {
                        continue;
                    }
                    choices.push_back(&input);
                    labels.push_back(input.id + " (" + std::string(VFX::ToString(input.type)) + ")");
                }
                std::vector<AuthoringChoice> inputChoices{};
                inputChoices.reserve(labels.size());
                for (size_t choiceIndex = 0u; choiceIndex < choices.size(); ++choiceIndex) {
                    inputChoices.push_back({choices[choiceIndex]->id, labels[choiceIndex]});
                }
                int selectedIndex = 0;
                for (size_t choiceIndex = 0u; choiceIndex < choices.size(); ++choiceIndex) {
                    if (choices[choiceIndex]->id == binding.inputId) {
                        selectedIndex = static_cast<int>(choiceIndex);
                        break;
                    }
                }
                if (!choices.empty() &&
                    builder.Choice(D{bindingId + "inputId", "Exposed Input"}, selectedIndex, inputChoices)) {
                    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(choices.size() - 1u));
                    binding.inputId = choices[static_cast<size_t>(selectedIndex)]->id;
                    selectedInput = choices[static_cast<size_t>(selectedIndex)];
                }
            }
            if (selectedInput != nullptr) {
                (void)builder.RuntimeVariableIdPicker(D{bindingId + "variableId", "Runtime Variable"},
                                                      ToBindingType(selectedInput->type), binding.variableId);
            } else {
                builder.Text("This exposed input no longer exists in the VFX asset.");
            }
            if (builder.Button("Remove Binding" + bindingSuffix)) {
                removeBinding = bindingIndex;
            }
        }
        if (removeBinding < slot.variableBindings.size()) {
            slot.variableBindings.erase(slot.variableBindings.begin() + removeBinding);
        }

        if (builder.Button("Duplicate" + suffix))
            duplicateId = slot.id;
        if (slotIndex > 0u && builder.Button("Move Up" + suffix)) {
            moveId = slot.id;
            moveTarget = slotIndex - 1u;
        }
        if (slotIndex + 1u < slots_.size() && builder.Button("Move Down" + suffix)) {
            moveId = slot.id;
            moveTarget = slotIndex + 1u;
        }
        if (builder.Button("Remove Slot" + suffix))
            removeId = slot.id;
    }
    if (duplicateId.IsValid())
        (void)DuplicateSlot(duplicateId);
    if (moveId.IsValid())
        (void)MoveSlot(moveId, moveTarget);
    if (removeId.IsValid())
        (void)RemoveSlot(removeId);
}

} // namespace HIKARI
