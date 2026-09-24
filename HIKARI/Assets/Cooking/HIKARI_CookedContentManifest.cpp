#include "Assets/Cooking/HIKARI_CookedContentManifest.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <unordered_set>
#include <utility>

#include <json.hpp>

#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI::ASSETS::COOKING {
namespace {

constexpr const char* kTextureMetadataBooleanFields[] = {
    "sourceHasAlphaChannel",
    "sourceHasMeaningfulAlpha",
    "sourceHasTranslucentAlpha",
    "sourceHasCutoutAlpha",
};

nlohmann::json SerializeTextureMetadata(const AssetRecord& record) {
    nlohmann::json result = nlohmann::json::object();
    if (record.type != AssetType::Texture) {
        return result;
    }
    const auto diagnostics = nlohmann::json::parse(record.artifactManifest.diagnosticsJson, nullptr, false);
    if (!diagnostics.is_object()) {
        return result;
    }
    const auto texture = diagnostics.find("texture");
    if (texture == diagnostics.end() || !texture->is_object()) {
        return result;
    }
    for (const char* key : kTextureMetadataBooleanFields) {
        const auto value = texture->find(key);
        if (value != texture->end() && value->is_boolean()) {
            result[key] = *value;
        }
    }
    return result;
}

bool ParseTextureMetadata(const nlohmann::json& node, AssetRecord& record, std::string& outMessage) {
    const auto metadata = node.find("textureMetadata");
    if (metadata == node.end()) {
        return true;
    }
    if (record.type != AssetType::Texture || !metadata->is_object()) {
        outMessage = "Cooked texture metadata requires a texture asset and an object: " + record.guid.value;
        return false;
    }
    nlohmann::json texture = nlohmann::json::object();
    for (const char* key : kTextureMetadataBooleanFields) {
        const auto value = metadata->find(key);
        if (value == metadata->end()) {
            continue;
        }
        if (!value->is_boolean()) {
            outMessage = "Cooked texture metadata field must be boolean: " + record.guid.value + " (" + key + ").";
            return false;
        }
        texture[key] = *value;
    }
    record.artifactManifest.diagnosticsJson = nlohmann::json{{"texture", std::move(texture)}}.dump();
    return true;
}

void SetMessage(std::string* outMessage, std::string message) {
    if (outMessage != nullptr) {
        *outMessage = std::move(message);
    }
}

nlohmann::json SerializeDependencies(std::vector<AssetDependencyDesc> dependencies) {
    std::sort(dependencies.begin(), dependencies.end(),
              [](const AssetDependencyDesc& lhs, const AssetDependencyDesc& rhs) {
                  if (lhs.guid.value != rhs.guid.value) {
                      return lhs.guid.value < rhs.guid.value;
                  }
                  if (lhs.path != rhs.path) {
                      return lhs.path < rhs.path;
                  }
                  return lhs.role < rhs.role;
              });

    nlohmann::json result = nlohmann::json::array();
    for (const AssetDependencyDesc& dependency : dependencies) {
        result.push_back({
            {"guid", dependency.guid.value},
            {"path", dependency.path},
            {"role", dependency.role},
        });
    }
    return result;
}

nlohmann::json SerializeArtifacts(std::vector<AssetArtifactDesc> artifacts) {
    std::sort(artifacts.begin(), artifacts.end(), [](const AssetArtifactDesc& lhs, const AssetArtifactDesc& rhs) {
        if (lhs.role != rhs.role) {
            return lhs.role < rhs.role;
        }
        if (lhs.path != rhs.path) {
            return lhs.path < rhs.path;
        }
        return lhs.format < rhs.format;
    });

    nlohmann::json result = nlohmann::json::array();
    for (const AssetArtifactDesc& artifact : artifacts) {
        result.push_back({
            {"role", artifact.role},
            {"path", artifact.path},
            {"format", artifact.format},
        });
    }
    return result;
}

bool ParseDependencies(const nlohmann::json& node, std::vector<AssetDependencyDesc>& outDependencies,
                       std::string& outMessage) {
    outDependencies.clear();
    if (!node.is_array()) {
        outMessage = "Cooked asset dependencies must be an array.";
        return false;
    }

    for (const nlohmann::json& dependencyNode : node) {
        if (!dependencyNode.is_object()) {
            outMessage = "Cooked asset dependency must be an object.";
            return false;
        }

        AssetDependencyDesc dependency{};
        dependency.guid.value = dependencyNode.value("guid", "");
        dependency.path = dependencyNode.value("path", "");
        dependency.role = dependencyNode.value("role", "");
        if (!dependency.path.empty() && !IsSafeCookedRelativePath(dependency.path)) {
            outMessage = "Cooked asset dependency path is unsafe: " + dependency.path;
            return false;
        }
        outDependencies.push_back(std::move(dependency));
    }
    return true;
}

bool ParseArtifacts(const nlohmann::json& node, std::vector<AssetArtifactDesc>& outArtifacts, std::string& outMessage) {
    outArtifacts.clear();
    if (!node.is_array()) {
        outMessage = "Cooked asset artifacts must be an array.";
        return false;
    }

    for (const nlohmann::json& artifactNode : node) {
        if (!artifactNode.is_object()) {
            outMessage = "Cooked asset artifact must be an object.";
            return false;
        }

        AssetArtifactDesc artifact{};
        artifact.role = artifactNode.value("role", "");
        artifact.path = artifactNode.value("path", "");
        artifact.format = artifactNode.value("format", "");
        if (artifact.path.empty() || !IsSafeCookedRelativePath(artifact.path)) {
            outMessage = "Cooked asset artifact path is unsafe: " + artifact.path;
            return false;
        }
        outArtifacts.push_back(std::move(artifact));
    }
    return true;
}

bool ParseAssetRecord(const nlohmann::json& node, AssetRecord& outRecord, std::string& outMessage) {
    if (!node.is_object()) {
        outMessage = "Cooked asset entry must be an object.";
        return false;
    }

    AssetRecord record{};
    record.guid.value = node.value("guid", "");
    record.type = ASSETS::SEMANTICS::ParseAssetType(node.value("type", ""));
    record.sourcePath = node.value("sourcePath", "");
    record.displayName = node.value("displayName", "");
    if (!record.guid.IsValid()) {
        outMessage = "Cooked asset has an invalid GUID: " + record.guid.value;
        return false;
    }
    if (record.type == AssetType::Unknown) {
        outMessage = "Cooked asset has an unknown type: " + record.guid.value;
        return false;
    }
    if (record.sourcePath.empty() || !IsSafeCookedRelativePath(record.sourcePath)) {
        outMessage = "Cooked asset source path is unsafe: " + record.sourcePath.generic_string();
        return false;
    }

    record.meta.guid = record.guid;
    record.meta.type = record.type;
    record.meta.importerId = node.value("importerId", "");
    const uint64_t importerVersion = node.value("importerVersion", uint64_t{1u});
    if (importerVersion == 0u || importerVersion > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())) {
        outMessage = "Cooked asset importer version is invalid: " + record.guid.value;
        return false;
    }
    record.meta.importerVersion = static_cast<uint32_t>(importerVersion);
    record.meta.sourcePath = record.sourcePath.generic_string();
    record.meta.displayName = record.displayName;
    record.meta.importSettingsJson = node.value("importSettingsJson", "");

    if (!ParseTextureMetadata(node, record, outMessage)) {
        return false;
    }

    if (!node.contains("dependencies") ||
        !ParseDependencies(node["dependencies"], record.artifactManifest.dependencies, outMessage)) {
        return false;
    }
    if (!node.contains("artifacts") ||
        !ParseArtifacts(node["artifacts"], record.artifactManifest.artifacts, outMessage)) {
        return false;
    }

    record.artifactManifest.guid = record.guid;
    record.artifactManifest.sourcePath = record.sourcePath.generic_string();
    record.artifactManifest.importerId = record.meta.importerId;
    record.artifactManifest.importerVersion = record.meta.importerVersion;
    record.artifactManifest.lastImportSucceeded = true;
    record.artifactManifest.lastImportMessage = "Loaded from CookedAssetRegistry.json";

    record.metaExists = true;
    record.importOutdated = false;
    record.lastImportSucceeded = true;
    record.duplicateGuid = false;
    record.importerMissing = false;
    record.artifactMissing = false;
    record.lastImportMessage = record.artifactManifest.lastImportMessage;
    outRecord = std::move(record);
    return true;
}

} // namespace

bool IsSafeCookedRelativePath(const std::filesystem::path& path) noexcept {
    if (path.empty() || path.is_absolute() || path.has_root_directory() || path.has_root_name()) {
        return false;
    }

    for (const std::filesystem::path& component : path) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

bool ReadCookedContentManifest(const std::filesystem::path& path, CookedContentManifest& outManifest,
                               std::string* outMessage) {
    nlohmann::json root{};
    std::string readMessage{};
    if (!SERIALIZATION::JSON::ReadJsonFile(path, root, &readMessage) || !root.is_object()) {
        SetMessage(outMessage,
                   readMessage.empty() ? "Cooked content manifest root must be an object." : std::move(readMessage));
        return false;
    }

    const uint64_t formatVersion = root.value("formatVersion", uint64_t{0u});
    if (formatVersion != kCookedContentManifestFormatVersion) {
        SetMessage(outMessage, "Unsupported cooked content manifest version: " + std::to_string(formatVersion));
        return false;
    }
    if (!root.contains("assets") || !root["assets"].is_array()) {
        SetMessage(outMessage, "Cooked content manifest requires an assets array.");
        return false;
    }

    CookedContentManifest manifest{};
    manifest.startupSceneGuid = root.value("startupSceneGuid", "");
    if (root.contains("exportedSceneGuids")) {
        if (!root["exportedSceneGuids"].is_array()) {
            SetMessage(outMessage, "exportedSceneGuids must be an array.");
            return false;
        }
        for (const nlohmann::json& guidNode : root["exportedSceneGuids"]) {
            if (!guidNode.is_string()) {
                SetMessage(outMessage, "exportedSceneGuids must contain strings.");
                return false;
            }
            manifest.exportedSceneGuids.push_back(guidNode.get<std::string>());
        }
    }

    std::unordered_set<std::string> seenGuids{};
    std::unordered_set<std::string> seenPaths{};
    for (const nlohmann::json& assetNode : root["assets"]) {
        AssetRecord record{};
        std::string parseMessage{};
        if (!ParseAssetRecord(assetNode, record, parseMessage)) {
            SetMessage(outMessage, std::move(parseMessage));
            return false;
        }

        if (!seenGuids.insert(record.guid.value).second) {
            SetMessage(outMessage, "Cooked content contains duplicate GUID: " + record.guid.value);
            return false;
        }
        const std::string sourceKey = record.sourcePath.lexically_normal().generic_string();
        if (!seenPaths.insert(sourceKey).second) {
            SetMessage(outMessage, "Cooked content contains duplicate source path: " + sourceKey);
            return false;
        }
        manifest.assets.push_back(std::move(record));
    }

    std::sort(manifest.exportedSceneGuids.begin(), manifest.exportedSceneGuids.end());
    manifest.exportedSceneGuids.erase(
        std::unique(manifest.exportedSceneGuids.begin(), manifest.exportedSceneGuids.end()),
        manifest.exportedSceneGuids.end());
    std::sort(manifest.assets.begin(), manifest.assets.end(),
              [](const AssetRecord& lhs, const AssetRecord& rhs) { return lhs.guid.value < rhs.guid.value; });

    outManifest = std::move(manifest);
    SetMessage(outMessage, {});
    return true;
}

bool WriteCookedContentManifest(const std::filesystem::path& path, const CookedContentManifest& manifest,
                                std::string* outMessage) {
    std::vector<std::string> exportedSceneGuids = manifest.exportedSceneGuids;
    std::sort(exportedSceneGuids.begin(), exportedSceneGuids.end());
    exportedSceneGuids.erase(std::unique(exportedSceneGuids.begin(), exportedSceneGuids.end()),
                             exportedSceneGuids.end());

    std::vector<const AssetRecord*> records{};
    records.reserve(manifest.assets.size());
    for (const AssetRecord& record : manifest.assets) {
        records.push_back(&record);
    }
    std::sort(records.begin(), records.end(),
              [](const AssetRecord* lhs, const AssetRecord* rhs) { return lhs->guid.value < rhs->guid.value; });

    nlohmann::json assets = nlohmann::json::array();
    for (const AssetRecord* record : records) {
        if (record == nullptr || !record->guid.IsValid() || record->type == AssetType::Unknown ||
            !IsSafeCookedRelativePath(record->sourcePath)) {
            SetMessage(outMessage, "Cooked content contains an invalid asset entry.");
            return false;
        }

        for (const AssetArtifactDesc& artifact : record->artifactManifest.artifacts) {
            if (!IsSafeCookedRelativePath(artifact.path)) {
                SetMessage(outMessage, "Cooked artifact path is unsafe: " + artifact.path);
                return false;
            }
        }

        assets.push_back({
            {"guid", record->guid.value},
            {"type", std::string(ASSETS::SEMANTICS::ToString(record->type))},
            {"sourcePath", record->sourcePath.generic_string()},
            {"displayName", record->displayName},
            {"importerId", record->meta.importerId},
            {"importerVersion", record->meta.importerVersion},
            {"importSettingsJson", record->meta.importSettingsJson},
            {"dependencies", SerializeDependencies(record->artifactManifest.dependencies)},
            {"artifacts", SerializeArtifacts(record->artifactManifest.artifacts)},
        });
        nlohmann::json textureMetadata = SerializeTextureMetadata(*record);
        if (!textureMetadata.empty()) {
            assets.back()["textureMetadata"] = std::move(textureMetadata);
        }
    }

    const nlohmann::json root{
        {"formatVersion", kCookedContentManifestFormatVersion},
        {"startupSceneGuid", manifest.startupSceneGuid},
        {"exportedSceneGuids", exportedSceneGuids},
        {"assets", std::move(assets)},
    };

    std::error_code ec{};
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        SetMessage(outMessage, "Could not create cooked content directory: " + path.parent_path().generic_string());
        return false;
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        SetMessage(outMessage, "Could not write cooked content manifest: " + path.generic_string());
        return false;
    }
    stream << root.dump(2) << '\n';
    if (!stream) {
        SetMessage(outMessage, "Cooked content manifest write failed: " + path.generic_string());
        return false;
    }

    SetMessage(outMessage, {});
    return true;
}

} // namespace HIKARI::ASSETS::COOKING
