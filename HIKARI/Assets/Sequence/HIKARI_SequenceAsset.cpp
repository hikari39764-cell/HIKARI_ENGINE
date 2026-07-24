#include "Assets/Sequence/HIKARI_SequenceAsset.h"

#include <utility>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Scene/Sequencer/Serialization/HIKARI_CinematicSequenceJson.h"

namespace HIKARI {

    namespace {
        void SetError(std::string* outError, std::string message) {
            if (outError != nullptr) {
                *outError = std::move(message);
            }
        }
    }

    bool LoadSequenceAsset(
        const std::filesystem::path& path,
        const AssetGuid& authoritativeGuid,
        SequenceAsset& outAsset,
        std::string* outError) {

        nlohmann::json root{};
        std::string ioMessage{};
        if (!SERIALIZATION::JSON::ReadJsonFile(
                path,
                root,
                &ioMessage)) {
            SetError(
                outError,
                "Unable to read sequence asset: " + ioMessage);
            return false;
        }
        if (!root.is_object()) {
            SetError(outError, "Invalid sequence asset JSON");
            return false;
        }
        const uint32_t version = root.value("formatVersion", 0u);
        if (version == 0 || version > SequenceAsset::kFormatVersion) {
            SetError(outError, "Unsupported sequence asset version");
            return false;
        }
        const auto sequenceIt = root.find("sequence");
        if (sequenceIt == root.end() || !sequenceIt->is_object()) {
            SetError(outError, "Sequence asset has no sequence payload");
            return false;
        }

        SequenceAsset loaded{};
        loaded.guid = authoritativeGuid;
        if (!loaded.guid.IsValid()) {
            loaded.guid.value = root.value("guid", std::string{});
        }
        loaded.displayName = root.value("displayName", path.stem().string());
        DeserializeCinematicSequenceJson(*sequenceIt, loaded.sequence);
        if (loaded.displayName.empty()) {
            loaded.displayName = loaded.sequence.name;
        }
        outAsset = std::move(loaded);
        if (outError != nullptr) {
            outError->clear();
        }
        return true;
    }

    bool SaveSequenceAsset(
        const std::filesystem::path& path,
        const SequenceAsset& asset,
        std::string* outError) {

        CinematicSequence normalized = asset.sequence;
        NormalizeCinematicSequence(normalized);
        nlohmann::json sequenceJson{};
        SerializeCinematicSequenceJson(normalized, sequenceJson);
        nlohmann::json root{
            { "assetType", "Sequence" },
            { "formatVersion", SequenceAsset::kFormatVersion },
            { "guid", asset.guid.value },
            { "displayName", asset.displayName.empty()
                ? normalized.name
                : asset.displayName },
            { "sequence", std::move(sequenceJson) }
        };

        std::string ioMessage{};
        if (!SERIALIZATION::JSON::WriteJsonFile(
                path,
                root,
                &ioMessage)) {
            SetError(
                outError,
                "Unable to write sequence asset: " + ioMessage);
            return false;
        }
        if (outError != nullptr) {
            outError->clear();
        }
        return true;
    }

} // namespace HIKARI
