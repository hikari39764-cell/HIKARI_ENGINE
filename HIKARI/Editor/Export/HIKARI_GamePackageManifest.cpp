#include "Editor/Export/HIKARI_GamePackageManifest.h"
#include "Editor/Export/HIKARI_GamePackageHash.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Assets/Cooking/HIKARI_CookedContentManifest.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectDescriptor.h"

namespace HIKARI::EDITOR {
    namespace {

        constexpr char kProductManifestFilename[] =
            "product_manifest.json";
        constexpr uint32_t kProductManifestFormatVersion = 2u;

        struct ManifestFile final {
            std::string path{};
            uintmax_t size = 0u;
            std::string sha256{};
        };

        bool StartsWith(
            std::string_view value,
            std::string_view prefix) {

            return value.size() >= prefix.size() &&
                value.substr(0u, prefix.size()) == prefix;
        }

        bool IsForbiddenPackagePath(
            const std::filesystem::path& relativePath) {

            const std::string path =
                TEXT::ToLowerAsciiCopy(
                    TEXT::PathToUtf8(
                        relativePath.lexically_normal()));
            const std::string filename =
                TEXT::ToLowerAsciiCopy(
                    TEXT::PathToUtf8(
                        relativePath.filename()));
            const std::string extension =
                TEXT::ToLowerAsciiCopy(
                    TEXT::PathToUtf8(
                        relativePath.extension()));

            if (StartsWith(path, "assets/") ||
                StartsWith(path, "source/") ||
                StartsWith(path, "gameproject/") ||
                StartsWith(
                    path,
                    "content/assets/") ||
                StartsWith(
                    path,
                    "content/projectsettings/assetmeta/") ||
                StartsWith(
                    path,
                    "content/library/assetdatabase/") ||
                StartsWith(
                    path,
                    "content/library/shadercache/")) {
                return true;
            }

            if (filename == "cmakelists.txt" ||
                path.ends_with(
                    ".hikari.asset.json")) {
                return true;
            }

            const bool installedShaderHeader =
                extension == ".h" &&
                StartsWith(
                    path,
                    "enginecontent/shaders/");
            const bool runtimeAudioPayload = StartsWith(
                path,
                "content/runtimepayload/assets/audio/");
            if (extension == ".sln" ||
                extension == ".vcxproj" ||
                extension == ".filters" ||
                extension == ".cmake" ||
                extension == ".cpp" ||
                extension == ".cxx" ||
                extension == ".c" ||
                (extension == ".h" &&
                 !installedShaderHeader) ||
                extension == ".hpp" ||
                extension == ".pdb" ||
                extension == ".ilk" ||
                extension == ".exp" ||
                extension == ".lib" ||
                extension == ".obj" ||
                extension == ".fbx" ||
                extension == ".gltf" ||
                extension == ".glb" ||
                extension == ".obj" ||
                extension == ".bin" ||
                ((extension == ".wav" ||
                  extension == ".mp3" ||
                  extension == ".ogg" ||
                  extension == ".flac" ||
                  extension == ".aac" ||
                  extension == ".m4a" ||
                  extension == ".wma" ||
                  extension == ".aif" ||
                  extension == ".aiff") &&
                 !runtimeAudioPayload) ||
                extension == ".tga" ||
                extension == ".psd" ||
                extension == ".blend") {
                return true;
            }

            if ((extension == ".png" ||
                 extension == ".jpg" ||
                 extension == ".jpeg") &&
                !StartsWith(
                    path,
                    "content/runtimepayload/spine/")) {
                return true;
            }
            return false;
        }

        bool RequireFile(
            const std::filesystem::path& packageRoot,
            const std::filesystem::path& relativePath,
            std::string& outMessage) {

            std::error_code ec{};
            if (!std::filesystem::is_regular_file(
                    packageRoot / relativePath,
                    ec) ||
                ec) {
                outMessage =
                    "Game package is missing required file: " +
                    relativePath.generic_string();
                return false;
            }
            return true;
        }

    } // namespace

    bool ValidateGamePackageBoundary(
        const std::filesystem::path& packageRoot,
        const GamePackageManifestOptions& options,
        std::string& outMessage) {

        std::error_code ec{};
        if (!std::filesystem::is_directory(
                packageRoot,
                ec) ||
            ec) {
            outMessage =
                "Game package root is missing: " +
                packageRoot.generic_string();
            return false;
        }

        const std::filesystem::path executableFileName =
            TEXT::PathFromUtf8(
                options.executableFileName.empty()
                    ? "HIKARI_Game.exe"
                    : options.executableFileName);
        for (const std::filesystem::path& required :
             {
                 executableFileName,
                 std::filesystem::path(
                     "HIKARI_GameProject.dll"),
                 std::filesystem::path("dxcompiler.dll"),
                 std::filesystem::path("dxil.dll"),
                 std::filesystem::path(
                     "HIKARI/runtime_config.json"),
                 std::filesystem::path(
                     "EngineContent/ShaderBytecode/Shaders/"
                     "Render3D_StaticPS.hlsl/main_ps_6_0.dxil"),
                 std::filesystem::path("Content") /
                     kProjectDescriptorFilename,
                 std::filesystem::path("Content") /
                     ASSETS::COOKING::
                         kCookedContentManifestFilename,
             }) {
            if (!RequireFile(
                    packageRoot,
                    required,
                    outMessage)) {
                return false;
            }
        }

        if (!std::filesystem::is_directory(
                packageRoot / "EngineContent",
                ec) ||
            ec) {
            outMessage =
                "Game package is missing installed EngineContent.";
            return false;
        }

        std::filesystem::recursive_directory_iterator iterator(
            packageRoot,
            std::filesystem::directory_options::
                skip_permission_denied,
            ec);
        const std::filesystem::recursive_directory_iterator end{};
        for (; !ec && iterator != end; iterator.increment(ec)) {
            if (!iterator->is_regular_file(ec)) {
                ec.clear();
                continue;
            }

            const std::filesystem::path relative =
                std::filesystem::relative(
                    iterator->path(),
                    packageRoot,
                    ec);
            if (ec) {
                break;
            }
            if (IsForbiddenPackagePath(relative)) {
                outMessage =
                    "Game package contains forbidden authoring/build "
                    "content: " +
                    relative.generic_string();
                return false;
            }
        }

        if (ec) {
            outMessage =
                "Could not inspect game package boundary: " +
                ec.message();
            return false;
        }

        outMessage.clear();
        return true;
    }

    bool WriteGamePackageManifest(
        const std::filesystem::path& packageRoot,
        const GamePackageManifestOptions& options,
        std::string& outMessage) {

        std::vector<ManifestFile> files{};
        std::error_code ec{};
        std::filesystem::recursive_directory_iterator iterator(
            packageRoot,
            std::filesystem::directory_options::
                skip_permission_denied,
            ec);
        const std::filesystem::recursive_directory_iterator end{};
        for (; !ec && iterator != end; iterator.increment(ec)) {
            if (!iterator->is_regular_file(ec)) {
                ec.clear();
                continue;
            }

            const std::filesystem::path relative =
                std::filesystem::relative(
                    iterator->path(),
                    packageRoot,
                    ec);
            if (ec) {
                break;
            }
            if (relative.filename() ==
                kProductManifestFilename) {
                continue;
            }

            const uintmax_t size =
                iterator->file_size(ec);
            if (ec) {
                break;
            }
            std::string sha256{};
            if (!ComputeGamePackageFileSha256(
                    iterator->path(), sha256, &outMessage)) {
                return false;
            }
            files.push_back({
                TEXT::PathToUtf8(
                    relative.lexically_normal()),
                size,
                std::move(sha256),
            });
        }
        if (ec) {
            outMessage =
                "Could not inventory game package: " +
                ec.message();
            return false;
        }
        std::sort(
            files.begin(),
            files.end(),
            [](const ManifestFile& lhs, const ManifestFile& rhs) {
                return lhs.path < rhs.path;
            });

        nlohmann::json fileNodes =
            nlohmann::json::array();
        uintmax_t totalBytes = 0u;
        for (const ManifestFile& file : files) {
            fileNodes.push_back({
                { "path", file.path },
                { "size", file.size },
                { "sha256", file.sha256 },
            });
            totalBytes += file.size;
        }

        std::vector<std::string> exportedSceneGuids =
            options.exportedSceneGuids;
        std::sort(
            exportedSceneGuids.begin(),
            exportedSceneGuids.end());
        exportedSceneGuids.erase(
            std::unique(
                exportedSceneGuids.begin(),
                exportedSceneGuids.end()),
            exportedSceneGuids.end());

        const nlohmann::json root{
            {
                "formatVersion",
                kProductManifestFormatVersion
            },
            { "product", "HIKARI_Game" },
            {
                "executable",
                options.executableFileName.empty()
                    ? "HIKARI_Game.exe"
                    : options.executableFileName
            },
            { "configuration", "Shipping" },
            {
                "contentManifest",
                (
                    std::filesystem::path("Content") /
                    ASSETS::COOKING::
                        kCookedContentManifestFilename)
                    .generic_string()
            },
            {
                "projectModule",
                "HIKARI_GameProject.dll"
            },
            {
                "startupSceneGuid",
                options.startupSceneGuid
            },
            {
                "exportedSceneGuids",
                exportedSceneGuids
            },
            {
                "defaultWindowMode",
                options.defaultWindowMode
            },
            { "fileCount", files.size() },
            { "totalBytes", totalBytes },
            { "files", std::move(fileNodes) },
        };

        const std::filesystem::path manifestPath =
            packageRoot / kProductManifestFilename;
        std::ofstream stream(
            manifestPath,
            std::ios::binary | std::ios::trunc);
        if (!stream) {
            outMessage =
                "Could not write product manifest: " +
                manifestPath.generic_string();
            return false;
        }
        stream << root.dump(2) << '\n';
        if (!stream) {
            outMessage =
                "Product manifest write failed: " +
                manifestPath.generic_string();
            return false;
        }

        outMessage.clear();
        return true;
    }

} // namespace HIKARI::EDITOR
