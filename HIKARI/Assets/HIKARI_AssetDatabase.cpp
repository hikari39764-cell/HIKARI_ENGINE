#include "HIKARI_AssetDatabase.h"
#include "Core/Numeric/HIKARI_NonZero.h"
#include "Importers/HIKARI_StructuredAssetImporters.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_UtfPath.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <utility>

#include <json.hpp>

#include "Assets/Cooking/HIKARI_CookedContentManifest.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include "Importers/HIKARI_MaterialImporter.h"
#include "Importers/HIKARI_ModelMaterialBindingImporter.h"
#include "Importers/HIKARI_ModelImporter.h"
#include "Importers/HIKARI_PrefabAssetImporter.h"
#include "Importers/HIKARI_SpriteAssetImporter.h"
#include "Importers/HIKARI_SpriteMaterialImporter.h"
#include "Importers/HIKARI_TextAssetImporter.h"
#include "Importers/HIKARI_ShaderImporter.h"
#include "Importers/HIKARI_SkyCubemapImporter.h"
#include "Importers/HIKARI_TextureImportBackend_DirectXTex.h"
#include "Importers/HIKARI_TextureImporter.h"
#include "Importers/HIKARI_VfxAssetImporter.h"

namespace HIKARI {

namespace {
bool IsIgnoredDirectoryName(const std::string& name) {
    const std::string lower = TEXT::ToLowerAsciiCopy(name);
    return lower == ".git" || lower == "library" || lower == "projectsettings" || lower == "tmp" || lower == "cache";
}

bool IsMetaPath(const std::filesystem::path& path) {
    return TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(path.filename())).ends_with(ASSETS::SEMANTICS::kSourceMetaSuffix);
}

} // namespace

bool AssetDatabase::Initialize(const std::filesystem::path& projectRoot) {
    if (assetTaskService_.HasActiveTasks()) {
        HIKARI_LOG_ERROR("[AssetDatabase] initialization rejected while imports are active.");
        return false;
    }
    const std::thread::id currentThread = std::this_thread::get_id();
    {
        std::lock_guard ownerLock(ownerThreadMutex_);
        if (ownerThread_ != std::thread::id{} && ownerThread_ != currentThread) {
            HIKARI_LOG_ERROR("[AssetDatabase] initialization rejected outside the "
                             "owner thread.");
            return false;
        }
        ownerThread_ = currentThread;
    }
    std::error_code ec{};
    const ProjectContext& projectContext = GetProjectContext();
    const std::filesystem::path root =
        projectRoot.empty() ? (projectContext.IsOpen() ? projectContext.GetRoot() : std::filesystem::current_path(ec))
                            : projectRoot;

    projectRoot_ = std::filesystem::absolute(root, ec).lexically_normal();
    if (ec) {
        projectRoot_ = root.lexically_normal();
    }

    projectSettingsRoot_ = projectRoot_ / "ProjectSettings";

    nlohmann::json pipelineSettings{};
    const std::filesystem::path settingsPath = projectSettingsRoot_ / "asset_pipeline.json";
    (void)SERIALIZATION::JSON::ReadJsonFile(settingsPath, pipelineSettings);

    const auto resolvePipelinePath = [this, &pipelineSettings](const char* key, const char* fallback) {
        const std::string value =
            pipelineSettings.is_object() ? pipelineSettings.value(key, std::string(fallback)) : std::string(fallback);
        return PROJECT_PATHS::ResolveProjectPath(projectRoot_, TEXT::PathFromUtf8(value));
    };

    assetsRoot_ = resolvePipelinePath("assetsRoot", "Assets");
    libraryRoot_ = resolvePipelinePath("libraryRoot", "Library");
    sourceMetaRoot_ = resolvePipelinePath("sourceMetaRoot", "ProjectSettings/AssetMeta");

    cookedManifestPath_ = projectRoot_ / ASSETS::COOKING::kCookedContentManifestFilename;
    ec.clear();
    cookedContent_ = std::filesystem::is_regular_file(cookedManifestPath_, ec) && !ec;
#if defined(HIKARI_SHIPPING)
    if (!cookedContent_) {
        HIKARI_LOG_ERROR("[AssetDatabase] Shipping requires " + TEXT::PathToUtf8(cookedManifestPath_));
        projectRoot_.clear();
        assetsRoot_.clear();
        libraryRoot_.clear();
        projectSettingsRoot_.clear();
        sourceMetaRoot_.clear();
        cookedManifestPath_.clear();
        cookedContent_ = false;
        return false;
    }
#endif
    if (cookedContent_) {
        HIKARI_LOG_INFO("[AssetDatabase] cooked content mode: " + TEXT::PathToUtf8(cookedManifestPath_));
        return true;
    }

    RegisterDefaultImporters();
    EnsureProjectDirectories();
    return true;
}

const std::filesystem::path& AssetDatabase::GetProjectRoot() const { return projectRoot_; }

std::filesystem::path AssetDatabase::ResolveSourcePath(const AssetRecord& record) const {
    return (record.sourcePath.is_absolute() ? record.sourcePath : projectRoot_ / record.sourcePath).lexically_normal();
}

const std::filesystem::path& AssetDatabase::GetAssetsRoot() const { return assetsRoot_; }

const std::filesystem::path& AssetDatabase::GetLibraryRoot() const { return libraryRoot_; }

const std::filesystem::path& AssetDatabase::GetSourceMetaRoot() const { return sourceMetaRoot_; }

uint64_t AssetDatabase::GetContentRevision() const noexcept { return contentRevision_; }

AssetDatabase::ContentSnapshot AssetDatabase::TakeContentSnapshot() noexcept {
    ContentSnapshot snapshot{};
    snapshot.records = std::move(records_);
    snapshot.directories = std::move(directories_);
    snapshot.recordsByGuid = std::move(recordsByGuid_);
    snapshot.guidByNormalizedPath = std::move(guidByNormalizedPath_);
    snapshot.contentRevision = contentRevision_;
    return snapshot;
}

void AssetDatabase::RestoreContentSnapshot(ContentSnapshot&& snapshot) noexcept {
    records_ = std::move(snapshot.records);
    directories_ = std::move(snapshot.directories);
    recordsByGuid_ = std::move(snapshot.recordsByGuid);
    guidByNormalizedPath_ = std::move(snapshot.guidByNormalizedPath);
    contentRevision_ = snapshot.contentRevision;
}

bool AssetDatabase::CheckOwnerThread(const char* operation) const {
    {
        std::lock_guard ownerLock(ownerThreadMutex_);
        if (ownerThread_ == std::thread::id{} || ownerThread_ != std::this_thread::get_id()) {
            HIKARI_LOG_ERROR(std::string("[AssetDatabase] ") + operation + " rejected outside the owner thread.");
            return false;
        }
    }
    return true;
}

bool AssetDatabase::CanReplaceContentSnapshot() const {
    if (!CheckOwnerThread("content replacement")) {
        return false;
    }
    if (assetTaskService_.HasActiveTasks()) {
        HIKARI_LOG_WARN("[AssetDatabase] scan deferred while asset tasks are "
                        "active.");
        return false;
    }
    return true;
}

bool AssetDatabase::TryTakeContentSnapshot(ContentSnapshot& outSnapshot) {
    if (!CanReplaceContentSnapshot()) {
        return false;
    }
    outSnapshot = TakeContentSnapshot();
    return true;
}

bool AssetDatabase::IsCookedContent() const noexcept { return cookedContent_; }

AssetImporterRegistry& AssetDatabase::GetImporterRegistry() { return importerRegistry_; }

const AssetImporterRegistry& AssetDatabase::GetImporterRegistry() const { return importerRegistry_; }

bool AssetDatabase::ScanAssets(bool createMissingMeta) {
    if (!CanReplaceContentSnapshot()) {
        return false;
    }

    ContentSnapshot previous = TakeContentSnapshot();
    const auto restore = [&]() noexcept { RestoreContentSnapshot(std::move(previous)); };
    try {
        if (!ScanAssetsInPlace(createMissingMeta)) {
            restore();
            return false;
        }
        return true;
    } catch (const std::exception& error) {
        HIKARI_LOG_ERROR(std::string("[AssetDatabase] scan threw: ") + error.what());
    } catch (...) {
        HIKARI_LOG_ERROR("[AssetDatabase] scan threw an unknown exception.");
    }
    restore();
    return false;
}

bool AssetDatabase::ScanAssetsInPlace(bool createMissingMeta) {
    if (cookedContent_) {
        (void)createMissingMeta;
        return LoadCookedRecords();
    }

    EnsureProjectDirectories();

    records_.clear();
    directories_.clear();
    recordsByGuid_.clear();
    guidByNormalizedPath_.clear();
    AddDirectoryToCache("Assets");

    std::error_code ec{};
    if (!std::filesystem::exists(assetsRoot_, ec)) {
        HIKARI_LOG_WARN("[AssetDatabase] Assets root missing: " + TEXT::PathToUtf8(assetsRoot_));
        return false;
    }

    std::unordered_map<std::string, std::string> scanDigests;
    auto appendRecordForSource = [&](const std::filesystem::path& sourcePath, bool allowCreateMissingMeta) {
        std::filesystem::path relativeSource = NormalizeProjectPath(sourcePath);
        if (ASSETS::SEMANTICS::IsAssetCompanionSource(relativeSource)) {
            return true;
        }
        const AssetType guessedType = GuessAssetTypeFromPath(relativeSource);
        std::error_code metaEc{};
        const bool hasExistingMeta = std::filesystem::exists(GetMetaPathForSource(relativeSource), metaEc);
        if (guessedType == AssetType::Unknown && !hasExistingMeta) {
            return true;
        }

        AssetRecord record = BuildRecordForSource(relativeSource, allowCreateMissingMeta, scanDigests);
        const std::string pathKey = MakePathKey(record.sourcePath);
        if (guidByNormalizedPath_.find(pathKey) != guidByNormalizedPath_.end()) {
            return true;
        }

        const size_t index = records_.size();
        AddDirectoryToCache(record.sourcePath.parent_path());

        if (record.guid.IsValid()) {
            const auto existing = recordsByGuid_.find(record.guid.value);
            if (existing != recordsByGuid_.end()) {
                AssetRecord& firstRecord = records_[existing->second];
                firstRecord.duplicateGuid = true;
                firstRecord.lastImportMessage = "[AssetDatabase] duplicate GUID: " + record.guid.value;
                record.duplicateGuid = true;
                record.lastImportMessage = "[AssetDatabase] duplicate GUID: " + record.guid.value;
                HIKARI_LOG_ERROR(record.lastImportMessage + " source=" + TEXT::PathToUtf8(record.sourcePath));
            } else {
                recordsByGuid_[record.guid.value] = index;
            }
        }

        guidByNormalizedPath_[pathKey] = index;
        records_.push_back(std::move(record));
        return true;
    };

    std::filesystem::recursive_directory_iterator it(assetsRoot_,
                                                     std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end{};
    for (; !ec && it != end; it.increment(ec)) {
        const std::filesystem::directory_entry& entry = *it;
        if (entry.is_directory(ec)) {
            if (IsIgnoredDirectoryName(TEXT::PathToUtf8(entry.path().filename()))) {
                it.disable_recursion_pending();
                continue;
            }

            std::error_code relativeEc{};
            const std::filesystem::path relativeDirectory =
                std::filesystem::relative(entry.path(), projectRoot_, relativeEc);
            if (!relativeEc) {
                AddDirectoryToCache(relativeDirectory.lexically_normal());
            }
            continue;
        }

        if (!entry.is_regular_file(ec)) {
            continue;
        }

        const std::filesystem::path& absoluteSource = entry.path();
        if (IsMetaPath(absoluteSource)) {
            continue;
        }

        appendRecordForSource(absoluteSource, createMissingMeta);
    }

    if (ec) {
        HIKARI_LOG_ERROR("[AssetDatabase] scan failed: " + ec.message());
        return false;
    }

    SortAndUniqueDirectories();
    AdvanceContentRevision();
    HIKARI_LOG_INFO("[AssetDatabase] scanned assets. count=" + std::to_string(records_.size()));
    return true;
}

bool AssetDatabase::LoadCookedRecords() {
    ASSETS::COOKING::CookedContentManifest manifest{};
    std::string message{};
    if (!ASSETS::COOKING::ReadCookedContentManifest(cookedManifestPath_, manifest, &message)) {
        HIKARI_LOG_ERROR("[AssetDatabase] cooked manifest load failed: " + message);
        return false;
    }

    records_.clear();
    directories_.clear();
    recordsByGuid_.clear();
    guidByNormalizedPath_.clear();
    AddDirectoryToCache("RuntimePayload");

    for (AssetRecord& record : manifest.assets) {
        std::error_code ec{};
        const bool payloadExists = std::filesystem::is_regular_file(projectRoot_ / record.sourcePath, ec) && !ec;

        bool artifactsReady = true;
        for (const AssetArtifactDesc& artifact : record.artifactManifest.artifacts) {
            ec.clear();
            if (!std::filesystem::is_regular_file(projectRoot_ / TEXT::PathFromUtf8(artifact.path), ec) || ec) {
                HIKARI_LOG_ERROR("[AssetDatabase] cooked artifact missing: " + artifact.path);
                artifactsReady = false;
                break;
            }
        }
        if (!artifactsReady || (!payloadExists && record.artifactManifest.artifacts.empty())) {
            if (!payloadExists && record.artifactManifest.artifacts.empty()) {
                HIKARI_LOG_ERROR("[AssetDatabase] cooked payload missing: " + TEXT::PathToUtf8(record.sourcePath));
            }
            records_.clear();
            directories_.clear();
            recordsByGuid_.clear();
            guidByNormalizedPath_.clear();
            return false;
        }

        record.metaPath.clear();
        record.artifactManifestPath.clear();
        record.importedDirectory = GetImportedDirectory(record.guid);
        record.sourceExists = payloadExists || !record.artifactManifest.artifacts.empty();
        record.metaExists = true;
        record.importOutdated = false;
        record.lastImportSucceeded = true;
        record.duplicateGuid = false;
        record.importerMissing = false;
        record.artifactMissing = false;

        const size_t index = records_.size();
        recordsByGuid_[record.guid.value] = index;
        guidByNormalizedPath_[MakePathKey(record.sourcePath)] = index;
        AddDirectoryToCache(record.sourcePath.parent_path());
        records_.push_back(std::move(record));
    }

    SortAndUniqueDirectories();
    AdvanceContentRevision();
    HIKARI_LOG_INFO("[AssetDatabase] loaded cooked asset registry. count=" + std::to_string(records_.size()));
    return true;
}

void AssetDatabase::AdvanceContentRevision() noexcept { NUMERIC::AdvanceNonZero(contentRevision_); }

const AssetRecord* AssetDatabase::FindByGuid(const AssetGuid& guid) const {
    const auto it = recordsByGuid_.find(guid.value);
    if (it == recordsByGuid_.end() || it->second >= records_.size()) {
        return nullptr;
    }
    return &records_[it->second];
}

AssetRecord* AssetDatabase::FindByGuid(const AssetGuid& guid) {
    const auto it = recordsByGuid_.find(guid.value);
    if (it == recordsByGuid_.end() || it->second >= records_.size()) {
        return nullptr;
    }
    return &records_[it->second];
}

const AssetRecord* AssetDatabase::FindByPath(const std::filesystem::path& path) const {
    const auto it = guidByNormalizedPath_.find(MakePathKey(path));
    if (it == guidByNormalizedPath_.end() || it->second >= records_.size()) {
        return nullptr;
    }
    return &records_[it->second];
}

AssetRecord* AssetDatabase::FindByPath(const std::filesystem::path& path) {
    const auto it = guidByNormalizedPath_.find(MakePathKey(path));
    if (it == guidByNormalizedPath_.end() || it->second >= records_.size()) {
        return nullptr;
    }
    return &records_[it->second];
}

const AssetRecord* AssetDatabase::FindByArtifactPath(const std::filesystem::path& path) const {
    const std::string requestedKey = MakePathKey(path);
    if (requestedKey.empty()) {
        return nullptr;
    }

    const AssetRecord* match = nullptr;
    for (const AssetRecord& record : records_) {
        for (const AssetArtifactDesc& artifact : record.artifactManifest.artifacts) {
            if (MakePathKey(TEXT::PathFromUtf8(artifact.path)) != requestedKey) {
                continue;
            }
            // Ambiguous artifact ownership is an invalid database state.
            // Do not silently select one source asset by scan order.
            if (match != nullptr && match != &record) {
                return nullptr;
            }
            match = &record;
        }
    }
    return match;
}

AssetRecord* AssetDatabase::FindByArtifactPath(const std::filesystem::path& path) {
    return const_cast<AssetRecord*>(std::as_const(*this).FindByArtifactPath(path));
}

std::vector<const AssetRecord*> AssetDatabase::CollectByType(AssetType type) const {
    std::vector<const AssetRecord*> out;
    for (const AssetRecord& record : records_) {
        if (record.type == type) {
            out.push_back(&record);
        }
    }
    return out;
}

std::vector<const AssetRecord*> AssetDatabase::CollectAll() const {
    std::vector<const AssetRecord*> out;
    out.reserve(records_.size());
    for (const AssetRecord& record : records_) {
        out.push_back(&record);
    }
    return out;
}

std::vector<std::filesystem::path> AssetDatabase::CollectDirectories() const { return directories_; }

std::vector<const AssetRecord*> AssetDatabase::CollectInDirectory(const std::filesystem::path& directory,
                                                                  bool recursive) const {
    std::vector<const AssetRecord*> out;
    for (const AssetRecord& record : records_) {
        const bool include = recursive ? IsPathUnderDirectory(record.sourcePath, directory)
                                       : MakePathKey(record.sourcePath.parent_path()) == MakePathKey(directory);
        if (include) {
            out.push_back(&record);
        }
    }
    return out;
}

bool AssetDatabase::RegenerateMeta(const std::filesystem::path& sourcePath) {
    if (!CheckOwnerThread("asset metadata regeneration")) {
        return false;
    }
    if (cookedContent_) {
        HIKARI_LOG_ERROR("[AssetDatabase] cooked content is read-only; "
                         "RegenerateMeta was rejected.");
        return false;
    }

    AssetRecord* record = FindByPath(sourcePath);
    if (!record) {
        HIKARI_LOG_ERROR("[AssetDatabase] RegenerateMeta failed: source not found " + TEXT::PathToUtf8(sourcePath));
        return false;
    }

    const AssetGuid guid = record->guid.IsValid() ? record->guid : GenerateAssetGuid();
    const AssetType type = GuessAssetTypeFromPath(record->sourcePath);
    const std::string importerId = SelectDefaultImporterId(type, record->sourcePath);

    AssetMeta meta{};
    if (const IAssetImporter* importer = importerRegistry_.FindById(importerId)) {
        meta = importer->CreateDefaultMeta(record->sourcePath, guid);
    } else {
        meta.guid = guid;
        meta.type = type;
        meta.importerId = importerId;
        meta.importerVersion = 1;
        meta.sourcePath = TEXT::PathToUtf8(record->sourcePath);
        meta.displayName = TEXT::PathToUtf8(record->sourcePath.stem());
        meta.importSettingsJson = "{}";
    }

    record->guid = meta.guid;
    record->type = meta.type;
    record->meta = std::move(meta);
    record->displayName = record->meta.displayName;
    record->metaPath = GetMetaPathForSource(record->sourcePath);
    record->importedDirectory = GetImportedDirectory(record->guid);
    if (record->guid.IsValid()) {
        const size_t index = static_cast<size_t>(record - records_.data());
        recordsByGuid_[record->guid.value] = index;
        guidByNormalizedPath_[MakePathKey(record->sourcePath)] = index;
    }
    const bool wrote = WriteMeta(*record);
    RefreshRecordState(*record);
    record->importOutdated = true;
    record->lastImportMessage =
        wrote ? "[AssetDatabase] Meta regenerated; reimport required" : "[AssetDatabase] Meta regeneration failed";
    return wrote;
}

std::filesystem::path AssetDatabase::GetMetaPathForSource(const std::filesystem::path& sourcePath) const {
    const std::filesystem::path relativeSource = NormalizeProjectPath(sourcePath);
    return ASSETS::SEMANTICS::MakeSourceMetaPath(sourceMetaRoot_, relativeSource);
}

std::filesystem::path AssetDatabase::GetArtifactManifestPath(const AssetGuid& guid) const {
    if (!guid.IsValid()) {
        return {};
    }
    return ASSETS::SEMANTICS::MakeArtifactManifestPath(libraryRoot_, guid.value);
}

std::filesystem::path AssetDatabase::GetImportedDirectory(const AssetGuid& guid) const {
    if (!guid.IsValid()) {
        return {};
    }
    return libraryRoot_ / "Imported" / guid.value;
}

AssetType AssetDatabase::GuessAssetTypeFromPath(const std::filesystem::path& sourcePath) const {
    return ASSETS::SEMANTICS::ClassifyAssetTypeFromPath(sourcePath);
}

std::string AssetDatabase::SelectDefaultImporterId(AssetType type, const std::filesystem::path& sourcePath) const {
    if (const ASSETS::SEMANTICS::AssetImporterSemantics* semantics =
            ASSETS::SEMANTICS::FindDefaultAssetImporterSemantics(type)) {
        return std::string(semantics->importerId);
    }
    if (const ASSETS::SEMANTICS::AssetImporterSemantics* semantics =
            ASSETS::SEMANTICS::ResolveAssetSourceSemantics(sourcePath)) {
        return std::string(semantics->importerId);
    }
    return {};
}

void AssetDatabase::RegisterDefaultImporters() {
    RegisterStructuredAssetImporters(importerRegistry_);
    importerRegistry_.Register(
        std::make_unique<SkyCubemapImporter>(std::make_unique<DirectXTexTextureImportBackend>()));
    importerRegistry_.Register(std::make_unique<TextureImporter>(std::make_unique<DirectXTexTextureImportBackend>()));
    importerRegistry_.Register(
        std::make_unique<SpriteAssetImporter>(std::make_unique<DirectXTexTextureImportBackend>()));
    importerRegistry_.Register(std::make_unique<SpriteMaterialImporter>());
    importerRegistry_.Register(std::make_unique<ShaderImporter>());
    importerRegistry_.Register(std::make_unique<ModelImporter>());
    importerRegistry_.Register(std::make_unique<PrefabAssetImporter>());
    importerRegistry_.Register(std::make_unique<TextAssetImporter>(false));
    importerRegistry_.Register(std::make_unique<TextAssetImporter>(true));
    importerRegistry_.Register(std::make_unique<MaterialImporter>());
    importerRegistry_.Register(std::make_unique<ModelMaterialBindingImporter>());
    importerRegistry_.Register(std::make_unique<VfxAssetImporter>());
}

void AssetDatabase::EnsureProjectDirectories() const {
    std::error_code ec{};
    const std::filesystem::path roots[] = {
        assetsRoot_,
        assetsRoot_ / "Models",
        assetsRoot_ / "Textures",
        assetsRoot_ / "Materials",
        assetsRoot_ / "Skies",
        assetsRoot_ / "IBL",
        assetsRoot_ / "Vfx",
        assetsRoot_ / "Shaders",
        assetsRoot_ / "Scenes",
        assetsRoot_ / "AnimationStateMachines",
        assetsRoot_ / "Dialogues",
        assetsRoot_ / "Sequences",
        libraryRoot_,
        libraryRoot_ / "Imported",
        libraryRoot_ / "Thumbnails",
        libraryRoot_ / "AssetDatabase",
        libraryRoot_ / "AssetDatabase" / "Artifacts",
        libraryRoot_ / "ShaderCache",
        projectSettingsRoot_,
        sourceMetaRoot_,
    };

    for (const std::filesystem::path& root : roots) {
        std::filesystem::create_directories(root, ec);
        ec.clear();
    }

    const std::filesystem::path settingsPath = projectSettingsRoot_ / "asset_pipeline.json";
    if (!std::filesystem::exists(settingsPath, ec)) {
        nlohmann::json settings{
            {"version", 1},
            {"assetsRoot", "Assets"},
            {"libraryRoot", "Library"},
            {"importedRoot", "Library/Imported"},
            {"thumbnailRoot", "Library/Thumbnails"},
            {"assetDatabaseRoot", "Library/AssetDatabase"},
            {"shaderCacheRoot", "Library/ShaderCache"},
            {"sourceMetaRoot", "ProjectSettings/AssetMeta"},
        };

        (void)SERIALIZATION::JSON::WriteJsonFile(settingsPath, settings);
    }
}

void AssetDatabase::AddDirectoryToCache(const std::filesystem::path& directory) {
    if (directory.empty()) {
        return;
    }

    std::filesystem::path normalized = NormalizeProjectPath(directory).lexically_normal();
    if (normalized.empty()) {
        return;
    }

    directories_.push_back(std::move(normalized));
}

void AssetDatabase::SortAndUniqueDirectories() {
    std::sort(directories_.begin(), directories_.end(),
              [this](const auto& lhs, const auto& rhs) { return MakePathKey(lhs) < MakePathKey(rhs); });
    directories_.erase(
        std::unique(directories_.begin(), directories_.end(),
                    [this](const auto& lhs, const auto& rhs) { return MakePathKey(lhs) == MakePathKey(rhs); }),
        directories_.end());
}

std::filesystem::path AssetDatabase::NormalizeProjectPath(const std::filesystem::path& path) const {
    if (path.empty()) {
        return {};
    }

    std::filesystem::path normalized = path.lexically_normal();
    if (normalized.is_absolute() && !projectRoot_.empty()) {
        std::error_code ec{};
        const std::filesystem::path relative = std::filesystem::relative(normalized, projectRoot_, ec);
        if (!ec && !relative.empty() && relative.native().find(L"..") != 0) {
            normalized = relative;
        }
    }
    return normalized.lexically_normal();
}

std::string AssetDatabase::MakePathKey(const std::filesystem::path& path) const {
    return TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(NormalizeProjectPath(path)));
}

bool AssetDatabase::IsPathUnderDirectory(const std::filesystem::path& path,
                                         const std::filesystem::path& directory) const {
    const std::string pathKey = MakePathKey(path);
    const std::string directoryKey = MakePathKey(directory);
    if (directoryKey.empty()) {
        return false;
    }
    if (pathKey == directoryKey) {
        return true;
    }
    const std::string prefix = directoryKey.back() == '/' ? directoryKey : directoryKey + '/';
    return pathKey.rfind(prefix, 0) == 0;
}

} // namespace HIKARI
