#include "Assets/ContentLibrary/HIKARI_ContentLibraryInternal.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <tuple>

namespace HIKARI::ASSETS::LIBRARY {
namespace INTERNAL {
bool SafeKey(std::string_view value) {
    return !value.empty() && value.size() <= 80 && value != "." && value != ".." &&
           std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
           });
}

bool IsWithin(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code error;
    const auto base = std::filesystem::weakly_canonical(root, error);
    if (error || base.empty())
        return false;
    const auto target = std::filesystem::weakly_canonical(path, error);
    if (error)
        return false;
    const auto relative = target.lexically_relative(base);
    return !relative.empty() && relative != "." && !relative.is_absolute() && *relative.begin() != "..";
}

bool ResolveMember(const std::filesystem::path& root, std::string_view relative, std::filesystem::path& out,
                   std::string& message) {
    const auto member = TEXT::PathFromUtf8(relative);
    if (relative.empty() || relative.find(':') != std::string_view::npos || member.has_root_path() ||
        !IsWithin(root, root / member)) {
        message = "Content path escapes its package: " + std::string(relative);
        return false;
    }
    out = (root / member).lexically_normal();
    return true;
}

bool FileHash(const std::filesystem::path& path, std::string& out, std::string& message) {
    HASH::Sha256Digest digest;
    if (!HASH::ComputeFileSha256(path, digest, message))
        return false;
    out = HASH::ToHex(digest);
    return true;
}

StagingDirectory::StagingDirectory(const std::filesystem::path& parent)
    : path(std::filesystem::absolute(parent / (".hikari-content-" + GenerateAssetGuid().value))) {
    std::filesystem::create_directories(path);
}
StagingDirectory::~StagingDirectory() {
    std::error_code ignored;
    if (IsWithin(path.parent_path(), path) && TEXT::PathToUtf8(path.filename()).starts_with(".hikari-content-"))
        std::filesystem::remove_all(path, ignored);
}

bool ReadManifest(const std::filesystem::path& path, nlohmann::json& out, ContentPackageInfo& info,
                  std::string& message) {
    if (!SERIALIZATION::JSON::ReadJsonFile(path, out, &message))
        return false;
    try {
        if (out.at("schema") != "hikari.content_package" || out.at("version") != 1)
            throw std::runtime_error("Unsupported content package format");
        info = {};
        info.id = out.at("id").get<std::string>();
        info.version = out.at("revision").get<std::string>();
        info.name = out.at("name").get<std::string>();
        info.category = out.value("category", "");
        info.description = out.value("description", "");
        info.license = out.value("license", "");
        info.manifestPath = path;
        if (!SafeKey(info.id) || !SafeKey(info.version))
            throw std::runtime_error("Package ID/revision must be lowercase portable keys");
        const auto& assets = out.at("assets");
        const auto& roots = out.at("roots");
        if (!assets.is_array() || assets.empty() || assets.size() > 4096 || !roots.is_array() || roots.empty() ||
            roots.size() > assets.size())
            throw std::runtime_error("Invalid content package asset/root count");
        std::set<std::string> guids;
        std::set<std::string> paths;
        for (const auto& asset : assets) {
            const auto guid = asset.at("guid").get<std::string>();
            const auto member = asset.at("path").get<std::string>();
            std::filesystem::path absolute;
            HASH::Sha256Digest hash;
            if (!IsValidAssetGuid(guid) || !guids.insert(guid).second ||
                !ResolveMember(path.parent_path(), member, absolute, message) ||
                !HASH::ParseSha256(asset.at("sha256").get<std::string>(), hash))
                throw std::runtime_error("Invalid/duplicate content asset identity, path or hash");
            auto pathKey = TEXT::PathToUtf8(absolute);
            std::transform(pathKey.begin(), pathKey.end(), pathKey.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!paths.insert(pathKey).second)
                throw std::runtime_error("Content paths differ only by case");
            const auto type = SEMANTICS::ParseAssetType(asset.at("meta").at("type").get<std::string>());
            const auto* semantics = SEMANTICS::ResolveAssetSourceSemantics(absolute.filename());
            if (semantics == nullptr || semantics->assetType != type ||
                (type != AssetType::Texture && type != AssetType::Material && type != AssetType::VfxEffect) ||
                asset.at("meta").at("guid") != guid || asset.at("meta").at("metaVersion") != 1 ||
                asset.at("meta").at("importerId").get<std::string>() != semantics->importerId ||
                !asset.at("meta").at("importSettings").is_object())
                throw std::runtime_error("Content source type or metadata does not match");
        }
        std::set<std::string> rootIds;
        for (const auto& root : roots) {
            const auto id = root.get<std::string>();
            if (!guids.contains(id) || !rootIds.insert(id).second)
                throw std::runtime_error("Content root is missing or duplicated");
            info.roots.push_back({id});
        }
        info.assetCount = assets.size();
        return true;
    } catch (const std::exception& error) {
        message = error.what();
        return false;
    }
}
} // namespace INTERNAL

bool ReadContentPackage(const std::filesystem::path& path, ContentPackageInfo& info, std::string& message) {
    nlohmann::json manifest;
    return INTERNAL::ReadManifest(path, manifest, info, message);
}

std::vector<ContentPackageInfo> ScanContentLibrary(const std::filesystem::path& root, std::string& message) {
    std::vector<ContentPackageInfo> result;
    message.clear();
    try {
        if (!std::filesystem::is_directory(root)) {
            message = "Select an existing content library folder";
            return result;
        }
        // 固定二階層: package/revision/package.hikari-content.json。起動時の全再帰走査を避ける。
        for (const auto& package : std::filesystem::directory_iterator(root)) {
            if (!package.is_directory() || package.is_symlink() ||
                TEXT::PathToUtf8(package.path().filename()).starts_with('.'))
                continue;
            for (const auto& revision : std::filesystem::directory_iterator(package.path())) {
                if (!revision.is_directory() || revision.is_symlink())
                    continue;
                const auto path = revision.path() / "package.hikari-content.json";
                if (!std::filesystem::exists(path))
                    continue;
                ContentPackageInfo info;
                std::string error;
                if (ReadContentPackage(path, info, error))
                    result.push_back(std::move(info));
                else
                    message += TEXT::PathToUtf8(path) + ": " + error + "\n";
            }
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return std::tie(a.category, a.name, a.version) < std::tie(b.category, b.name, b.version);
        });
    } catch (const std::exception& error) {
        message = error.what();
    }
    return result;
}
} // namespace HIKARI::ASSETS::LIBRARY
