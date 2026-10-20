#include "Assets/HIKARI_AssetImportInputs.h"

#include <unordered_set>
#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Tasks/HIKARI_AssetImportPublication.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

#include <Windows.h>

namespace HIKARI {
namespace {
constexpr int kDigestCacheVersion = 1;
// 同じ時刻の刻み内に書き直されたファイルを取り違えないよう、変更直後のファイルは保存しない（100 ns 単位で 2 秒）。
constexpr int64_t kRecentChangeTicks = 20'000'000;

struct InputFileStamp {
    uint64_t size = 0;
    int64_t changeTime = 0;
};

// 内容は開かず属性だけを読む。変更時刻を持たないファイルシステムでは false を返し、毎回ハッシュする。
bool ReadInputFileStamp(const std::filesystem::path& path, InputFileStamp& outStamp) {
    const HANDLE file =
        CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    FILE_BASIC_INFO basic{};
    FILE_STANDARD_INFO standard{};
    const bool read = GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic)) &&
                      GetFileInformationByHandleEx(file, FileStandardInfo, &standard, sizeof(standard));
    CloseHandle(file);
    if (!read || standard.Directory || basic.ChangeTime.QuadPart <= 0)
        return false;
    outStamp = {static_cast<uint64_t>(standard.EndOfFile.QuadPart), basic.ChangeTime.QuadPart};
    return true;
}

bool ChangedRecently(int64_t changeTime) {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const int64_t nowTicks =
        static_cast<int64_t>((static_cast<uint64_t>(now.dwHighDateTime) << 32u) | now.dwLowDateTime);
    return nowTicks - changeTime < kRecentChangeTicks;
}

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

void AssetInputDigestCache::Open(const std::filesystem::path& cachePath) {
    path_ = cachePath;
    entries_.clear();
    scanning_ = false;
    dirty_ = false;
    // キャッシュなので、読めない・版が違う時は空から作り直す。
    nlohmann::json document;
    if (!SERIALIZATION::JSON::ReadJsonFile(cachePath, document) || !document.is_object() ||
        document.value("version", 0) != kDigestCacheVersion)
        return;
    const auto files = document.find("files");
    if (files == document.end() || !files->is_object())
        return;
    for (const auto& [key, value] : files->items()) {
        if (value.is_array() && value.size() == 3u && value[0].is_number_unsigned() && value[1].is_number_integer() &&
            value[2].is_string())
            entries_[key] = {value[0].get<uint64_t>(), value[1].get<int64_t>(), value[2].get<std::string>()};
    }
}

void AssetInputDigestCache::BeginScan() {
    for (auto& [key, entry] : entries_)
        entry.scanned = false;
    scanning_ = true;
}

void AssetInputDigestCache::FinishScan(bool succeeded) {
    scanning_ = false;
    if (!succeeded || path_.empty())
        return;
    if (std::erase_if(entries_, [](const auto& item) { return !item.second.scanned; }) != 0u)
        dirty_ = true;
    if (!dirty_)
        return;
    nlohmann::json files = nlohmann::json::object();
    for (const auto& [key, entry] : entries_)
        files[key] = nlohmann::json::array({entry.size, entry.changeTime, entry.digest});
    dirty_ = !SERIALIZATION::JSON::WriteJsonFile(
        path_, nlohmann::json{{"version", kDigestCacheVersion}, {"files", std::move(files)}});
}

std::string AssetInputDigestCache::Digest(const std::string& key, const std::filesystem::path& path) {
    const auto found = entries_.find(key);
    // 一回の走査では同じ入力を一度だけ確かめる。
    if (scanning_ && found != entries_.end() && found->second.scanned)
        return found->second.digest;
    InputFileStamp stamp{};
    if (!ReadInputFileStamp(path, stamp))
        return FileDigest(path, nullptr);
    if (found != entries_.end() && found->second.size == stamp.size && found->second.changeTime == stamp.changeTime) {
        if (scanning_)
            found->second.scanned = true;
        return found->second.digest;
    }
    std::string digest = FileDigest(path, nullptr);
    if (digest.empty() || ChangedRecently(stamp.changeTime)) {
        if (found != entries_.end()) {
            entries_.erase(found);
            dirty_ = true;
        }
        return digest;
    }
    entries_.insert_or_assign(key, Entry{stamp.size, stamp.changeTime, digest, scanning_});
    dirty_ = true;
    return digest;
}

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
                            AssetInputDigestCache* digests) {
    if (inputs.settings.empty() || inputs.files.empty()) {
        reason = "No completed import for the current inputs";
        return false;
    }
    for (const auto& file : inputs.files) {
        const auto path = PROJECT_PATHS::ResolveProjectPath(projectRoot, TEXT::PathFromUtf8(file.path));
        const std::string digest = digests ? digests->Digest(file.path, path) : FileDigest(path, publication);
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
