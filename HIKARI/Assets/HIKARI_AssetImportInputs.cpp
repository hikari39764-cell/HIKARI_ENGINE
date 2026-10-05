#include "Assets/HIKARI_AssetImportInputs.h"

#include <unordered_set>
#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Tasks/HIKARI_AssetImportPublication.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

namespace HIKARI {
namespace {
bool ValidateSettingsShape(const nlohmann::json& edited, const nlohmann::json& defaults, const std::string& path,
                           std::string& error) {
    for (const auto& [key, fallback] : defaults.items()) {
        const auto value = edited.find(key);
        if (value == edited.end())
            continue;
        const std::string field = path.empty() ? key : path + "." + key;
        if (!(value->type() == fallback.type() || (value->is_number() && fallback.is_number()))) {
            error = "Invalid import setting type: " + field;
            return false;
        }
        if (fallback.is_object() && !ValidateSettingsShape(*value, fallback, field, error))
            return false;
    }
    return true;
}

std::string FileDigest(const std::filesystem::path& path, const AssetImportPublication* publication) {
    HASH::Sha256Digest digest;
    std::string error;
    const auto readable = publication ? publication->ReadPreparedFile(path) : path;
    return HASH::ComputeFileSha256(readable, digest, error) ? HASH::ToHex(digest) : std::string{};
}
} // namespace

AssetImportInputFile CaptureAssetInputFile(const std::filesystem::path& path) {
    return {TEXT::PathToUtf8(path.lexically_normal()), FileDigest(path, nullptr)};
}

std::string CanonicalImportSettings(std::string_view settings) {
    if (settings.empty())
        return "{}";
    const auto parsed = nlohmann::json::parse(settings, nullptr, false);
    return parsed.is_object() ? parsed.dump() : std::string{};
}

bool EquivalentAssetImportMeta(const AssetMeta& left, const AssetMeta& right) {
    return left.guid == right.guid && left.type == right.type && left.metaVersion == right.metaVersion &&
           left.sourcePath == right.sourcePath && left.importerId == right.importerId &&
           left.displayName == right.displayName && left.importerVersion == right.importerVersion &&
           CanonicalImportSettings(left.importSettingsJson) == CanonicalImportSettings(right.importSettingsJson);
}

bool ValidateAssetImportSettings(const AssetMeta& edited, const AssetMeta& defaults, std::string& error) {
    const auto settings = nlohmann::json::parse(CanonicalImportSettings(edited.importSettingsJson), nullptr, false);
    const auto shape = nlohmann::json::parse(CanonicalImportSettings(defaults.importSettingsJson), nullptr, false);
    if (!settings.is_object()) {
        error = "Import settings must be a valid JSON object";
        return false;
    }
    error.clear();
    return !shape.is_object() || ValidateSettingsShape(settings, shape, {}, error);
}

AssetImportInputs CaptureAssetImportInputs(const AssetDatabase& database, const AssetRecord& record,
                                           const std::vector<AssetDependencyDesc>& dependencies,
                                           const AssetImportPublication* publication) {
    AssetImportInputs inputs;
    inputs.settings = CanonicalImportSettings(record.meta.importSettingsJson);
    std::unordered_set<std::string> visited;
    const auto append = [&](const std::filesystem::path& path) {
        if (path.empty())
            return;
        const auto absolute = PROJECT_PATHS::ResolveProjectPath(database.GetProjectRoot(), path).lexically_normal();
        const auto relative = absolute.lexically_relative(database.GetProjectRoot());
        const std::string key = TEXT::PathToUtf8(relative.empty() ? absolute : relative);
        if (visited.insert(key).second)
            inputs.files.push_back({key, FileDigest(absolute, publication)});
    };
    append(record.sourcePath);
    // ソース依存だけでなく、素材の cook に使う texture とその設定も記録する。
    // GUID の参照だけで成立する runtime 資産は、ここで導入を強制しない。
    for (const auto& dependency : dependencies) {
        const AssetRecord* referenced = dependency.guid.IsValid() ? database.FindByGuid(dependency.guid) : nullptr;
        if (!referenced && !dependency.path.empty())
            referenced = database.FindByPath(TEXT::PathFromUtf8(dependency.path));
        if (referenced) {
            append(referenced->sourcePath);
            if (referenced->guid != record.guid) {
                append(referenced->metaPath);
                // テクスチャの cook 設定・alpha 判定・生成物もモデルの入力になる。
                if (referenced->type == AssetType::Texture && !referenced->artifactManifest.artifacts.empty())
                    append(referenced->artifactManifestPath);
            }
        } else if (!dependency.path.empty()) {
            append(TEXT::PathFromUtf8(dependency.path));
        }
    }
    return inputs;
}

bool CheckAssetImportInputs(const std::filesystem::path& projectRoot, const AssetImportInputs& inputs,
                            std::string& reason, const AssetImportPublication* publication,
                            std::unordered_map<std::string, std::string>* scanDigests) {
    if (inputs.settings.empty() || inputs.files.empty()) {
        reason = "No completed import for the current inputs";
        return false;
    }
    for (const auto& file : inputs.files) {
        const auto path = PROJECT_PATHS::ResolveProjectPath(projectRoot, TEXT::PathFromUtf8(file.path));
        std::string digest;
        if (scanDigests) {
            const auto key = TEXT::PathToUtf8(path.lexically_normal());
            auto [entry, inserted] = scanDigests->try_emplace(key);
            if (inserted)
                entry->second = FileDigest(path, publication);
            digest = entry->second;
        } else {
            digest = FileDigest(path, publication);
        }
        if (digest.empty()) {
            reason = "Input is missing or unreadable: " + file.path;
            return false;
        }
        if (digest != file.digest) {
            reason = "Input changed: " + file.path;
            return false;
        }
    }
    reason.clear();
    return true;
}
} // namespace HIKARI
