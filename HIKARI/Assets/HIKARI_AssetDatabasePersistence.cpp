#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

namespace HIKARI {
namespace {
nlohmann::json SerializeDependencies(const std::vector<AssetDependencyDesc>& dependencies) {
    nlohmann::json out = nlohmann::json::array();
    for (const AssetDependencyDesc& dependency : dependencies) {
        out.push_back({
            {"guid", dependency.guid.value},
            {"path", dependency.path},
            {"role", dependency.role},
        });
    }
    return out;
}

nlohmann::json SerializeArtifacts(const std::vector<AssetArtifactDesc>& artifacts) {
    nlohmann::json out = nlohmann::json::array();
    for (const AssetArtifactDesc& artifact : artifacts) {
        out.push_back({
            {"role", artifact.role},
            {"path", artifact.path},
            {"format", artifact.format},
        });
    }
    return out;
}

void ParseDependencies(const nlohmann::json& root, std::vector<AssetDependencyDesc>& out) {
    out.clear();
    if (!root.contains("dependencies") || !root["dependencies"].is_array()) {
        return;
    }

    for (const nlohmann::json& node : root["dependencies"]) {
        if (!node.is_object()) {
            continue;
        }
        AssetDependencyDesc dependency{};
        dependency.guid.value = node.value("guid", "");
        dependency.path = node.value("path", "");
        dependency.role = node.value("role", "");
        out.push_back(std::move(dependency));
    }
}

void ParseArtifacts(const nlohmann::json& root, std::vector<AssetArtifactDesc>& out) {
    out.clear();
    if (!root.contains("artifacts") || !root["artifacts"].is_array()) {
        return;
    }

    for (const nlohmann::json& node : root["artifacts"]) {
        if (!node.is_object()) {
            continue;
        }
        AssetArtifactDesc artifact{};
        artifact.role = node.value("role", "");
        artifact.path = node.value("path", "");
        artifact.format = node.value("format", "");
        out.push_back(std::move(artifact));
    }
}

nlohmann::json SerializeImportResult(const AssetRecord& record, const AssetImportResult& result,
                                     bool preserveArtifactsOnFailure) {
    const bool usePrevious = preserveArtifactsOnFailure && !result.success;
    nlohmann::json root{
        {"guid", record.guid.value},
        {"sourcePath", TEXT::PathToUtf8(record.sourcePath)},
        {"importerId", record.meta.importerId},
        {"importerVersion", record.meta.importerVersion},
        {"success", result.success},
        {"message", result.message},
        {"artifacts", SerializeArtifacts(usePrevious ? record.artifactManifest.artifacts : result.artifacts)},
        {"dependencies",
         SerializeDependencies(usePrevious ? record.artifactManifest.dependencies : result.dependencies)},
    };
    if (!result.diagnosticsJson.empty()) {
        auto diagnostics = nlohmann::json::parse(result.diagnosticsJson, nullptr, false);
        if (diagnostics.is_discarded()) {
            root["diagnosticsText"] = result.diagnosticsJson;
        } else {
            root["diagnostics"] = std::move(diagnostics);
        }
    }
    return root;
}

} // namespace

bool AssetDatabase::WriteMeta(const AssetRecord& record) {
    if (!CheckOwnerThread("asset metadata write")) {
        return false;
    }
    if (cookedContent_) {
        HIKARI_LOG_ERROR("[AssetDatabase] cooked content is read-only; "
                         "WriteMeta was rejected.");
        return false;
    }

    if (record.metaPath.empty()) {
        return false;
    }

    nlohmann::json settings = nlohmann::json::parse(record.meta.importSettingsJson, nullptr, false);
    if (!settings.is_object()) {
        settings = nlohmann::json::object();
    }

    nlohmann::json root{
        {"metaVersion", record.meta.metaVersion},
        {"guid", record.meta.guid.value},
        {"type", std::string(ASSETS::SEMANTICS::ToString(record.meta.type))},
        {"importerId", record.meta.importerId},
        {"importerVersion", record.meta.importerVersion},
        {"sourcePath", record.meta.sourcePath},
        {"displayName", record.meta.displayName},
        {"importSettings", settings},
    };

    std::string writeMessage{};
    if (!SERIALIZATION::JSON::WriteJsonFile(record.metaPath, root, &writeMessage)) {
        HIKARI_LOG_ERROR("[AssetDatabase] failed to write meta: " + writeMessage);
        return false;
    }
    return true;
}

bool AssetDatabase::ReadMeta(const std::filesystem::path& metaPath, AssetMeta& outMeta) const {
    const std::filesystem::path absoluteMetaPath = PROJECT_PATHS::ResolveProjectPath(projectRoot_, metaPath);

    nlohmann::json root;
    if (!SERIALIZATION::JSON::ReadJsonFile(absoluteMetaPath, root) || !root.is_object()) {
        HIKARI_LOG_ERROR("[AssetDatabase] meta JSON parse failed: " + TEXT::PathToUtf8(absoluteMetaPath));
        return false;
    }

    outMeta = AssetMeta{};
    outMeta.metaVersion = root.value("metaVersion", 1u);
    outMeta.guid.value = root.value("guid", "");
    outMeta.type = ASSETS::SEMANTICS::ParseAssetType(root.value("type", "Unknown"));
    outMeta.importerId = root.value("importerId", "");
    outMeta.importerVersion = root.value("importerVersion", 1u);
    outMeta.sourcePath = root.value("sourcePath", "");
    outMeta.displayName = root.value("displayName", "");
    if (root.contains("importSettings") && root["importSettings"].is_object()) {
        outMeta.importSettingsJson = root["importSettings"].dump(2);
    } else {
        outMeta.importSettingsJson = "{}";
    }
    return true;
}

bool AssetDatabase::ReadArtifactManifest(const std::filesystem::path& manifestPath,
                                         AssetArtifactManifest& outManifest) const {
    const std::filesystem::path absoluteManifestPath = PROJECT_PATHS::ResolveProjectPath(projectRoot_, manifestPath);

    nlohmann::json root;
    if (!SERIALIZATION::JSON::ReadJsonFile(absoluteManifestPath, root) || !root.is_object()) {
        return false;
    }

    const uint32_t manifestVersion = root.value("manifestVersion", 0u);
    if (manifestVersion != kAssetArtifactManifestVersion) {
        HIKARI_LOG_WARN("[AssetDatabase] unsupported artifact manifest version: " +
                        TEXT::PathToUtf8(absoluteManifestPath));
        return false;
    }

    outManifest = AssetArtifactManifest{};
    outManifest.manifestVersion = manifestVersion;
    outManifest.guid.value = root.value("guid", "");
    outManifest.sourcePath = root.value("sourcePath", "");
    outManifest.importerId = root.value("importerId", "");
    outManifest.importerVersion = root.value("importerVersion", 1u);
    outManifest.lastImportSucceeded = root.value("success", false);
    outManifest.lastImportMessage = root.value("message", "");
    if (root.contains("diagnostics") && root["diagnostics"].is_object()) {
        outManifest.diagnosticsJson = root["diagnostics"].dump(2);
    } else {
        outManifest.diagnosticsJson = root.value("diagnosticsText", "");
    }
    ParseDependencies(root, outManifest.dependencies);
    ParseArtifacts(root, outManifest.artifacts);
    return true;
}

bool AssetDatabase::WriteArtifactManifest(const AssetRecord& record, const AssetImportResult& result) const {
    if (record.artifactManifestPath.empty()) {
        return false;
    }

    auto root = SerializeImportResult(record, result, false);
    root["manifestVersion"] = kAssetArtifactManifestVersion;

    std::string writeMessage{};
    if (!SERIALIZATION::JSON::WriteJsonFile(record.artifactManifestPath, root, &writeMessage)) {
        HIKARI_LOG_ERROR("[AssetDatabase] failed to write artifact manifest: " + writeMessage);
        return false;
    }
    return true;
}

bool AssetDatabase::WriteImportReport(const AssetRecord& record, const AssetImportResult& result) const {
    if (record.importedDirectory.empty()) {
        return false;
    }

    auto root = SerializeImportResult(record, result, true);

    const std::filesystem::path reportPath = record.importedDirectory / "import_report.json";
    std::string writeMessage{};
    if (!SERIALIZATION::JSON::WriteJsonFile(reportPath, root, &writeMessage)) {
        HIKARI_LOG_ERROR("[AssetDatabase] failed to write import report: " + writeMessage);
        return false;
    }
    return true;
}

} // namespace HIKARI
