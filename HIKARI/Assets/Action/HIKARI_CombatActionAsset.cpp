#include "Assets/Action/HIKARI_CombatActionAsset.h"

#include <algorithm>
#include <utility>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI::ACTION {

    namespace {
        using nlohmann::json;

        void SetError(std::string* outError, std::string message) {
            if (outError != nullptr) *outError = std::move(message);
        }

        const char* ToPhaseText(CombatActionPhaseKind kind) noexcept {
            switch (kind) {
            case CombatActionPhaseKind::Active: return "active";
            case CombatActionPhaseKind::Recovery: return "recovery";
            case CombatActionPhaseKind::Startup:
            default: return "startup";
            }
        }

        CombatActionPhaseKind ParsePhase(std::string_view text) noexcept {
            if (text == "active") return CombatActionPhaseKind::Active;
            if (text == "recovery") return CombatActionPhaseKind::Recovery;
            return CombatActionPhaseKind::Startup;
        }

        const char* ToHitShapeText(CombatHitShapeKind kind) noexcept {
            switch (kind) {
            case CombatHitShapeKind::Sphere: return "sphere";
            case CombatHitShapeKind::Box: return "box";
            case CombatHitShapeKind::Capsule:
            default: return "capsule";
            }
        }

        CombatHitShapeKind ParseHitShape(std::string_view text) noexcept {
            if (text == "sphere") return CombatHitShapeKind::Sphere;
            if (text == "box") return CombatHitShapeKind::Box;
            return CombatHitShapeKind::Capsule;
        }

        const char* ToMotionText(CombatMotionMode mode) noexcept {
            switch (mode) {
            case CombatMotionMode::AuthoredVelocity: return "velocity";
            case CombatMotionMode::RootMotion: return "rootMotion";
            case CombatMotionMode::MotionWarp: return "motionWarp";
            case CombatMotionMode::None:
            default: return "none";
            }
        }

        CombatMotionMode ParseMotion(std::string_view text) noexcept {
            if (text == "velocity") return CombatMotionMode::AuthoredVelocity;
            if (text == "rootMotion") return CombatMotionMode::RootMotion;
            if (text == "motionWarp") return CombatMotionMode::MotionWarp;
            return CombatMotionMode::None;
        }

        json Vec3ToJson(const MATH::Vec3& value) {
            return json::array({ value.x, value.y, value.z });
        }

        MATH::Vec3 Vec3FromJson(
            const json& value,
            MATH::Vec3 fallback = {}) {
            if (!value.is_array() || value.size() < 3u ||
                !value[0].is_number() || !value[1].is_number() ||
                !value[2].is_number()) {
                return fallback;
            }
            return {
                value[0].get<float>(),
                value[1].get<float>(),
                value[2].get<float>()
            };
        }

        json SerializeDefinition(const CombatActionDefinition& definition) {
            json output{
                { "actionId", definition.actionId },
                { "displayName", definition.displayName },
                { "durationSeconds", definition.durationSeconds },
                { "displayFramesPerSecond", definition.displayFramesPerSecond },
                { "staminaCost", definition.staminaCost },
                { "cooldownSeconds", definition.cooldownSeconds },
                { "tags", definition.tags },
                { "preview", {
                    { "attackerModelGuid", definition.previewAttackerModelGuid.value },
                    { "victimModelGuid", definition.previewVictimModelGuid.value }
                } },
                { "animationSegments", json::array() },
                { "phases", json::array() },
                { "hitWindows", json::array() },
                { "cancelWindows", json::array() },
                { "motionWindows", json::array() },
                { "defenseWindows", json::array() },
                { "presentation", {
                    { "sequenceAssetGuid", definition.presentation.sequenceAssetGuid.value },
                    { "cameraReactionProfile", definition.presentation.cameraReactionProfile },
                    { "vfxCues", json::array() }
                } }
            };
            for (const auto& value : definition.animationSegments) {
                output["animationSegments"].push_back({
                    { "id", value.id }, { "clipName", value.clipName },
                    { "startTimeSeconds", value.startTimeSeconds },
                    { "durationSeconds", value.durationSeconds },
                    { "sourceStartSeconds", value.sourceStartSeconds },
                    { "playbackRate", value.playbackRate },
                    { "blendInSeconds", value.blendInSeconds },
                    { "blendOutSeconds", value.blendOutSeconds },
                    { "loop", value.loop }
                });
            }
            for (const auto& value : definition.phases) {
                output["phases"].push_back({
                    { "id", value.id }, { "kind", ToPhaseText(value.kind) },
                    { "startTimeSeconds", value.startTimeSeconds },
                    { "endTimeSeconds", value.endTimeSeconds }
                });
            }
            for (const auto& value : definition.hitWindows) {
                output["hitWindows"].push_back({
                    { "id", value.id }, { "label", value.label },
                    { "startTimeSeconds", value.startTimeSeconds },
                    { "endTimeSeconds", value.endTimeSeconds },
                    { "socketName", value.socketName },
                    { "shape", ToHitShapeText(value.shape) },
                    { "localOffset", Vec3ToJson(value.localOffset) },
                    { "halfExtents", Vec3ToJson(value.halfExtents) },
                    { "radius", value.radius },
                    { "halfHeight", value.halfHeight },
                    { "damageScale", value.damageScale },
                    { "postureDamageScale", value.postureDamageScale }
                });
            }
            for (const auto& value : definition.cancelWindows) {
                output["cancelWindows"].push_back({
                    { "id", value.id }, { "label", value.label },
                    { "startTimeSeconds", value.startTimeSeconds },
                    { "endTimeSeconds", value.endTimeSeconds },
                    { "allowedTags", value.allowedTags }
                });
            }
            for (const auto& value : definition.motionWindows) {
                output["motionWindows"].push_back({
                    { "id", value.id }, { "mode", ToMotionText(value.mode) },
                    { "startTimeSeconds", value.startTimeSeconds },
                    { "endTimeSeconds", value.endTimeSeconds },
                    { "warpTargetSlot", value.warpTargetSlot },
                    { "velocity", Vec3ToJson(value.velocity) },
                    { "translationScale", value.translationScale },
                    { "rotationWeight", value.rotationWeight }
                });
            }
            for (const auto& value : definition.defenseWindows) {
                output["defenseWindows"].push_back({
                    { "id", value.id },
                    { "startTimeSeconds", value.startTimeSeconds },
                    { "endTimeSeconds", value.endTimeSeconds },
                    { "damageReduction", value.damageReduction },
                    { "invulnerable", value.invulnerable },
                    { "superArmor", value.superArmor }
                });
            }
            for (const auto& value : definition.presentation.vfxCues) {
                output["presentation"]["vfxCues"].push_back({
                    { "id", value.id },
                    { "assetGuid", value.assetGuid.value },
                    { "socketName", value.socketName },
                    { "timeSeconds", value.timeSeconds },
                    { "scale", value.scale }
                });
            }
            return output;
        }

        CombatActionDefinition DeserializeDefinition(const json& input) {
            CombatActionDefinition definition{};
            definition.actionId = input.value("actionId", definition.actionId);
            definition.displayName = input.value(
                "displayName", definition.displayName);
            definition.durationSeconds = input.value(
                "durationSeconds", definition.durationSeconds);
            definition.displayFramesPerSecond = input.value(
                "displayFramesPerSecond", definition.displayFramesPerSecond);
            definition.staminaCost = input.value(
                "staminaCost", definition.staminaCost);
            definition.cooldownSeconds = input.value(
                "cooldownSeconds", definition.cooldownSeconds);
            if (const auto it = input.find("tags");
                it != input.end() && it->is_array()) {
                definition.tags = it->get<std::vector<std::string>>();
            }
            if (const auto it = input.find("preview");
                it != input.end() && it->is_object()) {
                definition.previewAttackerModelGuid.value = it->value(
                    "attackerModelGuid", std::string{});
                definition.previewVictimModelGuid.value = it->value(
                    "victimModelGuid", std::string{});
            }
            if (const auto list = input.find("animationSegments");
                list != input.end() && list->is_array()) {
                for (const json& node : *list) {
                    if (!node.is_object()) continue;
                    CombatActionAnimationSegment value{};
                    value.id = node.value("id", 0u);
                    value.clipName = node.value("clipName", std::string{});
                    value.startTimeSeconds = node.value("startTimeSeconds", 0.0f);
                    value.durationSeconds = node.value("durationSeconds", 1.0f);
                    value.sourceStartSeconds = node.value("sourceStartSeconds", 0.0f);
                    value.playbackRate = node.value("playbackRate", 1.0f);
                    value.blendInSeconds = node.value("blendInSeconds", 0.08f);
                    value.blendOutSeconds = node.value("blendOutSeconds", 0.08f);
                    value.loop = node.value("loop", false);
                    definition.animationSegments.push_back(std::move(value));
                }
            }
            if (const auto list = input.find("phases");
                list != input.end() && list->is_array()) {
                for (const json& node : *list) {
                    if (!node.is_object()) continue;
                    CombatActionPhaseWindow value{};
                    value.id = node.value("id", 0u);
                    value.kind = ParsePhase(node.value("kind", std::string{}));
                    value.startTimeSeconds = node.value("startTimeSeconds", 0.0f);
                    value.endTimeSeconds = node.value("endTimeSeconds", 0.0f);
                    definition.phases.push_back(std::move(value));
                }
            }
            if (const auto list = input.find("hitWindows");
                list != input.end() && list->is_array()) {
                for (const json& node : *list) {
                    if (!node.is_object()) continue;
                    CombatActionHitWindow value{};
                    value.id = node.value("id", 0u);
                    value.label = node.value("label", value.label);
                    value.startTimeSeconds = node.value("startTimeSeconds", 0.0f);
                    value.endTimeSeconds = node.value("endTimeSeconds", 0.0f);
                    value.socketName = node.value("socketName", std::string{});
                    value.shape = ParseHitShape(node.value("shape", std::string{}));
                    if (node.contains("localOffset")) value.localOffset = Vec3FromJson(node["localOffset"]);
                    if (node.contains("halfExtents")) value.halfExtents = Vec3FromJson(node["halfExtents"], value.halfExtents);
                    value.radius = node.value("radius", value.radius);
                    value.halfHeight = node.value("halfHeight", value.halfHeight);
                    value.damageScale = node.value("damageScale", value.damageScale);
                    value.postureDamageScale = node.value("postureDamageScale", value.postureDamageScale);
                    definition.hitWindows.push_back(std::move(value));
                }
            }
            if (const auto list = input.find("cancelWindows");
                list != input.end() && list->is_array()) {
                for (const json& node : *list) {
                    if (!node.is_object()) continue;
                    CombatActionCancelWindow value{};
                    value.id = node.value("id", 0u);
                    value.label = node.value("label", value.label);
                    value.startTimeSeconds = node.value("startTimeSeconds", 0.0f);
                    value.endTimeSeconds = node.value("endTimeSeconds", 0.0f);
                    if (const auto tags = node.find("allowedTags");
                        tags != node.end() && tags->is_array()) {
                        value.allowedTags = tags->get<std::vector<std::string>>();
                    }
                    definition.cancelWindows.push_back(std::move(value));
                }
            }
            if (const auto list = input.find("motionWindows");
                list != input.end() && list->is_array()) {
                for (const json& node : *list) {
                    if (!node.is_object()) continue;
                    CombatActionMotionWindow value{};
                    value.id = node.value("id", 0u);
                    value.mode = ParseMotion(node.value("mode", std::string{}));
                    value.startTimeSeconds = node.value("startTimeSeconds", 0.0f);
                    value.endTimeSeconds = node.value("endTimeSeconds", 0.0f);
                    value.warpTargetSlot = node.value("warpTargetSlot", value.warpTargetSlot);
                    if (node.contains("velocity")) value.velocity = Vec3FromJson(node["velocity"]);
                    value.translationScale = node.value("translationScale", value.translationScale);
                    value.rotationWeight = node.value("rotationWeight", value.rotationWeight);
                    definition.motionWindows.push_back(std::move(value));
                }
            }
            if (const auto list = input.find("defenseWindows");
                list != input.end() && list->is_array()) {
                for (const json& node : *list) {
                    if (!node.is_object()) continue;
                    CombatActionDefenseWindow value{};
                    value.id = node.value("id", 0u);
                    value.startTimeSeconds = node.value("startTimeSeconds", 0.0f);
                    value.endTimeSeconds = node.value("endTimeSeconds", 0.0f);
                    value.damageReduction = node.value("damageReduction", 0.0f);
                    value.invulnerable = node.value("invulnerable", false);
                    value.superArmor = node.value("superArmor", false);
                    definition.defenseWindows.push_back(std::move(value));
                }
            }
            if (const auto node = input.find("presentation");
                node != input.end() && node->is_object()) {
                definition.presentation.sequenceAssetGuid.value = node->value(
                    "sequenceAssetGuid", std::string{});
                definition.presentation.cameraReactionProfile = node->value(
                    "cameraReactionProfile", std::string{});
                if (const auto list = node->find("vfxCues");
                    list != node->end() && list->is_array()) {
                    for (const json& cueNode : *list) {
                        if (!cueNode.is_object()) continue;
                        CombatActionVfxCue value{};
                        value.id = cueNode.value("id", 0u);
                        value.assetGuid.value = cueNode.value("assetGuid", std::string{});
                        value.socketName = cueNode.value("socketName", std::string{});
                        value.timeSeconds = cueNode.value("timeSeconds", 0.0f);
                        value.scale = cueNode.value("scale", 1.0f);
                        definition.presentation.vfxCues.push_back(std::move(value));
                    }
                }
            }
            NormalizeCombatAction(definition);
            return definition;
        }
    }

    bool LoadCombatActionAsset(
        const std::filesystem::path& path,
        const AssetGuid& authoritativeGuid,
        CombatActionAsset& outAsset,
        std::string* outError) {
        json root{};
        std::string ioMessage{};
        if (!SERIALIZATION::JSON::ReadJsonFile(path, root, &ioMessage)) {
            SetError(outError, "Unable to read combat action asset: " + ioMessage);
            return false;
        }
        if (!root.is_object() || root.value("assetType", std::string{}) != "CombatAction") {
            SetError(outError, "Invalid combat action asset JSON");
            return false;
        }
        const uint32_t version = root.value("formatVersion", 0u);
        if (version == 0u || version > CombatActionAsset::kFormatVersion) {
            SetError(outError, "Unsupported combat action asset version");
            return false;
        }
        const auto definitionIt = root.find("definition");
        if (definitionIt == root.end() || !definitionIt->is_object()) {
            SetError(outError, "Combat action asset has no definition payload");
            return false;
        }
        CombatActionAsset loaded{};
        loaded.guid = authoritativeGuid;
        if (!loaded.guid.IsValid()) loaded.guid.value = root.value("guid", std::string{});
        loaded.definition = DeserializeDefinition(*definitionIt);
        loaded.displayName = root.value("displayName", loaded.definition.displayName);
        const auto diagnostics = ValidateCombatAction(loaded.definition);
        if (HasCombatActionErrors(diagnostics)) {
            const auto found = std::find_if(
                diagnostics.begin(), diagnostics.end(),
                [](const CombatActionDiagnostic& value) {
                    return value.severity == CombatActionDiagnosticSeverity::Error;
                });
            SetError(outError, found != diagnostics.end()
                ? found->path + ": " + found->message
                : "Combat action validation failed");
            return false;
        }
        outAsset = std::move(loaded);
        if (outError != nullptr) outError->clear();
        return true;
    }

    bool SaveCombatActionAsset(
        const std::filesystem::path& path,
        const CombatActionAsset& asset,
        std::string* outError) {
        CombatActionDefinition normalized = asset.definition;
        NormalizeCombatAction(normalized);
        const auto diagnostics = ValidateCombatAction(normalized);
        if (HasCombatActionErrors(diagnostics)) {
            SetError(outError, "Combat action validation failed before save");
            return false;
        }
        const json root{
            { "assetType", "CombatAction" },
            { "formatVersion", CombatActionAsset::kFormatVersion },
            { "guid", asset.guid.value },
            { "displayName", asset.displayName.empty() ? normalized.displayName : asset.displayName },
            { "definition", SerializeDefinition(normalized) }
        };
        std::string ioMessage{};
        if (!SERIALIZATION::JSON::WriteJsonFile(path, root, &ioMessage)) {
            SetError(outError, "Unable to write combat action asset: " + ioMessage);
            return false;
        }
        if (outError != nullptr) outError->clear();
        return true;
    }

} // namespace HIKARI::ACTION
