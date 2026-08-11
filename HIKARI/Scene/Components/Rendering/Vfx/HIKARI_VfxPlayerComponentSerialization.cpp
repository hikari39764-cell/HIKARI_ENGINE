#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponent.h"

#include <cmath>
#include <limits>
#include <utility>

namespace HIKARI {
    namespace {
        nlohmann::json WriteVec3(const MATH::Vec3& value) {
            return nlohmann::json::array({ value.x, value.y, value.z });
        }

        bool Fail(
            std::string& outDiagnostic,
            std::string message) {

            outDiagnostic = std::move(message);
            return false;
        }

        bool ReadBool(
            const nlohmann::json& object,
            const char* key,
            bool fallback,
            bool& outValue,
            std::string& outDiagnostic,
            const std::string& path) {

            const auto member = object.find(key);
            if (member == object.end()) {
                outValue = fallback;
                return true;
            }
            if (!member->is_boolean()) {
                return Fail(
                    outDiagnostic,
                    path + "." + key + " must be a boolean");
            }
            outValue = member->get<bool>();
            return true;
        }

        bool ReadString(
            const nlohmann::json& object,
            const char* key,
            std::string fallback,
            std::string& outValue,
            std::string& outDiagnostic,
            const std::string& path) {

            const auto member = object.find(key);
            if (member == object.end()) {
                outValue = std::move(fallback);
                return true;
            }
            if (!member->is_string()) {
                return Fail(
                    outDiagnostic,
                    path + "." + key + " must be a string");
            }
            outValue = member->get<std::string>();
            return true;
        }

        bool ReadUint64(
            const nlohmann::json& object,
            const char* key,
            uint64_t fallback,
            uint64_t& outValue,
            std::string& outDiagnostic,
            const std::string& path) {

            const auto member = object.find(key);
            if (member == object.end()) {
                outValue = fallback;
                return true;
            }
            if (member->is_number_unsigned()) {
                outValue = member->get<uint64_t>();
                return true;
            }
            if (member->is_number_integer()) {
                const int64_t value = member->get<int64_t>();
                if (value >= 0) {
                    outValue = static_cast<uint64_t>(value);
                    return true;
                }
            }
            return Fail(
                outDiagnostic,
                path + "." + key + " must be a non-negative integer");
        }

        bool ReadUint32(
            const nlohmann::json& object,
            const char* key,
            uint32_t fallback,
            uint32_t& outValue,
            std::string& outDiagnostic,
            const std::string& path) {

            uint64_t value = fallback;
            if (!ReadUint64(
                    object,
                    key,
                    fallback,
                    value,
                    outDiagnostic,
                    path)) {
                return false;
            }
            if (value > (std::numeric_limits<uint32_t>::max)()) {
                return Fail(
                    outDiagnostic,
                    path + "." + key + " exceeds uint32 range");
            }
            outValue = static_cast<uint32_t>(value);
            return true;
        }

        bool ReadVec3(
            const nlohmann::json& node,
            MATH::Vec3& outValue,
            std::string& outDiagnostic,
            const std::string& path) {

            if (!node.is_array() || node.size() != 3u ||
                !node[0].is_number() || !node[1].is_number() ||
                !node[2].is_number()) {
                return Fail(
                    outDiagnostic,
                    path + " must be an array of three finite numbers");
            }
            const double x = node[0].get<double>();
            const double y = node[1].get<double>();
            const double z = node[2].get<double>();
            constexpr double kMaximumFloat =
                static_cast<double>((std::numeric_limits<float>::max)());
            if (!std::isfinite(x) || !std::isfinite(y) ||
                !std::isfinite(z) || std::abs(x) > kMaximumFloat ||
                std::abs(y) > kMaximumFloat || std::abs(z) > kMaximumFloat) {
                return Fail(
                    outDiagnostic,
                    path + " must contain finite float values");
            }
            outValue = {
                static_cast<float>(x),
                static_cast<float>(y),
                static_cast<float>(z)
            };
            return true;
        }
    }

    void VfxPlayerComponent::Serialize(nlohmann::json& out) const {
        out = nlohmann::json::object();
        out["enabled"] = enabled_;
        out["slots"] = nlohmann::json::array();
        for (const VfxPlayerSlot& slot : slots_) {
            nlohmann::json bindings = nlohmann::json::array();
            for (const VfxVariableBinding& binding :
                slot.variableBindings) {
                bindings.push_back({
                    { "inputId", binding.inputId },
                    { "variableId", binding.variableId },
                });
            }
            out["slots"].push_back({
                { "id", slot.id.value },
                { "name", slot.name },
                { "enabled", slot.enabled },
                { "effectAssetGuid", slot.effectAssetGuid.value },
                { "playOnStart", slot.playOnStart },
                { "triggerEventId", slot.triggerEventId },
                { "spawnTarget", std::string(ToString(slot.spawnTarget)) },
                { "fixedObjectId", slot.fixedObjectId.value },
                { "followTarget", slot.followTarget },
                { "offset", {
                    { "position", WriteVec3(slot.offset.position) },
                    { "rotationEulerDegrees", WriteVec3(slot.offset.rotationEulerDegrees) },
                    { "scale", WriteVec3(slot.offset.scale) },
                } },
                { "loopMode", std::string(ToString(slot.loopMode)) },
                { "retriggerPolicy", std::string(ToString(slot.retriggerPolicy)) },
                { "randomSeed", slot.randomSeed },
                { "variableBindings", std::move(bindings) },
            });
        }
    }

    void VfxPlayerComponent::Deserialize(const nlohmann::json& in) {
        std::string ignoredDiagnostic{};
        (void)TryDeserialize(in, ignoredDiagnostic);
    }

    bool VfxPlayerComponent::TryDeserialize(
        const nlohmann::json& in,
        std::string& outDiagnostic) {

        try {
            if (!in.is_object()) {
                return Fail(
                    outDiagnostic,
                    "VfxPlayerComponent must be a JSON object");
            }

            VfxPlayerComponent decoded{};
            if (!ReadBool(
                    in,
                    "enabled",
                    true,
                    decoded.enabled_,
                    outDiagnostic,
                    "VfxPlayerComponent")) {
                return false;
            }

            const auto slots = in.find("slots");
            if (slots != in.end() && !slots->is_array()) {
                return Fail(
                    outDiagnostic,
                    "VfxPlayerComponent.slots must be an array");
            }
            if (slots != in.end()) {
                decoded.slots_.reserve(slots->size());
                for (size_t slotIndex = 0u;
                    slotIndex < slots->size();
                    ++slotIndex) {

                    const nlohmann::json& node = (*slots)[slotIndex];
                    const std::string slotPath =
                        "VfxPlayerComponent.slots[" +
                        std::to_string(slotIndex) + "]";
                    if (!node.is_object()) {
                        return Fail(
                            outDiagnostic,
                            slotPath + " must be an object");
                    }

                    VfxPlayerSlot slot{};
                    std::string enumText{};
                    if (!ReadUint64(
                            node,
                            "id",
                            0u,
                            slot.id.value,
                            outDiagnostic,
                            slotPath) ||
                        !ReadString(
                            node,
                            "name",
                            {},
                            slot.name,
                            outDiagnostic,
                            slotPath) ||
                        !ReadBool(
                            node,
                            "enabled",
                            true,
                            slot.enabled,
                            outDiagnostic,
                            slotPath) ||
                        !ReadString(
                            node,
                            "effectAssetGuid",
                            {},
                            slot.effectAssetGuid.value,
                            outDiagnostic,
                            slotPath) ||
                        !ReadBool(
                            node,
                            "playOnStart",
                            false,
                            slot.playOnStart,
                            outDiagnostic,
                            slotPath) ||
                        !ReadString(
                            node,
                            "triggerEventId",
                            {},
                            slot.triggerEventId,
                            outDiagnostic,
                            slotPath) ||
                        !ReadString(
                            node,
                            "spawnTarget",
                            "Owner",
                            enumText,
                            outDiagnostic,
                            slotPath)) {
                        return false;
                    }
                    if (!TryParseVfxSpawnTarget(
                            enumText,
                            slot.spawnTarget)) {
                        return Fail(
                            outDiagnostic,
                            slotPath + ".spawnTarget is invalid");
                    }
                    if (!ReadUint64(
                            node,
                            "fixedObjectId",
                            0u,
                            slot.fixedObjectId.value,
                            outDiagnostic,
                            slotPath) ||
                        !ReadBool(
                            node,
                            "followTarget",
                            true,
                            slot.followTarget,
                            outDiagnostic,
                            slotPath) ||
                        !ReadString(
                            node,
                            "loopMode",
                            "AssetDefault",
                            enumText,
                            outDiagnostic,
                            slotPath)) {
                        return false;
                    }
                    if (!TryParseVfxLoopMode(enumText, slot.loopMode)) {
                        return Fail(
                            outDiagnostic,
                            slotPath + ".loopMode is invalid");
                    }
                    if (!ReadString(
                            node,
                            "retriggerPolicy",
                            "Parallel",
                            enumText,
                            outDiagnostic,
                            slotPath)) {
                        return false;
                    }
                    if (!TryParseVfxRetriggerPolicy(
                            enumText,
                            slot.retriggerPolicy)) {
                        return Fail(
                            outDiagnostic,
                            slotPath + ".retriggerPolicy is invalid");
                    }
                    if (!ReadUint32(
                            node,
                            "randomSeed",
                            0u,
                            slot.randomSeed,
                            outDiagnostic,
                            slotPath)) {
                        return false;
                    }

                    const auto offset = node.find("offset");
                    if (offset != node.end()) {
                        if (!offset->is_object()) {
                            return Fail(
                                outDiagnostic,
                                slotPath + ".offset must be an object");
                        }
                        const auto readOptionalVector = [&outDiagnostic,
                            &slotPath,
                            &offset](
                                const char* key,
                                MATH::Vec3& outValue) {
                            const auto value = offset->find(key);
                            return value == offset->end() || ReadVec3(
                                *value,
                                outValue,
                                outDiagnostic,
                                slotPath + ".offset." + key);
                        };
                        if (!readOptionalVector(
                                "position",
                                slot.offset.position) ||
                            !readOptionalVector(
                                "rotationEulerDegrees",
                                slot.offset.rotationEulerDegrees) ||
                            !readOptionalVector(
                                "scale",
                                slot.offset.scale)) {
                            return false;
                        }
                    }

                    const auto bindings = node.find("variableBindings");
                    if (bindings != node.end()) {
                        if (!bindings->is_array()) {
                            return Fail(
                                outDiagnostic,
                                slotPath +
                                    ".variableBindings must be an array");
                        }
                        slot.variableBindings.reserve(bindings->size());
                        for (size_t bindingIndex = 0u;
                            bindingIndex < bindings->size();
                            ++bindingIndex) {

                            const nlohmann::json& bindingNode =
                                (*bindings)[bindingIndex];
                            const std::string bindingPath =
                                slotPath + ".variableBindings[" +
                                std::to_string(bindingIndex) + "]";
                            if (!bindingNode.is_object()) {
                                return Fail(
                                    outDiagnostic,
                                    bindingPath + " must be an object");
                            }
                            VfxVariableBinding binding{};
                            if (!ReadString(
                                    bindingNode,
                                    "inputId",
                                    {},
                                    binding.inputId,
                                    outDiagnostic,
                                    bindingPath) ||
                                !ReadString(
                                    bindingNode,
                                    "variableId",
                                    {},
                                    binding.variableId,
                                    outDiagnostic,
                                    bindingPath)) {
                                return false;
                            }
                            slot.variableBindings.push_back(
                                std::move(binding));
                        }
                    }
                    decoded.slots_.push_back(std::move(slot));
                }
            }

            decoded.NormalizeSlots();
            enabled_ = decoded.enabled_;
            slots_ = std::move(decoded.slots_);
            nextSlotId_ = decoded.nextSlotId_;
            pendingCommands_.clear();
            outDiagnostic.clear();
            return true;
        } catch (const nlohmann::json::exception& exception) {
            outDiagnostic = std::string(
                "VfxPlayerComponent JSON decode failed: ") +
                exception.what();
            return false;
        }
    }

} // namespace HIKARI
