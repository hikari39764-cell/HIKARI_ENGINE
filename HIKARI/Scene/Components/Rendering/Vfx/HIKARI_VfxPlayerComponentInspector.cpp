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
        bool InputBoundByOther(
            const VfxPlayerSlot& slot,
            std::string_view inputId,
            size_t bindingIndex) noexcept {

            for (size_t index = 0u;
                index < slot.variableBindings.size();
                ++index) {
                if (index != bindingIndex &&
                    slot.variableBindings[index].inputId == inputId) {
                    return true;
                }
            }
            return false;
        }
    }

    void VfxPlayerComponent::BuildInspector(
        IInspectorBuilder& builder) {

        using VfxPlayerAuthoringInternal::FindProgramInput;
        using VfxPlayerAuthoringInternal::ResolveProgram;
        using VfxPlayerAuthoringInternal::ToBindingType;

        (void)builder.Bool("Enabled", enabled_);
        if (builder.Button("Add VFX Slot")) {
            (void)AddSlot();
        }

        VfxSlotId removeId{};
        VfxSlotId duplicateId{};
        VfxSlotId moveId{};
        size_t moveTarget = 0u;
        for (size_t slotIndex = 0u;
            slotIndex < slots_.size();
            ++slotIndex) {
            VfxPlayerSlot& slot = slots_[slotIndex];
            const std::string suffix = "##vfx-slot-" +
                std::to_string(slot.id.value);
            if (!builder.Section(slot.name + suffix, true)) {
                continue;
            }
            (void)builder.String("Name" + suffix, slot.name);
            (void)builder.Bool("Slot Enabled" + suffix, slot.enabled);
            (void)builder.AssetIdPicker(
                "Effect" + suffix,
                AssetType::VfxEffect,
                slot.effectAssetGuid.value);
            (void)builder.Bool(
                "Play On Start" + suffix,
                slot.playOnStart);
            const AuthoringEventPayloadPolicy eventPolicy =
                slot.spawnTarget == VfxSpawnTarget::EventPayloadPosition
                ? AuthoringEventPayloadPolicy::Exact
                : AuthoringEventPayloadPolicy::Any;
            (void)builder.RuntimeEventIdPicker(
                "Trigger Event" + suffix,
                eventPolicy,
                BINDING::ValueType::Float3,
                slot.triggerEventId);

            static constexpr std::array<const char*, 4> spawnChoices{
                "Owner", "Event Source", "Fixed Object", "Event Payload Position"
            };
            int spawnIndex = static_cast<int>(slot.spawnTarget);
            if (builder.Choice(
                    "Spawn Target" + suffix,
                    spawnIndex,
                    spawnChoices)) {
                slot.spawnTarget = static_cast<VfxSpawnTarget>(
                    std::clamp(spawnIndex, 0, 3));
            }
            if (slot.spawnTarget == VfxSpawnTarget::FixedObject) {
                (void)builder.SceneObjectIdPicker(
                    "Fixed Object" + suffix,
                    slot.fixedObjectId);
            }
            (void)builder.Bool(
                "Follow Target" + suffix,
                slot.followTarget);
            (void)builder.Float(
                "Offset Position X" + suffix,
                slot.offset.position.x);
            (void)builder.Float(
                "Offset Position Y" + suffix,
                slot.offset.position.y);
            (void)builder.Float(
                "Offset Position Z" + suffix,
                slot.offset.position.z);
            (void)builder.Float(
                "Offset Rotation X" + suffix,
                slot.offset.rotationEulerDegrees.x);
            (void)builder.Float(
                "Offset Rotation Y" + suffix,
                slot.offset.rotationEulerDegrees.y);
            (void)builder.Float(
                "Offset Rotation Z" + suffix,
                slot.offset.rotationEulerDegrees.z);
            (void)builder.Float(
                "Offset Scale X" + suffix,
                slot.offset.scale.x);
            (void)builder.Float(
                "Offset Scale Y" + suffix,
                slot.offset.scale.y);
            (void)builder.Float(
                "Offset Scale Z" + suffix,
                slot.offset.scale.z);

            static constexpr std::array<const char*, 3> loopChoices{
                "Asset Default", "Force Loop", "Force One Shot"
            };
            int loopIndex = static_cast<int>(slot.loopMode);
            if (builder.Choice(
                    "Loop Mode" + suffix,
                    loopIndex,
                    loopChoices)) {
                slot.loopMode = static_cast<VfxLoopMode>(
                    std::clamp(loopIndex, 0, 2));
            }
            static constexpr std::array<const char*, 3> retriggerChoices{
                "Parallel", "Restart", "Ignore If Playing"
            };
            int retriggerIndex = static_cast<int>(slot.retriggerPolicy);
            if (builder.Choice(
                    "Retrigger" + suffix,
                    retriggerIndex,
                    retriggerChoices)) {
                slot.retriggerPolicy = static_cast<VfxRetriggerPolicy>(
                    std::clamp(retriggerIndex, 0, 2));
            }
            int randomSeed = static_cast<int>((std::min)(
                slot.randomSeed,
                static_cast<uint32_t>(
                    (std::numeric_limits<int>::max)())));
            if (builder.Int("Random Seed" + suffix, randomSeed)) {
                slot.randomSeed = static_cast<uint32_t>(
                    (std::max)(randomSeed, 0));
            }

            const VFX::VfxProgram* program = ResolveProgram(
                builder.GetContext().assetRegistry,
                slot);
            if (program == nullptr) {
                builder.Text(
                    "Import a VFX asset to expose its typed inputs.");
            } else if (builder.Button("Add Input Binding" + suffix)) {
                const auto available = std::find_if(
                    program->inputs.begin(),
                    program->inputs.end(),
                    [&slot](const VFX::VfxProgramInput& input) {
                        return std::none_of(
                            slot.variableBindings.begin(),
                            slot.variableBindings.end(),
                            [&input](const VfxVariableBinding& binding) {
                                return binding.inputId == input.id;
                            });
                    });
                if (available != program->inputs.end()) {
                    slot.variableBindings.push_back({ available->id, {} });
                }
            }
            size_t removeBinding = slot.variableBindings.size();
            for (size_t bindingIndex = 0u;
                bindingIndex < slot.variableBindings.size();
                ++bindingIndex) {
                VfxVariableBinding& binding =
                    slot.variableBindings[bindingIndex];
                const std::string bindingSuffix = suffix + "-binding-" +
                    std::to_string(bindingIndex);
                const VFX::VfxProgramInput* selectedInput =
                    FindProgramInput(program, binding.inputId);
                if (program != nullptr && !program->inputs.empty()) {
                    std::vector<std::string> labels{};
                    std::vector<const VFX::VfxProgramInput*> choices{};
                    for (const VFX::VfxProgramInput& input :
                        program->inputs) {
                        if (input.id != binding.inputId &&
                            InputBoundByOther(
                                slot,
                                input.id,
                                bindingIndex)) {
                            continue;
                        }
                        choices.push_back(&input);
                        labels.push_back(
                            input.id + " (" +
                            std::string(VFX::ToString(input.type)) + ")");
                    }
                    std::vector<const char*> choiceLabels{};
                    choiceLabels.reserve(labels.size());
                    for (const std::string& label : labels) {
                        choiceLabels.push_back(label.c_str());
                    }
                    int selectedIndex = 0;
                    for (size_t choiceIndex = 0u;
                        choiceIndex < choices.size();
                        ++choiceIndex) {
                        if (choices[choiceIndex]->id == binding.inputId) {
                            selectedIndex = static_cast<int>(choiceIndex);
                            break;
                        }
                    }
                    if (!choices.empty() && builder.Choice(
                            "Exposed Input" + bindingSuffix,
                            selectedIndex,
                            choiceLabels)) {
                        selectedIndex = std::clamp(
                            selectedIndex,
                            0,
                            static_cast<int>(choices.size() - 1u));
                        binding.inputId = choices[
                            static_cast<size_t>(selectedIndex)]->id;
                        selectedInput = choices[
                            static_cast<size_t>(selectedIndex)];
                    }
                }
                if (selectedInput != nullptr) {
                    (void)builder.RuntimeVariableIdPicker(
                        "Runtime Variable" + bindingSuffix,
                        ToBindingType(selectedInput->type),
                        binding.variableId);
                } else {
                    builder.Text(
                        "This exposed input no longer exists in the VFX asset.");
                }
                if (builder.Button("Remove Binding" + bindingSuffix)) {
                    removeBinding = bindingIndex;
                }
            }
            if (removeBinding < slot.variableBindings.size()) {
                slot.variableBindings.erase(
                    slot.variableBindings.begin() + removeBinding);
            }

            if (builder.Button("Duplicate" + suffix)) duplicateId = slot.id;
            if (slotIndex > 0u && builder.Button("Move Up" + suffix)) {
                moveId = slot.id;
                moveTarget = slotIndex - 1u;
            }
            if (slotIndex + 1u < slots_.size() &&
                builder.Button("Move Down" + suffix)) {
                moveId = slot.id;
                moveTarget = slotIndex + 1u;
            }
            if (builder.Button("Remove Slot" + suffix)) removeId = slot.id;
        }
        if (duplicateId.IsValid()) (void)DuplicateSlot(duplicateId);
        if (moveId.IsValid()) (void)MoveSlot(moveId, moveTarget);
        if (removeId.IsValid()) (void)RemoveSlot(removeId);
    }

} // namespace HIKARI
