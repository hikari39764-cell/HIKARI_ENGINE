#include "HIKARI_PrefabRegistry.h"

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_Utf8.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "HIKARI_PrefabDocument.h"
#include "HIKARI_PrefabSerializer.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI {

namespace SceneJson = SCENE::SERIALIZATION;

namespace {

bool IsValidPrefabFileName(std::string_view name) noexcept {
    constexpr std::string_view invalidCharacters{"<>:\"/\\|?*"};
    return !name.empty() && name.size() <= 128u && name != "." && name != ".." && name.back() != ' ' &&
           name.back() != '.' && TEXT::IsValidUtf8(name) &&
           std::none_of(name.begin(), name.end(), [](unsigned char character) { return character < 32u; }) &&
           name.find_first_of(invalidCharacters) == std::string_view::npos && !TEXT::IsWindowsReservedFileName(name);
}

bool IsReparsePoint(const std::filesystem::path& path) noexcept {
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u;
#else
    std::error_code error{};
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path, error)) && !error;
#endif
}

} // namespace

bool PrefabRegistry::Save(AssetDatabase& assetDatabase, const std::string& name, const PrefabDocument& document,
                          const PrefabSerializer& serializer, AssetGuid& outGuid, std::string& outError) const {
    outGuid = {};
    outError.clear();
    if (assetDatabase.IsCookedContent()) {
        outError = "Cooked Prefab assets are read-only.";
        return false;
    }
    if (!IsValidPrefabFileName(name)) {
        outError = "Prefab name must be a safe Windows file name of 1 to 128 UTF-8 bytes.";
        return false;
    }

    std::error_code ec{};
    const std::filesystem::path directory = assetDatabase.GetAssetsRoot() / "Prefabs";
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        outError = "Could not create Prefabs asset directory: " + TEXT::PathToUtf8(directory);
        return false;
    }
    const std::filesystem::path canonicalAssetsRoot =
        std::filesystem::weakly_canonical(assetDatabase.GetAssetsRoot(), ec);
    if (ec) {
        outError = "Could not resolve the Assets directory: " + TEXT::PathToUtf8(assetDatabase.GetAssetsRoot());
        return false;
    }
    const std::filesystem::path canonicalPrefabDirectory = std::filesystem::weakly_canonical(directory, ec);
    if (ec || canonicalPrefabDirectory != (canonicalAssetsRoot / "Prefabs").lexically_normal()) {
        outError = "Prefab assets must remain inside Assets/Prefabs.";
        return false;
    }
    const std::filesystem::path path = directory / TEXT::PathFromUtf8(name + ".hprefab");
    if (const AssetRecord* existing = assetDatabase.FindByPath(path);
        existing != nullptr && existing->type != AssetType::Prefab) {
        outError = "A different asset already uses the requested Prefab path.";
        return false;
    }
    if (IsReparsePoint(path)) {
        outError = "Prefab destination cannot be a symbolic link or reparse point: " + TEXT::PathToUtf8(path);
        return false;
    }
    const std::filesystem::file_status targetStatus = std::filesystem::symlink_status(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        outError = "Could not inspect Prefab destination: " + TEXT::PathToUtf8(path);
        return false;
    }
    if (!ec && std::filesystem::exists(targetStatus) && !std::filesystem::is_regular_file(targetStatus)) {
        outError = "Prefab destination must be a regular file: " + TEXT::PathToUtf8(path);
        return false;
    }
    PrefabDocument savedDocument = document;
    savedDocument.prefabName = name;
    if (!serializer.SaveToFile(path, savedDocument, outError)) {
        return false;
    }
    if (!assetDatabase.ScanAssets(true)) {
        outError = "Prefab source was saved, but the asset database could not scan it.";
        return false;
    }
    const AssetRecord* record = assetDatabase.FindByPath(path);
    if (record == nullptr || record->type != AssetType::Prefab || !record->guid.IsValid()) {
        outError = "Prefab source was saved, but its asset record could not be created.";
        return false;
    }
    outGuid = record->guid;
    if (!assetDatabase.ImportAsset(outGuid)) {
        const AssetRecord* failed = assetDatabase.FindByGuid(outGuid);
        outError = failed != nullptr ? failed->lastImportMessage : "Prefab source import failed.";
        return false;
    }
    return true;
}

bool PrefabRegistry::Load(const AssetDatabase& assetDatabase, const AssetGuid& guid, PrefabDocument& outDocument,
                          const PrefabSerializer& serializer, std::string& outError) const {
    const AssetRecord* record = assetDatabase.FindByGuid(guid);
    if (record == nullptr || record->type != AssetType::Prefab || !record->guid.IsValid() || !record->sourceExists ||
        record->duplicateGuid) {
        outError = "Prefab authoring source is missing or invalid.";
        return false;
    }
    if (assetDatabase.IsCookedContent()) {
        outError = "Prefab authoring documents are unavailable from cooked content.";
        return false;
    }
    const std::filesystem::path path = assetDatabase.ResolveSourcePath(*record);
    return serializer.LoadFromFile(path, outDocument, outError);
}

std::vector<PrefabAssetEntry> PrefabRegistry::List(const AssetDatabase& assetDatabase) const {
    std::vector<PrefabAssetEntry> entries{};
    for (const AssetRecord* record : assetDatabase.CollectByType(AssetType::Prefab)) {
        if (record != nullptr && record->sourceExists && !record->duplicateGuid && record->guid.IsValid()) {
            entries.push_back({record->guid, record->displayName});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const PrefabAssetEntry& lhs, const PrefabAssetEntry& rhs) {
        return lhs.name == rhs.name ? lhs.guid.value < rhs.guid.value : lhs.name < rhs.name;
    });
    return entries;
}

bool PrefabSerializer::LoadFromFile(const std::filesystem::path& path, PrefabDocument& outDocument,
                                    std::string& outError) const {
    outError.clear();
    nlohmann::json root{};
    if (!SERIALIZATION::JSON::ReadJsonFile(path, root) || !root.is_object()) {
        outError = "Could not read Prefab JSON object: " + TEXT::PathToUtf8(path);
        return false;
    }
    try {
        PrefabDocument document{};
        if (root.value("version", 0u) != document.version) {
            outError = "Prefab version is unsupported.";
            return false;
        }
        document.prefabName = root.value("prefabName", std::string("Prefab"));
        if (!root.contains("rootObject") ||
            !SceneJson::DeserializeSceneObjectJson(root.at("rootObject"), document.rootObject,
                                                   SceneJson::SceneObjectJsonScope::SceneDocument)) {
            outError = "Prefab root object is invalid.";
            return false;
        }
        if (root.contains("descendants")) {
            const auto& descendants = root.at("descendants");
            if (!descendants.is_array()) {
                outError = "Prefab descendants must be an array.";
                return false;
            }
            for (const auto& node : descendants) {
                SceneObjectData object;
                if (!SceneJson::DeserializeSceneObjectJson(node, object,
                                                           SceneJson::SceneObjectJsonScope::SceneDocument)) {
                    outError = "Prefab descendant is invalid.";
                    return false;
                }
                document.descendants.push_back(std::move(object));
            }
        }
        if (!ValidatePrefabDocument(document, outError))
            return false;
        outDocument = std::move(document);
        return true;
    } catch (const nlohmann::json::exception& error) {
        outError = error.what();
        return false;
    }
}

bool PrefabSerializer::SaveToFile(const std::filesystem::path& path, const PrefabDocument& document,
                                  std::string& outError) const {
    if (!ValidatePrefabDocument(document, outError))
        return false;
    nlohmann::json root{{"version", document.version}, {"prefabName", document.prefabName}};
    SceneObjectData persistedRoot = document.rootObject;
    NormalizeSceneComponentIds(persistedRoot);
    SceneJson::SerializeSceneObjectJson(persistedRoot, root["rootObject"], SceneJson::SceneObjectJsonScope::PrefabRoot);
    root["descendants"] = nlohmann::json::array();
    for (auto child : document.descendants) {
        NormalizeSceneComponentIds(child);
        nlohmann::json node;
        SceneJson::SerializeSceneObjectJson(child, node, SceneJson::SceneObjectJsonScope::SceneDocument);
        root["descendants"].push_back(std::move(node));
    }
    if (!SERIALIZATION::JSON::WriteJsonFile(path, root)) {
        outError = "Could not write Prefab JSON: " + TEXT::PathToUtf8(path);
        return false;
    }
    outError.clear();
    return true;
}

} // namespace HIKARI
