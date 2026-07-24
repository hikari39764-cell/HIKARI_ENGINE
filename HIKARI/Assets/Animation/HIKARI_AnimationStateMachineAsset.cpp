#include "Assets/Animation/HIKARI_AnimationStateMachineAsset.h"

#include <utility>

#include <json.hpp>

#include "Animation/StateMachine/HIKARI_AnimationStateMachineJson.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI {
    namespace {
        void SetError(std::string* outError, std::string message) {
            if (outError != nullptr) *outError = std::move(message);
        }
    }

    bool LoadAnimationStateMachineAsset(
        const std::filesystem::path& path,
        const AssetGuid& authoritativeGuid,
        AnimationStateMachineAsset& outAsset,
        std::string* outError) {
        nlohmann::json root{};
        std::string ioMessage{};
        if (!SERIALIZATION::JSON::ReadJsonFile(
                path,
                root,
                &ioMessage)) {
            SetError(
                outError,
                "Unable to read animation state machine asset: " +
                    ioMessage);
            return false;
        }
        if (!root.is_object()) {
            SetError(outError, "Invalid animation state machine JSON");
            return false;
        }
        const uint32_t version = root.value("formatVersion", 0u);
        if (version != AnimationStateMachineAsset::kFormatVersion) {
            SetError(outError, "Unsupported animation state machine version");
            return false;
        }
        const auto definitionNode = root.find("stateMachine");
        if (definitionNode == root.end()) {
            SetError(outError, "Animation state machine payload is missing");
            return false;
        }

        AnimationStateMachineAsset loaded{};
        loaded.guid = authoritativeGuid;
        if (!loaded.guid.IsValid()) {
            loaded.guid.value = root.value("guid", "");
        }
        loaded.displayName = root.value(
            "displayName", path.stem().string());
        if (!ANIMATION::DeserializeAnimationStateMachineJson(
                *definitionNode,
                loaded.definition)) {
            SetError(outError, "Invalid animation state machine payload");
            return false;
        }
        if (loaded.displayName.empty()) {
            loaded.displayName = loaded.definition.name;
        }
        outAsset = std::move(loaded);
        if (outError != nullptr) outError->clear();
        return true;
    }

    bool SaveAnimationStateMachineAsset(
        const std::filesystem::path& path,
        const AnimationStateMachineAsset& asset,
        std::string* outError) {
        ANIMATION::AnimationStateMachineDefinition normalized =
            asset.definition;
        ANIMATION::NormalizeAnimationStateMachine(normalized);
        nlohmann::json stateMachineJson{};
        ANIMATION::SerializeAnimationStateMachineJson(
            normalized,
            stateMachineJson);
        const nlohmann::json root{
            { "assetType", "AnimationStateMachine" },
            { "formatVersion", AnimationStateMachineAsset::kFormatVersion },
            { "guid", asset.guid.value },
            { "displayName", asset.displayName.empty()
                ? normalized.name
                : asset.displayName },
            { "stateMachine", std::move(stateMachineJson) }
        };
        std::string ioMessage{};
        if (!SERIALIZATION::JSON::WriteJsonFile(
                path,
                root,
                &ioMessage)) {
            SetError(
                outError,
                "Unable to write animation state machine asset: " +
                    ioMessage);
            return false;
        }
        if (outError != nullptr) outError->clear();
        return true;
    }

} // namespace HIKARI
