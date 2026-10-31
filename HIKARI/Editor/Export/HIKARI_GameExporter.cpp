#include "HIKARI_GameExporter.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Editor/Export/HIKARI_GameExecutableIcon.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Assets/Cooking/HIKARI_AssetCooker.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Export/HIKARI_GamePackageManifest.h"
#include "Editor/Export/HIKARI_GameExportPublication.h"
#include "Editor/Export/HIKARI_GameShippingArtifactValidator.h"
#include "Editor/Export/HIKARI_NativeRuntimeDeployment.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Project/HIKARI_ProjectDescriptor.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"
#include "Render3D/Settings/HIKARI_RenderQualityProfileStore.h"
#include "Render3D/Upscaling/HIKARI_StreamlineDeployment.h"
#include "Runtime/HIKARI_RuntimeHost.h"

#include <Windows.h>

namespace HIKARI::EDITOR {

namespace {
constexpr const char* kExportOutputFolderName = "HIKARI_Game";
constexpr const char* kProjectCookManifestPath = "ProjectSettings/game_export.json";

struct ProjectCookManifest {
    GameExportIdentity identity;
    // 網格シェーダー非対応の GPU で従来の頂点経路へ落としてよいか（描画が網格シェーダー頼みなら false）。
    bool allowTraditionalGeometryFallback = true;
    std::vector<std::string> sceneGuids{};
    std::vector<std::filesystem::path> assetRoots{};
    std::vector<std::filesystem::path> runtimePayloadRoots{};
};

const char* GameExportWindowModeName(GameExportWindowMode mode) noexcept {
    switch (mode) {
    case GameExportWindowMode::Windowed: return "Windowed";
    case GameExportWindowMode::TopmostFullscreen: return "Fullscreen";
    case GameExportWindowMode::BorderlessFullscreen:
    default: return "Borderless";
    }
}

std::string RelativeFileKey(const std::filesystem::path& path) {
    return TEXT::ToLowerAsciiCopy(path.lexically_normal().generic_string());
}

std::filesystem::path FindProjectRoot() {
    const ProjectContext& projectContext = GetProjectContext();
    if (projectContext.IsOpen()) {
        return projectContext.GetRoot();
    }

    const std::filesystem::path descriptorPath = FindProjectDescriptor(std::filesystem::current_path());
    if (!descriptorPath.empty()) {
        return descriptorPath.parent_path();
    }

    return std::filesystem::current_path();
}

std::filesystem::path NormalizeExportPath(const std::filesystem::path& path) {
    std::error_code ec{};
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if (!ec && !normalized.empty()) {
        return normalized;
    }

    ec.clear();
    normalized = std::filesystem::absolute(path, ec);
    if (!ec && !normalized.empty()) {
        return normalized.lexically_normal();
    }

    return path.lexically_normal();
}

std::filesystem::path ResolveOutputDirectory(const std::filesystem::path& projectRoot,
                                             const std::filesystem::path& requestedDirectory) {
    if (requestedDirectory.empty()) {
        return NormalizeExportPath(projectRoot / "Build" / "Exports" / kExportOutputFolderName);
    }

    std::filesystem::path outputDirectory = requestedDirectory;
    if (outputDirectory.is_relative()) {
        outputDirectory = projectRoot / outputDirectory;
    }
    return NormalizeExportPath(outputDirectory);
}

bool CopyFileChecked(const std::filesystem::path& from, const std::filesystem::path& to, std::string& errorMessage) {
    std::error_code ec{};
    if (!std::filesystem::exists(from, ec) || ec) {
        errorMessage = "Missing file: " + from.string();
        return false;
    }

    std::filesystem::create_directories(to.parent_path(), ec);
    if (ec) {
        errorMessage = "Could not create directory: " + to.parent_path().string();
        return false;
    }

    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        errorMessage = "Could not copy file: " + from.string() + " -> " + to.string();
        return false;
    }
    return true;
}

bool CopyDirectoryChecked(const std::filesystem::path& from, const std::filesystem::path& to, std::string& errorMessage,
                          bool generatedRuntimeOnly = false,
                          const std::unordered_set<std::string>* excludedRelativeFiles = nullptr) {
    std::error_code ec{};
    if (!std::filesystem::exists(from, ec) || ec) {
        return true;
    }

    std::filesystem::create_directories(to, ec);
    if (ec) {
        errorMessage = "Could not create directory: " + to.string();
        return false;
    }

    const std::filesystem::recursive_directory_iterator end{};
    std::filesystem::recursive_directory_iterator it(from, ec);
    if (ec) {
        errorMessage = "Could not iterate directory: " + from.string();
        return false;
    }

    for (; it != end; it.increment(ec)) {
        if (ec) {
            errorMessage = "Could not iterate directory: " + from.string();
            return false;
        }

        const std::filesystem::path relativePath = std::filesystem::relative(it->path(), from, ec);
        if (ec) {
            errorMessage = "Could not compute relative export path: " + it->path().string();
            return false;
        }

        const std::filesystem::path targetPath = to / relativePath;
        if (it->is_directory(ec)) {
            if (ec) {
                errorMessage = "Could not inspect directory: " + it->path().string();
                return false;
            }
            std::filesystem::create_directories(targetPath, ec);
            if (ec) {
                errorMessage = "Could not create directory: " + targetPath.string();
                return false;
            }
            continue;
        }

        if (!it->is_regular_file(ec)) {
            if (ec) {
                errorMessage = "Could not inspect file: " + it->path().string();
                return false;
            }
            continue;
        }

        if (excludedRelativeFiles &&
            excludedRelativeFiles->find(RelativeFileKey(relativePath)) != excludedRelativeFiles->end()) {
            continue;
        }

        if (generatedRuntimeOnly) {
            const std::string genericPath = TEXT::ToLowerAsciiCopy(relativePath.generic_string());
            if (genericPath.find("/captures/") != std::string::npos || genericPath.ends_with("_capture.dds")) {
                continue;
            }
        }

        std::filesystem::create_directories(targetPath.parent_path(), ec);
        if (ec) {
            errorMessage = "Could not create directory: " + targetPath.parent_path().string();
            return false;
        }

        std::filesystem::copy_file(it->path(), targetPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            errorMessage = "Could not copy file: " + it->path().string() + " -> " + targetPath.string();
            return false;
        }
    }
    return true;
}

std::vector<GameExportSceneInfo> CollectSceneAssetsFromProjectRoot(const std::filesystem::path& projectRoot) {
    AssetDatabase assetDatabase{};
    if (!assetDatabase.Initialize(projectRoot)) {
        return {};
    }

    if (!assetDatabase.ScanAssets(false)) {
        return {};
    }

    ProjectSettingsService settings{};
    settings.Load(projectRoot);
    const AssetGuid projectStartupGuid = settings.GetSettings().startupSceneGuid;

    std::vector<const AssetRecord*> records = assetDatabase.CollectByType(AssetType::Scene);
    std::sort(records.begin(), records.end(), [](const AssetRecord* lhs, const AssetRecord* rhs) {
        if (!lhs || !rhs) {
            return lhs < rhs;
        }
        return GetAssetRecordDisplayName(*lhs) < GetAssetRecordDisplayName(*rhs);
    });

    std::vector<GameExportSceneInfo> scenes{};
    scenes.reserve(records.size());
    for (const AssetRecord* record : records) {
        if (!record || !record->guid.IsValid()) {
            continue;
        }

        GameExportSceneInfo info{};
        info.guid = record->guid.value;
        info.displayName = GetAssetRecordDisplayName(*record);
        info.sourcePath = record->sourcePath;
        info.projectStartup = projectStartupGuid.IsValid() && record->guid == projectStartupGuid;
        scenes.push_back(std::move(info));
    }
    return scenes;
}

bool ResolveExportRenderQuality(const std::filesystem::path& projectRoot, const GameExportOptions& options,
                                RENDER3D::RenderQualitySettings& outQuality, std::string& errorMessage) {
    outQuality = RENDER3D::GetRenderQualitySettings();
    std::string qualityMessage{};
    if (!RENDER3D::LoadRenderQualityProfile(projectRoot, outQuality, &qualityMessage) && !qualityMessage.empty()) {
        errorMessage = "Could not load project render quality for export: " + qualityMessage;
        return false;
    }
    switch (options.windowMode) {
    case GameExportWindowMode::Windowed: outQuality.windowMode = RENDER3D::WindowPresentationMode::Windowed; break;
    case GameExportWindowMode::TopmostFullscreen:
        outQuality.windowMode = RENDER3D::WindowPresentationMode::Fullscreen;
        break;
    case GameExportWindowMode::BorderlessFullscreen:
    default: outQuality.windowMode = RENDER3D::WindowPresentationMode::BorderlessWindow; break;
    }
    return true;
}

bool WriteRuntimeConfig(const std::filesystem::path& outputDirectory, const GameExportOptions& options,
                        const RENDER3D::RenderQualitySettings& quality, bool allowTraditionalGeometryFallback,
                        std::string& errorMessage) {
    const std::filesystem::path configPath = outputDirectory / "HIKARI" / "runtime_config.json";
    std::error_code ec{};
    std::filesystem::create_directories(configPath.parent_path(), ec);
    if (ec) {
        errorMessage = "Could not create runtime config directory: " + configPath.parent_path().string();
        return false;
    }

    RENDER3D::RenderResolution windowResolution = RENDER3D::ResolveFixedRenderResolution(quality.windowSize);
    if (windowResolution.width <= 0 || windowResolution.height <= 0) {
        windowResolution = {1280, 720};
    }

    nlohmann::json runtime{
        {"hostMode", RuntimeHostModeName(RuntimeHostMode::ExportedGame)},
        {"projectRoot", "../Content"},
        {"enableImGui", false},
        {"enableEditorUI", false},
        {"enableDebugLayer", false},
        {"enableDebugCamera", false},
        {"resizableWindow", quality.windowMode == RENDER3D::WindowPresentationMode::Windowed},
        {"allowTraditionalGeometryFallback", allowTraditionalGeometryFallback},
        {"windowWidth", windowResolution.width},
        {"windowHeight", windowResolution.height},
        {"renderQuality", RENDER3D::SerializeRenderQualitySettings(quality)},
    };

    if (!options.identity->windowTitle.empty()) {
        runtime["windowTitle"] = options.identity->windowTitle;
    }

    if (!options.startupSceneGuid.empty()) {
        runtime["startupSceneGuid"] = options.startupSceneGuid;
    }
    if (!options.sceneGuids.empty()) {
        runtime["exportedSceneGuids"] = options.sceneGuids;
    }

    const nlohmann::json root{{"runtime", std::move(runtime)}};

    std::ofstream ofs(configPath);
    if (!ofs) {
        errorMessage = "Could not write runtime config: " + configPath.string();
        return false;
    }
    ofs << root.dump(4);
    return true;
}

// 地形の機能（地表の触れ合いは地形を要る）を使わない項目は、地形と砂の内蔵テクスチャを運ばない。
// 実行時の登録は、この二つの領域が丸ごと無ければ省いたものとして扱う（RegisterEngineContentTextures）。
bool ProjectUsesTerrainContent(const std::vector<std::string>& enabledRuntimeFeatures) {
    return std::any_of(enabledRuntimeFeatures.begin(), enabledRuntimeFeatures.end(),
                       [](const std::string& id) { return id == "Terrain" || id == "SurfaceInteraction"; });
}

bool CopyInstalledEngineContent(const std::filesystem::path& runtimeRoot, const std::filesystem::path& outputDirectory,
                                bool includeTerrainContent, std::string& errorMessage) {
    const std::filesystem::path engineContentSource = runtimeRoot / "EngineContent";
    std::error_code engineContentError{};
    if (!std::filesystem::is_directory(engineContentSource, engineContentError) || engineContentError) {
        errorMessage = "Installed EngineContent is missing: " + engineContentSource.string();
        return false;
    }
    std::unordered_set<std::string> sourceOnlyExclusions{};
    std::error_code scanError{};
    std::filesystem::recursive_directory_iterator iterator(
        engineContentSource, std::filesystem::directory_options::skip_permission_denied, scanError);
    const std::filesystem::recursive_directory_iterator end{};
    for (; !scanError && iterator != end; iterator.increment(scanError)) {
        if (!iterator->is_regular_file(scanError)) {
            scanError.clear();
            continue;
        }
        const std::filesystem::path relative =
            std::filesystem::relative(iterator->path(), engineContentSource, scanError);
        if (scanError) {
            break;
        }
        const std::string key = RelativeFileKey(relative);
        const auto separator = key.find('/');
        const std::string_view domain = std::string_view(key).substr(0u, separator);
        const std::string_view local =
            separator == std::string::npos ? std::string_view{} : std::string_view(key).substr(separator + 1u);
        const bool textureDomain = domain == "sand" || domain == "terrain" || domain == "vfx" || domain == "painterly";
        const bool textureSource =
            textureDomain && local.starts_with("textures/") && !local.starts_with("textures/cooked/");
        const bool surfaceSource =
            (domain == "terrain" || domain == "painterly") && (local.starts_with("sources/") || local == "readme.md");
        const bool unusedTerrain = !includeTerrainContent && (domain == "terrain" || domain == "sand");
        if (IsForbiddenGamePackagePath(std::filesystem::path("EngineContent") / relative) || textureSource ||
            surfaceSource || unusedTerrain) {
            sourceOnlyExclusions.insert(key);
        }
    }
    if (scanError) {
        errorMessage = "Could not inspect installed EngineContent: " + scanError.message();
        return false;
    }
    if (!CopyDirectoryChecked(engineContentSource, outputDirectory / "EngineContent", errorMessage, false,
                              &sourceOnlyExclusions)) {
        return false;
    }
    // 運ばなかった地形・砂の領域は、複写がフォルダだけ作るので空のフォルダごと取り除く（ファイルが残っていれば触らない）。
    if (!includeTerrainContent) {
        for (const char* domain : {"Terrain", "Sand"}) {
            const std::filesystem::path root = outputDirectory / "EngineContent" / domain;
            std::error_code ec{};
            bool empty = true;
            for (std::filesystem::recursive_directory_iterator it(root, ec), last; !ec && it != last; it.increment(ec)) {
                if (it->is_regular_file(ec)) {
                    empty = false;
                    break;
                }
            }
            if (empty) {
                std::filesystem::remove_all(root, ec);
            }
        }
    }
    return true;
}

bool LoadProjectCookManifest(const std::filesystem::path& projectRoot, ProjectCookManifest& outManifest,
                             std::string& outMessage) {
    outManifest = {};
    outMessage.clear();
    const std::filesystem::path manifestPath = projectRoot / kProjectCookManifestPath;
    std::error_code ec{};
    if (!std::filesystem::exists(manifestPath, ec) || ec) {
        return true;
    }
    try {
        std::ifstream stream(manifestPath, std::ios::binary);
        if (!stream) {
            outMessage = "Could not open project game export manifest: " + manifestPath.generic_string();
            return false;
        }
        nlohmann::json root{};
        stream >> root;
        if (!root.is_object() || root.value("version", 0) != 1) {
            outMessage = "Project game export manifest must use version 1: " + manifestPath.generic_string();
            return false;
        }
        if (!ParseGameExportIdentity(root, outManifest.identity, outMessage))
            return false;
        if (const auto fallback = root.find("allowTraditionalGeometryFallback"); fallback != root.end()) {
            if (!fallback->is_boolean()) {
                outMessage = "allowTraditionalGeometryFallback must be a boolean: " + manifestPath.generic_string();
                return false;
            }
            outManifest.allowTraditionalGeometryFallback = fallback->get<bool>();
        }
        const auto readStrings = [&](const char* key, auto&& visitor) {
            const auto found = root.find(key);
            if (found == root.end()) {
                return true;
            }
            if (!found->is_array()) {
                return false;
            }
            for (const auto& node : *found) {
                if (!node.is_string() || node.get_ref<const std::string&>().empty()) {
                    return false;
                }
                visitor(node.get_ref<const std::string&>());
            }
            return true;
        };
        if (!readStrings("sceneGuids", [&](const std::string& value) { outManifest.sceneGuids.push_back(value); }) ||
            !readStrings("assetRoots", [&](const std::string& value) { outManifest.assetRoots.emplace_back(value); }) ||
            !readStrings("runtimePayloadRoots",
                         [&](const std::string& value) { outManifest.runtimePayloadRoots.emplace_back(value); })) {
            outMessage = "Project game export manifest contains an invalid list: " + manifestPath.generic_string();
            return false;
        }
        for (const std::string& guid : outManifest.sceneGuids) {
            if (!AssetGuid{guid}.IsValid()) {
                outMessage = "Project game export manifest contains an invalid scene GUID: " + guid;
                return false;
            }
        }
        return true;
    } catch (const std::exception& error) {
        outMessage = "Could not parse project game export manifest: " + std::string(error.what());
        return false;
    }
}

std::filesystem::path ResolveInstalledProjectModule(const std::filesystem::path& projectRoot,
                                                    const std::filesystem::path&) {
    // Formal exports consume only the installed project product.
    // Library/build and engine-directory fallbacks can silently mix
    // incompatible SDK or stale gameplay binaries.
    return projectRoot / "out" / "install" / "game-shipping" / "HIKARI_GameProject.dll";
}

// Windows が 260 字を超えるパスを許しているか（実行ファイルの longPathAware と LongPathsEnabled の両方が要る）。
bool LongPathsEnabled() {
    using Query = BOOLEAN(WINAPI*)();
    const auto query =
        reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlAreLongPathsEnabled"));
    return query != nullptr && query() != FALSE;
}
constexpr size_t kMaxWindowsPath = 259; // MAX_PATH から終端を除いた字数

// root の下で一番長い相対パスの字数（UTF-16、root との区切りは含まない）。
size_t DeepestRelativePath(const std::filesystem::path& root) {
    size_t deepest = 0;
    std::error_code ec;
    const size_t rootLength = root.native().size() + 1;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        deepest = (std::max)(deepest, it->path().native().size() - (std::min)(rootLength, it->path().native().size()));
    return deepest;
}

// プロジェクトの実行時コードが文字列で直に読む資産（"Assets/…" と書いた道筋）。場面の参照からはたどれないので、
// game_export.json に書き漏れると包の中で黙って欠ける。実行時の源（項目の CMakeLists の SOURCE_DIR から
// EDITOR_SOURCE_DIR を除いた所、読めなければ Source）を走査し、登録済みの資産で清単に無い物だけを焼く対象へ足す。
// 資産でないファイル（付属の .bin、データ）やフォルダ・書式の穴（% や {}）を含む文字列は足さない。無い道筋は知らせる。
struct CodeAssetReferences {
    std::vector<std::filesystem::path> added;
    std::vector<std::string> missing;
};
CodeAssetReferences CollectCodeReferencedAssets(const std::filesystem::path& projectRoot,
                                                const ProjectCookManifest& manifest) {
    CodeAssetReferences result;
    const auto key = [](const std::filesystem::path& path) {
        return TEXT::ToLowerAsciiCopy(path.lexically_normal().generic_string());
    };
    std::filesystem::path cmakePath = projectRoot / "CMakeLists.txt";
    if (std::ifstream descriptor(projectRoot / "Project.hikari", std::ios::binary); descriptor) {
        const auto json = nlohmann::json::parse(descriptor, nullptr, false);
        if (json.is_object() && json.contains("gameProjectManifest") && json["gameProjectManifest"].is_string()) {
            try {
                cmakePath = projectRoot / TEXT::PathFromUtf8(json["gameProjectManifest"].get<std::string>());
            } catch (const std::exception&) {
            }
        }
    }
    std::vector<std::filesystem::path> runtimeDirs, editorDirs;
    if (std::ifstream cmake(cmakePath, std::ios::binary); cmake) {
        const std::string text{std::istreambuf_iterator<char>(cmake), {}};
        const auto read = [&](const std::string& word, std::vector<std::filesystem::path>& out) {
            for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + word.size())) {
                if (at > 0 && (std::isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_'))
                    continue; // SOURCE_DIR を探す時に EDITOR_SOURCE_DIR を拾わない
                const size_t begin = text.find_first_not_of(" \t\r\n\"", at + word.size());
                const size_t end = begin == std::string::npos ? begin : text.find_first_of(" \t\r\n\")", begin);
                if (begin == std::string::npos || end == std::string::npos || end <= begin)
                    continue;
                const std::string value = text.substr(begin, end - begin);
                if (value.find("${") != std::string::npos)
                    continue;
                try {
                    out.push_back((cmakePath.parent_path() / TEXT::PathFromUtf8(value)).lexically_normal());
                } catch (const std::exception&) {
                }
            }
        };
        read("SOURCE_DIR", runtimeDirs);
        read("EDITOR_SOURCE_DIR", editorDirs);
    }
    std::erase_if(runtimeDirs, [](const std::filesystem::path& dir) {
        std::error_code ec;
        return !std::filesystem::is_directory(dir, ec);
    });
    if (runtimeDirs.empty())
        runtimeDirs.push_back(projectRoot / "Source");
    std::vector<std::string> editorKeys;
    for (const auto& dir : editorDirs)
        editorKeys.push_back(key(dir) + "/");
    const auto editorOnly = [&](const std::filesystem::path& file) {
        const std::string fileKey = key(file);
        return std::any_of(editorKeys.begin(), editorKeys.end(),
                           [&](const std::string& dir) { return fileKey.starts_with(dir); });
    };

    std::vector<std::string> roots;
    for (const auto* list : {&manifest.assetRoots, &manifest.runtimePayloadRoots})
        for (const auto& root : *list)
            roots.push_back(key(root));
    const auto covered = [&](const std::string& literalKey) {
        return std::any_of(roots.begin(), roots.end(), [&](const std::string& root) {
            return literalKey == root ||
                   (literalKey.starts_with(root) && (root.ends_with('/') || literalKey[root.size()] == '/'));
        });
    };

    AssetDatabase database{};
    if (!database.Initialize(projectRoot) || !database.ScanAssets(false))
        return result; // 見られない時は足さない（清単どおりに焼く）
    std::unordered_set<std::string> seen;
    for (const auto& dir : runtimeDirs) {
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code entry;
            const auto extension = TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(it->path().extension()));
            if (!it->is_regular_file(entry) || editorOnly(it->path()) ||
                (extension != ".cpp" && extension != ".cc" && extension != ".h" && extension != ".hpp" && extension != ".inl"))
                continue;
            std::ifstream source(it->path(), std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(source), {}};
            for (size_t at = text.find("\"Assets/"); at != std::string::npos; at = text.find("\"Assets/", at + 1)) {
                const size_t close = text.find_first_of("\"\r\n", at + 1);
                if (close == std::string::npos || text[close] != '"')
                    continue;
                const std::string literal = text.substr(at + 1, close - at - 1);
                if (literal.ends_with('/') || literal.find_first_of("%{}\\*") != std::string::npos ||
                    !seen.insert(literal).second)
                    continue;
                std::filesystem::path relative;
                try {
                    relative = TEXT::PathFromUtf8(literal).lexically_normal();
                } catch (const std::exception&) {
                    continue; // UTF-8 でない源の文字列
                }
                if (covered(key(relative)))
                    continue;
                std::error_code exists;
                const AssetRecord* record = database.FindByPath(relative);
                if (record != nullptr && record->guid.IsValid())
                    result.added.push_back(relative);
                else if (!std::filesystem::exists(projectRoot / relative, exists))
                    result.missing.push_back(literal);
            }
        }
    }
    return result;
}
} // namespace

std::filesystem::path GameExporter::GetDefaultOutputDirectory() {
    const std::filesystem::path projectRoot = FindProjectRoot();
    return ResolveOutputDirectory(projectRoot, {});
}

std::vector<GameExportSceneInfo> GameExporter::CollectSceneAssets() {
    return CollectSceneAssetsFromProjectRoot(FindProjectRoot());
}

GameExportResult GameExporter::Export(const GameExportOptions& options) {
    const std::filesystem::path projectRoot = FindProjectRoot();
    GameExportOptions effectiveOptions = options;
    ProjectCookManifest cookManifest{};
    std::string cookManifestError{};
    if (!LoadProjectCookManifest(projectRoot, cookManifest, cookManifestError)) {
        GameExportResult failure{};
        failure.message = cookManifestError;
        return failure;
    }
    if (!effectiveOptions.identity)
        effectiveOptions.identity = cookManifest.identity;
    const auto& identity = *effectiveOptions.identity;
    if (!ValidateGameExportIdentity(projectRoot, identity, cookManifestError))
        return {false, {}, cookManifestError};
    ProjectSettingsService projectSettings{};
    bool includeTerrainContent = true;
    if (projectSettings.Load(projectRoot)) {
        includeTerrainContent = ProjectUsesTerrainContent(projectSettings.GetSettings().enabledRuntimeFeatures);
        if (effectiveOptions.startupSceneGuid.empty() &&
            IsValidAssetGuid(projectSettings.GetSettings().startupSceneGuid.value)) {
            effectiveOptions.startupSceneGuid = projectSettings.GetSettings().startupSceneGuid.value;
        }
    }
    effectiveOptions.sceneGuids.insert(effectiveOptions.sceneGuids.end(), cookManifest.sceneGuids.begin(),
                                       cookManifest.sceneGuids.end());
    if (!effectiveOptions.startupSceneGuid.empty() && !IsValidAssetGuid(effectiveOptions.startupSceneGuid)) {
        return {false, {}, "Startup Scene GUID must be 32 hexadecimal characters."};
    }
    for (const std::string& sceneGuid : effectiveOptions.sceneGuids) {
        if (!IsValidAssetGuid(sceneGuid)) {
            return {false, {}, "Export Scene GUID must be 32 hexadecimal characters: " + sceneGuid};
        }
    }
    if (effectiveOptions.sceneGuids.empty() && !effectiveOptions.startupSceneGuid.empty()) {
        // Export() is also used by non-UI callers.  Resolve the canonical
        // project startup scene here so a first headless export cooks a
        // deterministic closure instead of silently selecting every
        // authored asset.
        effectiveOptions.sceneGuids.push_back(effectiveOptions.startupSceneGuid);
    }
    if (!effectiveOptions.startupSceneGuid.empty() &&
        std::find(effectiveOptions.sceneGuids.begin(), effectiveOptions.sceneGuids.end(),
                  effectiveOptions.startupSceneGuid) == effectiveOptions.sceneGuids.end()) {
        // The runtime cannot start from a scene omitted from the cooked
        // selection.  Keep an explicit story/battle selection valid by
        // adding the canonical startup scene before sorting/deduping.
        effectiveOptions.sceneGuids.push_back(effectiveOptions.startupSceneGuid);
    }
    std::sort(effectiveOptions.sceneGuids.begin(), effectiveOptions.sceneGuids.end());
    effectiveOptions.sceneGuids.erase(
        std::unique(effectiveOptions.sceneGuids.begin(), effectiveOptions.sceneGuids.end()),
        effectiveOptions.sceneGuids.end());
    const std::filesystem::path outputDirectory = ResolveOutputDirectory(projectRoot, options.outputDirectory);
    std::filesystem::path outputExecutableFileName{};
    std::string executableNameError{};
    if (!ResolveExportExecutableFileName(effectiveOptions.identity->executableName, outputExecutableFileName,
                                         executableNameError)) {
        return {
            false,
            outputDirectory,
            std::move(executableNameError),
        };
    }
    effectiveOptions.identity->executableName = TEXT::PathToUtf8(outputExecutableFileName);
    std::filesystem::path sourceExe;
    if (!ResolveGameShippingHostExecutable(projectRoot / "out/install/game-shipping", sourceExe, executableNameError))
        return {false, outputDirectory, executableNameError};
    const std::filesystem::path sourceProjectModule =
        ResolveInstalledProjectModule(projectRoot, sourceExe.parent_path());

    GameExportResult result{};
    result.outputDirectory = outputDirectory;

    std::string errorMessage{};
    const auto report = [&](const std::string& stage, float fraction) {
        if (effectiveOptions.progress && !effectiveOptions.progress(stage, fraction)) {
            result.message = "Export cancelled. The previous package was preserved.";
            return false;
        }
        return true;
    };
    if (!report("Checking Shipping artifacts", 0.05f))
        return result;
    RENDER3D::RenderQualitySettings renderQuality{};
    if (!ResolveExportRenderQuality(projectRoot, effectiveOptions, renderQuality, errorMessage)) {
        result.message = errorMessage;
        HIKARI_LOG_WARN(std::string("[GameExporter] ") + result.message);
        return result;
    }
    RENDER3D::UPSCALING::StreamlineDeploymentOptions streamlineDeployment{
        RENDER3D::IsDlaaAntiAliasingMode(renderQuality.antiAliasingMode) ||
            RENDER3D::IsDlssAntiAliasingMode(renderQuality.antiAliasingMode),
        renderQuality.frameGenerationMode == RENDER3D::RenderFrameGenerationMode::Dlss,
    };

    streamlineDeployment.requireDlss =
        streamlineDeployment.requireDlss || std::filesystem::exists(sourceExe.parent_path() / "sl.dlss.dll");
    // フレーム生成は画質設定で有効な時だけ同梱する（単体ゲームは無効なら DLSS-G を読み込まない）。

    std::error_code sourceExeError{};
    if (!std::filesystem::is_regular_file(sourceExe, sourceExeError) || sourceExeError) {
        result.message = "Installed Shipping runtime is missing: " + sourceExe.string() +
                         ". Run .\\Tools\\Build\\Build-Hikari.ps1 "
                         "-Preset game-shipping -Install before exporting.";
        HIKARI_LOG_WARN(std::string("[GameExporter] ") + result.message);
        return result;
    }
    sourceExeError.clear();
    if (!std::filesystem::is_regular_file(sourceProjectModule, sourceExeError) || sourceExeError) {
        result.message = "Installed GameProject module is missing: " + sourceProjectModule.string() +
                         ". Run .\\Tools\\Build\\Build-Hikari.ps1 "
                         "-Preset game-shipping -Project \"" +
                         projectRoot.string() + "\" -Install before exporting.";
        HIKARI_LOG_WARN(std::string("[GameExporter] ") + result.message);
        return result;
    }
    if (!ValidateGameShippingArtifacts(projectRoot, sourceExe, sourceProjectModule, errorMessage)) {
        result.message = errorMessage;
        HIKARI_LOG_WARN(std::string("[GameExporter] ") + result.message);
        return result;
    }

    GameExportPublication publication;
    if (!publication.Begin(outputDirectory, projectRoot, errorMessage)) {
        result.message = errorMessage;
        return result;
    }
    const auto& stagingDirectory = publication.StagingDirectory();
    // 包で一番深いのは内蔵シェーダーの産物。Windows が長いパスを許さない時、出力先が深すぎると途中の複写が
    // 分かりにくい失敗になるので、先に深さを確かめて出力先の上限を伝える。発行の時は前の包を
    // 「作業フォルダ名 + .previous」へ退かすので、作業フォルダより 9 字長い名前にも収まる必要がある。
    const bool longPaths = LongPathsEnabled();
    const long long engineDepth = static_cast<long long>(std::string_view("EngineContent/").size() +
                                                         DeepestRelativePath(sourceExe.parent_path() / "EngineContent"));
    const long long stagingLength =
        static_cast<long long>(stagingDirectory.native().size() + std::wstring_view(L".previous").size());
    if (!longPaths && stagingLength + 1 + engineDepth > static_cast<long long>(kMaxWindowsPath)) {
        const long long outputLength = static_cast<long long>(outputDirectory.native().size());
        const long long limit = static_cast<long long>(kMaxWindowsPath) - 1 - engineDepth - (stagingLength - outputLength);
        result.message = "The output folder is too deep for Windows paths: the package needs " +
                         std::to_string(engineDepth) + " characters below it. Choose an output folder of at most " +
                         std::to_string((std::max)(0ll, limit)) + " characters (now " + std::to_string(outputLength) +
                         "), or enable Windows long paths.";
        HIKARI_LOG_WARN(std::string("[GameExporter] ") + result.message);
        return result;
    }
    const auto outputExe = stagingDirectory / outputExecutableFileName;
    const auto outputProjectModule = stagingDirectory / "HIKARI_GameProject.dll";
    if (!report("Copying Shipping runtime", 0.15f))
        return result;

    const NativeRuntimeDeploymentResult nativeRuntime =
        DeployNativeRuntimeDependencies(sourceExe.parent_path(), stagingDirectory);
    if (!nativeRuntime.success) {
        result.message = nativeRuntime.message;
        HIKARI_LOG_WARN(std::string("[GameExporter] ") + result.message);
        return result;
    }
    if (!CopyFileChecked(sourceExe, outputExe, errorMessage) ||
        !CopyFileChecked(sourceProjectModule, outputProjectModule, errorMessage) ||
        !RENDER3D::UPSCALING::DeployStreamlineRuntime(sourceExe.parent_path(), stagingDirectory, streamlineDeployment,
                                                      errorMessage) ||
        !WriteRuntimeConfig(stagingDirectory, effectiveOptions, renderQuality, cookManifest.allowTraditionalGeometryFallback,
                            errorMessage) ||
        !CopyInstalledEngineContent(sourceExe.parent_path(), stagingDirectory, includeTerrainContent, errorMessage)) {
        result.message = errorMessage;
        HIKARI_LOG_WARN(std::string("[GameExporter] ") + result.message);
        return result;
    }

    if (!identity.iconPath.empty() &&
        !EmbedGameExecutableIcon(outputExe, projectRoot / TEXT::PathFromUtf8(identity.iconPath), errorMessage)) {
        result.message = errorMessage;
        return result;
    }

    if (!report("Checking and cooking project assets", 0.3f))
        return result;
    const CodeAssetReferences codeAssets = CollectCodeReferencedAssets(projectRoot, cookManifest);
    for (const auto& asset : codeAssets.added)
        HIKARI_LOG_INFO("[GameExporter] Cooking asset referenced only by project code: " + TEXT::PathToUtf8(asset));
    for (const auto& asset : codeAssets.missing)
        HIKARI_LOG_WARN("[GameExporter] Project code refers to a missing asset: " + asset);
    cookManifest.assetRoots.insert(cookManifest.assetRoots.end(), codeAssets.added.begin(), codeAssets.added.end());
    const ASSETS::COOKING::AssetCookResult cookResult = ASSETS::COOKING::AssetCooker{}.Cook({
        projectRoot,
        stagingDirectory / "Content",
        effectiveOptions.startupSceneGuid,
        effectiveOptions.sceneGuids,
        cookManifest.assetRoots,
        cookManifest.runtimePayloadRoots,
        [&](const std::string& stage, float fraction) { return report(stage, 0.3f + 0.5f * fraction); },
    });
    if (!cookResult.success) {
        result.message = cookResult.message;
        HIKARI_LOG_WARN(std::string("[GameExporter] ") + result.message);
        return result;
    }

    if (!report("Validating package and hashing files", 0.85f))
        return result;
    const GamePackageManifestOptions packageManifestOptions{
        effectiveOptions.startupSceneGuid,
        effectiveOptions.sceneGuids,
        GameExportWindowModeName(effectiveOptions.windowMode),
        effectiveOptions.identity->executableName,
    };
    if (!ValidateGamePackageBoundary(stagingDirectory, packageManifestOptions, errorMessage) ||
        !WriteGamePackageManifest(stagingDirectory, packageManifestOptions, errorMessage)) {
        result.message = errorMessage;
        HIKARI_LOG_WARN(std::string("[GameExporter] ") + result.message);
        return result;
    }

    // 遊ぶ人が展開するフォルダの上限（長いパスが無効な PC では、包の中の一番深いパスがこれに足される）。
    const long long unpackLimit =
        static_cast<long long>(kMaxWindowsPath) - 1 - static_cast<long long>(DeepestRelativePath(stagingDirectory));

    if (!report("Publishing validated package", 0.98f))
        return result;
    if (!publication.Publish(errorMessage)) {
        result.message = errorMessage;
        return result;
    }

    result.success = true;
    if (effectiveOptions.progress)
        effectiveOptions.progress("Export complete", 1.0f);
    // 画質の設定は ProjectSettings/render_quality.json から来るので、何で書き出したかを結果に必ず残す。
    const nlohmann::json quality = RENDER3D::SerializeRenderQualitySettings(renderQuality);
    const std::string frameGeneration = quality.value("frameGenerationMode", std::string{"Off"});
    result.message = "Exported " + std::to_string(effectiveOptions.sceneGuids.size()) + " scene(s), " +
                     std::to_string(cookResult.assetCount) + " cooked asset(s), and " +
                     std::to_string(cookResult.fileCount) + " cooked file(s) to " + TEXT::PathToUtf8(outputDirectory) +
                     ". Anti-aliasing: " + quality.value("antiAliasingMode", std::string{"?"}) + "; frame generation: " +
                     (frameGeneration == "Off" ? std::string{"off"}
                                               : frameGeneration + " x" +
                                                     std::to_string(quality.value("frameGenerationMultiplier", 2))) +
                     " (ProjectSettings/render_quality.json).";
    if (!codeAssets.added.empty())
        result.message += " Also cooked " + std::to_string(codeAssets.added.size()) +
                          " asset(s) referenced only from project code.";
    if (!codeAssets.missing.empty())
        result.message += " Project code refers to " + std::to_string(codeAssets.missing.size()) +
                          " missing asset path(s), e.g. " + codeAssets.missing.front() + ".";
    result.message += " Players must unpack it into a folder path of at most " +
                      std::to_string((std::max)(0ll, unpackLimit)) +
                      " characters unless Windows long paths are enabled.";
    HIKARI_LOG_INFO(std::string("[GameExporter] ") + result.message);
    return result;
}

} // namespace HIKARI::EDITOR
