#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportInputs.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

#include <algorithm>

namespace HIKARI {
namespace {
constexpr int kImportFailuresVersion = 1;

std::filesystem::path ImportFailuresPath(const std::filesystem::path& libraryRoot) {
    return libraryRoot / "AssetDatabase" / "ImportFailures.json";
}

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

bool AssetDatabase::UpdateImportSettings(const AssetGuid& guid, const AssetMeta& expected, const AssetMeta& edited,
                                         std::string& error) {
    if (!CheckOwnerThread("import settings update") || cookedContent_) {
        error = "Import settings require an editable project on the owner thread";
        return false;
    }
    auto* record = FindByGuid(guid);
    if (!record) {
        error = "Asset no longer exists";
        return false;
    }
    AssetMeta onDisk;
    if (!EquivalentAssetImportMeta(record->meta, expected) || !ReadMeta(record->metaPath, onDisk) ||
        !EquivalentAssetImportMeta(onDisk, expected)) {
        error = "Import settings changed externally. Reload before applying the draft";
        return false;
    }
    const std::string settings = CanonicalImportSettings(edited.importSettingsJson);
    if (settings.empty()) {
        error = "Import settings must be a valid JSON object";
        return false;
    }
    if (const auto* importer = importerRegistry_.FindById(record->meta.importerId)) {
        if (!ValidateAssetImportSettings(edited, importer->CreateDefaultMeta(record->sourcePath, guid), error))
            return false;
    }
    AssetRecord updated = *record;
    updated.meta.importSettingsJson = settings;
    updated.meta.displayName = edited.displayName;
    updated.displayName = edited.displayName;
    if (!WriteMeta(updated)) {
        error = "Failed to save import settings; the draft has been kept";
        return false;
    }
    *record = std::move(updated);
    RefreshRecordState(*record);
    RefreshImportDependents(guid);
    AdvanceContentRevision();
    error.clear();
    return true;
}

bool AssetDatabase::ReadMeta(const std::filesystem::path& metaPath, AssetMeta& outMeta) const {
    const std::filesystem::path absoluteMetaPath = PROJECT_PATHS::ResolveProjectPath(projectRoot_, metaPath);

    nlohmann::json root;
    if (!SERIALIZATION::JSON::ReadJsonFile(absoluteMetaPath, root) || !root.is_object()) {
        HIKARI_LOG_ERROR("[AssetDatabase] meta JSON parse failed: " + TEXT::PathToUtf8(absoluteMetaPath));
        return false;
    }

    AssetMeta parsed;
    std::string type, diagnostic;
    const std::pair<const char*, std::string*> strings[] = {{"guid", &parsed.guid.value},
                                                            {"type", &type},
                                                            {"importerId", &parsed.importerId},
                                                            {"sourcePath", &parsed.sourcePath},
                                                            {"displayName", &parsed.displayName}};
    for (const auto& [key, value] : strings) {
        if (!JSONSTRICT::ReadString(root, key, {}, *value, diagnostic, "meta")) {
            HIKARI_LOG_ERROR("[AssetDatabase] " + diagnostic);
            return false;
        }
    }
    if (!JSONSTRICT::ReadUint32(root, "metaVersion", parsed.metaVersion, parsed.metaVersion, diagnostic, "meta") ||
        !JSONSTRICT::ReadUint32(root, "importerVersion", parsed.importerVersion, parsed.importerVersion, diagnostic,
                                "meta")) {
        HIKARI_LOG_ERROR("[AssetDatabase] " + diagnostic);
        return false;
    }
    if (parsed.metaVersion != AssetMeta{}.metaVersion)
        return false;
    parsed.type = ASSETS::SEMANTICS::ParseAssetType(type);
    if (root.contains("importSettings") && !root["importSettings"].is_object())
        return false;
    parsed.importSettingsJson = root.value("importSettings", nlohmann::json::object()).dump(2);
    outMeta = std::move(parsed);
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
    if (root.contains("inputs") && root["inputs"].is_object()) {
        const auto& inputs = root["inputs"];
        outManifest.inputs.settings = inputs.value("settings", "");
        if (inputs.contains("files") && inputs["files"].is_array()) {
            for (const auto& file : inputs["files"])
                if (file.is_object())
                    outManifest.inputs.files.push_back({file.value("path", ""), file.value("digest", "")});
        }
    }
    return true;
}

bool AssetDatabase::WriteArtifactManifest(const AssetRecord& record, const AssetImportResult& result) const {
    if (record.artifactManifestPath.empty()) {
        return false;
    }

    auto root = SerializeImportResult(record, result, false);
    root["manifestVersion"] = kAssetArtifactManifestVersion;
    const auto& inputs = result.inputs.files.empty() ? record.artifactManifest.inputs : result.inputs;
    auto files = nlohmann::json::array();
    for (const auto& file : inputs.files)
        files.push_back({{"path", file.path}, {"digest", file.digest}});
    root["inputs"] = {{"settings", inputs.settings}, {"files", std::move(files)}};

    std::string writeMessage{};
    if (!SERIALIZATION::JSON::WriteJsonFile(record.artifactManifestPath, root, &writeMessage)) {
        HIKARI_LOG_ERROR("[AssetDatabase] failed to write artifact manifest: " + writeMessage);
        return false;
    }
    return true;
}

bool AssetDatabase::WriteImportReport(const AssetRecord& record, const AssetImportResult& result) {
    if (record.importedDirectory.empty()) {
        return false;
    }

    // 失敗は report より先に、成功は report の後に一覧へ保存し、途中で止まっても失敗を隠さない。
    // 他の database（書き出しや別プロセス）が記録した失敗を消さないよう、保存済みの一覧を読み直して重ねる。
    const auto recordOutcome = [&] {
        if (importFailures_.complete)
            LoadImportFailures();
        SetImportFailed(record.guid, !result.success);
        if (importFailures_.complete && importFailures_.dirty)
            SaveImportFailures();
    };
    if (!result.success)
        recordOutcome();

    auto root = SerializeImportResult(record, result, true);

    const std::filesystem::path reportPath = record.importedDirectory / "import_report.json";
    std::string writeMessage{};
    if (!SERIALIZATION::JSON::WriteJsonFile(reportPath, root, &writeMessage)) {
        HIKARI_LOG_ERROR("[AssetDatabase] failed to write import report: " + writeMessage);
        return false;
    }
    if (result.success)
        recordOutcome();
    return true;
}

void AssetDatabase::LoadImportFailures() {
    importFailures_ = {};
    nlohmann::json document;
    if (cookedContent_ || !SERIALIZATION::JSON::ReadJsonFile(ImportFailuresPath(libraryRoot_), document))
        return;
    // Library のキャッシュなので、壊れている・版が違う時は complete にせず、次の走査で全 report から作り直す。
    const auto version = document.find("version");
    const auto guids = document.find("guids");
    if (version == document.end() || *version != kImportFailuresVersion || guids == document.end() ||
        !guids->is_array())
        return;
    for (const auto& guid : *guids)
        if (guid.is_string())
            importFailures_.guids.insert(guid.get<std::string>());
    importFailures_.complete = true;
}

void AssetDatabase::SetImportFailed(const AssetGuid& guid, bool failed) {
    if (guid.IsValid() &&
        (failed ? importFailures_.guids.insert(guid.value).second : importFailures_.guids.erase(guid.value) != 0u))
        importFailures_.dirty = true;
}

void AssetDatabase::SaveImportFailures() {
    std::vector<std::string> guids(importFailures_.guids.begin(), importFailures_.guids.end());
    std::sort(guids.begin(), guids.end());
    std::string writeMessage{};
    importFailures_.dirty = !SERIALIZATION::JSON::WriteJsonFile(
        ImportFailuresPath(libraryRoot_), {{"version", kImportFailuresVersion}, {"guids", std::move(guids)}},
        &writeMessage);
    if (importFailures_.dirty)
        HIKARI_LOG_WARN("[AssetDatabase] failed to write import failure list: " + writeMessage);
}

} // namespace HIKARI
