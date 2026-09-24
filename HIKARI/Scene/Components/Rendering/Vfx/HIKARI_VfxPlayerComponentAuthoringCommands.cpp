#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponentAuthoring.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponentAuthoringInternal.h"

namespace HIKARI {
namespace {
bool ReadSlotId(const nlohmann::json& parameters, VfxSlotId& outId) {
    const auto found = parameters.find("slotId");
    if (found == parameters.end() || !found->is_number_integer()) {
        return false;
    }
    const int64_t value = found->get<int64_t>();
    if (value <= 0) {
        return false;
    }
    outId.value = static_cast<uint64_t>(value);
    return outId.IsValid();
}

bool ConfigureSlot(VfxPlayerSlot& slot, const nlohmann::json& values, std::string& outError) {
    if (!values.is_object()) {
        outError = "values must be an object";
        return false;
    }
    static constexpr std::array<std::string_view, 12> knownFields{
        "name",          "effectAssetGuid", "enabled",     "playOnStart", "triggerEventId",  "followTarget",
        "fixedObjectId", "randomSeed",      "spawnTarget", "loopMode",    "retriggerPolicy", "offset"};
    for (const auto& [name, value] : values.items()) {
        (void)value;
        if (std::find(knownFields.begin(), knownFields.end(), name) == knownFields.end()) {
            outError = "unknown VFX slot field: " + name;
            return false;
        }
    }

    VfxPlayerSlot configured = slot;
    const auto readString = [&values, &outError](const char* key, std::string& target) {
        const auto found = values.find(key);
        if (found == values.end())
            return true;
        if (!found->is_string()) {
            outError = std::string(key) + " must be a string";
            return false;
        }
        target = found->get<std::string>();
        return true;
    };
    const auto readBool = [&values, &outError](const char* key, bool& target) {
        const auto found = values.find(key);
        if (found == values.end())
            return true;
        if (!found->is_boolean()) {
            outError = std::string(key) + " must be a bool";
            return false;
        }
        target = found->get<bool>();
        return true;
    };
    const auto readUnsigned = [&values, &outError](const char* key, uint64_t maximum, uint64_t& target) {
        const auto found = values.find(key);
        if (found == values.end())
            return true;
        uint64_t parsed = 0u;
        if (found->is_number_unsigned()) {
            parsed = found->get<uint64_t>();
        } else if (found->is_number_integer()) {
            const int64_t signedValue = found->get<int64_t>();
            if (signedValue < 0) {
                outError = std::string(key) + " must not be negative";
                return false;
            }
            parsed = static_cast<uint64_t>(signedValue);
        } else {
            outError = std::string(key) + " must be an integer";
            return false;
        }
        if (parsed > maximum) {
            outError = std::string(key) + " is out of range";
            return false;
        }
        target = parsed;
        return true;
    };
    uint64_t fixedObjectId = configured.fixedObjectId.value;
    uint64_t randomSeed = configured.randomSeed;
    if (!readString("name", configured.name) || !readString("effectAssetGuid", configured.effectAssetGuid.value) ||
        !readString("triggerEventId", configured.triggerEventId) || !readBool("enabled", configured.enabled) ||
        !readBool("playOnStart", configured.playOnStart) || !readBool("followTarget", configured.followTarget) ||
        !readUnsigned("fixedObjectId", (std::numeric_limits<uint64_t>::max)(), fixedObjectId) ||
        !readUnsigned("randomSeed", (std::numeric_limits<uint32_t>::max)(), randomSeed)) {
        return false;
    }
    configured.fixedObjectId.value = fixedObjectId;
    configured.randomSeed = static_cast<uint32_t>(randomSeed);
    if (const auto found = values.find("spawnTarget"); found != values.end()) {
        if (!found->is_string() || !TryParseVfxSpawnTarget(found->get<std::string>(), configured.spawnTarget)) {
            outError = "unknown spawnTarget";
            return false;
        }
    }
    if (const auto found = values.find("loopMode"); found != values.end()) {
        if (!found->is_string() || !TryParseVfxLoopMode(found->get<std::string>(), configured.loopMode)) {
            outError = "unknown loopMode";
            return false;
        }
    }
    if (const auto found = values.find("retriggerPolicy"); found != values.end()) {
        if (!found->is_string() || !TryParseVfxRetriggerPolicy(found->get<std::string>(), configured.retriggerPolicy)) {
            outError = "unknown retriggerPolicy";
            return false;
        }
    }
    const auto offset = values.find("offset");
    if (offset != values.end()) {
        if (!offset->is_object()) {
            outError = "offset must be an object";
            return false;
        }
        static constexpr std::array<std::string_view, 3> knownOffsetFields{"position", "rotationEulerDegrees", "scale"};
        for (const auto& [name, value] : offset->items()) {
            (void)value;
            if (std::find(knownOffsetFields.begin(), knownOffsetFields.end(), name) == knownOffsetFields.end()) {
                outError = "unknown VFX offset field: " + name;
                return false;
            }
        }
        const auto readVec3 = [&outError](const nlohmann::json& parent, const char* key, MATH::Vec3& target) {
            const auto node = parent.find(key);
            if (node == parent.end())
                return true;
            if (!node->is_array() || node->size() != 3u || !(*node)[0].is_number() || !(*node)[1].is_number() ||
                !(*node)[2].is_number()) {
                outError = std::string(key) + " must be a numeric Vec3";
                return false;
            }
            MATH::Vec3 parsed{};
            try {
                parsed = {(*node)[0].get<float>(), (*node)[1].get<float>(), (*node)[2].get<float>()};
            } catch (const std::exception&) {
                outError = std::string(key) + " contains a value outside the float range";
                return false;
            }
            if (!VfxPlayerAuthoringInternal::IsFiniteVec3(parsed)) {
                outError = std::string(key) + " must contain finite values";
                return false;
            }
            target = parsed;
            return true;
        };
        if (!readVec3(*offset, "position", configured.offset.position) ||
            !readVec3(*offset, "rotationEulerDegrees", configured.offset.rotationEulerDegrees) ||
            !readVec3(*offset, "scale", configured.offset.scale)) {
            return false;
        }
    }
    slot = std::move(configured);
    return true;
}
} // namespace

std::vector<ComponentAuthoringCommandDefinition> BuildVfxPlayerComponentAuthoringCommands() {
    using VfxPlayerAuthoringInternal::Cast;
    using VfxPlayerAuthoringInternal::Error;
    using VfxPlayerAuthoringInternal::Ok;

    std::vector<ComponentAuthoringCommandDefinition> commands{};
    commands.push_back({"slot.add",
                        "Add a stable VFX slot.",
                        {
                            {"name", AuthoringCommandParameterType::String, false, "Slot display name."},
                            {"effectAssetGuid", AuthoringCommandParameterType::String, false, "VFX asset GUID."},
                        },
                        [](IComponent& base, const nlohmann::json& parameters) {
                            VfxPlayerComponent* component = Cast(base);
                            if (component == nullptr)
                                return Error("TYPE_MISMATCH", "Component is not a VFX player.");
                            VfxPlayerSlot& slot = component->AddSlot(parameters.value("name", std::string{}));
                            slot.effectAssetGuid.value = parameters.value("effectAssetGuid", std::string{});
                            return Ok("VFX slot added.", {{"slotId", slot.id.value}});
                        }});
    commands.push_back({"slot.configure",
                        "Configure one VFX slot through domain-owned fields.",
                        {
                            {"slotId", AuthoringCommandParameterType::Int, true, "Stable slot ID."},
                            {"values", AuthoringCommandParameterType::Json, true, "VFX slot field object."},
                        },
                        [](IComponent& base, const nlohmann::json& parameters) {
                            VfxPlayerComponent* component = Cast(base);
                            VfxSlotId id{};
                            if (component == nullptr || !ReadSlotId(parameters, id)) {
                                return Error("INVALID_SLOT", "A valid slotId is required.");
                            }
                            VfxPlayerSlot* slot = component->FindSlot(id);
                            if (slot == nullptr)
                                return Error("SLOT_NOT_FOUND", "VFX slot was not found.");
                            VfxPlayerSlot updated = *slot;
                            std::string error{};
                            if (!ConfigureSlot(updated, parameters.value("values", nlohmann::json{}), error)) {
                                return Error("INVALID_VALUES", std::move(error));
                            }
                            *slot = std::move(updated);
                            return Ok("VFX slot configured.");
                        }});
    const auto simpleSlotCommand = [](std::string id, std::string summary, auto execute) {
        return ComponentAuthoringCommandDefinition{
            std::move(id),
            std::move(summary),
            {{"slotId", AuthoringCommandParameterType::Int, true, "Stable slot ID."}},
            [execute](IComponent& base, const nlohmann::json& parameters) {
                VfxPlayerComponent* component = Cast(base);
                VfxSlotId id{};
                if (component == nullptr || !ReadSlotId(parameters, id)) {
                    return Error("INVALID_SLOT", "A valid slotId is required.");
                }
                return execute(*component, id) ? Ok("VFX slot command completed.")
                                               : Error("SLOT_NOT_FOUND", "VFX slot was not found.");
            }};
    };
    commands.push_back(
        simpleSlotCommand("slot.duplicate", "Duplicate a VFX slot.", [](VfxPlayerComponent& component, VfxSlotId id) {
            return component.DuplicateSlot(id) != nullptr;
        }));
    commands.push_back(
        simpleSlotCommand("slot.remove", "Remove a VFX slot.",
                          [](VfxPlayerComponent& component, VfxSlotId id) { return component.RemoveSlot(id); }));
    commands.push_back({"slot.move",
                        "Move a VFX slot within the component.",
                        {
                            {"slotId", AuthoringCommandParameterType::Int, true, "Stable slot ID."},
                            {"targetIndex", AuthoringCommandParameterType::Int, true, "Zero-based target index."},
                        },
                        [](IComponent& base, const nlohmann::json& parameters) {
                            VfxPlayerComponent* component = Cast(base);
                            VfxSlotId id{};
                            const int64_t target = parameters.value("targetIndex", int64_t{-1});
                            if (component == nullptr || !ReadSlotId(parameters, id) || target < 0) {
                                return Error("INVALID_ARGUMENT", "slotId and targetIndex are required.");
                            }
                            return component->MoveSlot(id, static_cast<size_t>(target))
                                       ? Ok("VFX slot moved.")
                                       : Error("SLOT_NOT_FOUND", "VFX slot was not found.");
                        }});
    commands.push_back(
        {"slot.bind_variable",
         "Bind one VFX exposed input to a generic runtime variable.",
         {
             {"slotId", AuthoringCommandParameterType::Int, true, "Stable slot ID."},
             {"inputId", AuthoringCommandParameterType::String, true, "VFX exposed input ID."},
             {"variableId", AuthoringCommandParameterType::String, true, "Runtime variable ID."},
         },
         [](IComponent& base, const nlohmann::json& parameters) {
             VfxPlayerComponent* component = Cast(base);
             VfxSlotId id{};
             const std::string inputId = parameters.value("inputId", std::string{});
             const std::string variableId = parameters.value("variableId", std::string{});
             if (component == nullptr || !ReadSlotId(parameters, id) || inputId.empty() || variableId.empty()) {
                 return Error("INVALID_ARGUMENT", "Variable binding fields are invalid.");
             }
             VfxPlayerSlot* slot = component->FindSlot(id);
             if (slot == nullptr)
                 return Error("SLOT_NOT_FOUND", "VFX slot was not found.");
             auto found =
                 std::find_if(slot->variableBindings.begin(), slot->variableBindings.end(),
                              [&inputId](const VfxVariableBinding& binding) { return binding.inputId == inputId; });
             if (found == slot->variableBindings.end()) {
                 slot->variableBindings.push_back({inputId, variableId});
             } else {
                 found->variableId = variableId;
             }
             return Ok("VFX runtime variable binding updated.");
         }});
    commands.push_back({"slot.unbind_variable",
                        "Remove the generic runtime variable binding for one VFX exposed input.",
                        {
                            {"slotId", AuthoringCommandParameterType::Int, true, "Stable slot ID."},
                            {"inputId", AuthoringCommandParameterType::String, true, "VFX exposed input ID."},
                        },
                        [](IComponent& base, const nlohmann::json& parameters) {
                            VfxPlayerComponent* component = Cast(base);
                            VfxSlotId id{};
                            const std::string inputId = parameters.value("inputId", std::string{});
                            if (component == nullptr || !ReadSlotId(parameters, id) || inputId.empty()) {
                                return Error("INVALID_ARGUMENT", "slotId and inputId are required.");
                            }
                            VfxPlayerSlot* slot = component->FindSlot(id);
                            if (slot == nullptr)
                                return Error("SLOT_NOT_FOUND", "VFX slot was not found.");
                            const auto found = std::find_if(
                                slot->variableBindings.begin(), slot->variableBindings.end(),
                                [&inputId](const VfxVariableBinding& binding) { return binding.inputId == inputId; });
                            if (found == slot->variableBindings.end()) {
                                return Error("BINDING_NOT_FOUND", "VFX input binding was not found.");
                            }
                            slot->variableBindings.erase(found);
                            return Ok("VFX runtime variable binding removed.");
                        }});
    return commands;
}

} // namespace HIKARI
