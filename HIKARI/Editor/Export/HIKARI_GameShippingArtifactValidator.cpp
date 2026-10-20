#include "Editor/Export/HIKARI_GameShippingArtifactValidator.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Export/HIKARI_GameExportIdentity.h"
#include "Editor/Export/HIKARI_GamePackageHash.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Platform/Paths/HIKARI_ExecutablePath.h"
#include "Project/HIKARI_ProjectDescriptor.h"

namespace HIKARI::EDITOR {
namespace {

bool IsProjectFingerprintSource(const std::filesystem::path& path) {
    const std::string extension = TEXT::ToLowerAsciiCopy(path.extension().string());
    return extension == ".c" || extension == ".cc" || extension == ".cpp" || extension == ".cxx" || extension == ".h" ||
           extension == ".hh" || extension == ".hpp" || extension == ".hxx" || extension == ".inl";
}

void AddFingerprintTree(const std::filesystem::path& root, bool sourceExtensionsOnly,
                        std::vector<std::filesystem::path>& files) {
    std::error_code ec{};
    if (!std::filesystem::is_directory(root, ec) || ec) {
        return;
    }
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end{};
    for (; !ec && iterator != end; iterator.increment(ec)) {
        if (!iterator->is_regular_file(ec)) {
            ec.clear();
            continue;
        }
        if (!sourceExtensionsOnly || IsProjectFingerprintSource(iterator->path())) {
            files.push_back(iterator->path());
        }
    }
}

bool ComputeFileSetFingerprint(const std::filesystem::path& basePath, std::vector<std::filesystem::path> files,
                               std::string& outFingerprint, std::string& outMessage) {
    struct Entry final {
        std::filesystem::path path{};
        std::string relative{};
        std::string sortKey{};
    };
    std::vector<Entry> entries{};
    std::set<std::string> admitted{};
    std::error_code ec{};
    for (const std::filesystem::path& file : files) {
        ec.clear();
        if (!std::filesystem::is_regular_file(file, ec) || ec) {
            continue;
        }
        const std::filesystem::path relative = std::filesystem::relative(file, basePath, ec);
        if (ec || relative.empty() || relative.is_absolute() || relative.generic_string().starts_with("../")) {
            outMessage = "Fingerprint input escaped its owner root: " + file.string();
            return false;
        }
        const std::string relativeText = relative.lexically_normal().generic_string();
        const std::string key = TEXT::ToLowerAsciiCopy(relativeText);
        if (admitted.insert(key).second) {
            entries.push_back({file, relativeText, key});
        }
    }
    if (entries.empty()) {
        outMessage = "No fingerprint inputs were found under " + basePath.string();
        return false;
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry& lhs, const Entry& rhs) { return lhs.sortKey < rhs.sortKey; });
    std::string fingerprintSource{};
    for (const Entry& entry : entries) {
        std::string fileHash{};
        if (!ComputeGamePackageFileSha256(entry.path, fileHash, &outMessage)) {
            return false;
        }
        std::transform(fileHash.begin(), fileHash.end(), fileHash.begin(),
                       [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
        fingerprintSource.append(entry.relative);
        fingerprintSource.push_back('|');
        fingerprintSource.append(fileHash);
        fingerprintSource.push_back('\n');
    }
    return ComputeGamePackageSha256(fingerprintSource, outFingerprint, &outMessage);
}

bool ComputeProjectFingerprint(const std::filesystem::path& projectRoot, std::string& outFingerprint,
                               std::string& outMessage) {
    const ProjectContext& context = GetProjectContext();
    const std::filesystem::path manifest = context.IsOpen()
                                               ? context.Resolve(context.GetDescriptor().gameProjectManifest)
                                               : projectRoot / "CMakeLists.txt";
    const std::filesystem::path manifestRoot = manifest.parent_path();
    std::vector<std::filesystem::path> files{
        projectRoot / kProjectDescriptorFilename,
        manifest,
    };
    AddFingerprintTree(manifestRoot / "Source", true, files);
    AddFingerprintTree(manifestRoot / "cmake", false, files);
    return ComputeFileSetFingerprint(projectRoot, std::move(files), outFingerprint, outMessage);
}

bool ComputeSdkFingerprint(const std::filesystem::path& sdkRoot, std::string& outFingerprint, std::string& outMessage) {
    std::vector<std::filesystem::path> files{
        sdkRoot / "lib" / "HIKARI_Engine.lib",
        sdkRoot / "Tools" / "Build-HikariProject.ps1",
    };
    AddFingerprintTree(sdkRoot / "include", false, files);
    AddFingerprintTree(sdkRoot / "lib" / "cmake" / "HIKARI", false, files);
    AddFingerprintTree(sdkRoot / "src", false, files);
    return ComputeFileSetFingerprint(sdkRoot, std::move(files), outFingerprint, outMessage);
}

std::filesystem::path ResolveEngineSourceRoot() {
    std::filesystem::path candidate = PLATFORM::GetExecutableDirectory();
    std::error_code ec{};
    if (candidate.is_relative()) {
        candidate = std::filesystem::absolute(candidate, ec);
    }
    for (uint32_t depth = 0u; depth < 8u && !candidate.empty(); ++depth) {
        ec.clear();
        if (std::filesystem::is_regular_file(candidate / "CMakeLists.txt", ec) && !ec &&
            std::filesystem::is_directory(candidate / "HIKARI", ec) && !ec) {
            return candidate.lexically_normal();
        }
        candidate = candidate.parent_path();
    }
    return {};
}

bool ShippingEngineSourcesAreCurrent(const std::filesystem::path& sourceExe, std::string& outMessage) {
    const std::filesystem::path sourceRoot = ResolveEngineSourceRoot();
    if (sourceRoot.empty()) {
        // Installed Editor packages may not retain source. Project
        // and SDK fingerprints still prove their binary pairing.
        return true;
    }
    // Source exports run the canonical incremental build before reaching
    // validation. Compare products, not source timestamps (shader/content
    // updates need not relink the executable).
    const auto builtExe = sourceRoot / "out/build/game-shipping/bin" / sourceExe.filename();
    std::string installedHash, builtHash;
    if (!ComputeGamePackageFileSha256(sourceExe, installedHash, &outMessage) ||
        !ComputeGamePackageFileSha256(builtExe, builtHash, &outMessage))
        return false;
    if (installedHash != builtHash) {
        outMessage = "Shipping runtime differs from the prepared source build. Run the export preparation again.";
        return false;
    }
    return true;
}

bool ValidateShippingArtifactsInternal(const std::filesystem::path& projectRoot, const std::filesystem::path& sourceExe,
                                       const std::filesystem::path& sourceProjectModule, std::string& outMessage) {
    const std::filesystem::path statePath =
        projectRoot / "Library" / "Build" / "game-shipping" / "HIKARI_GameProject.buildstate.json";
    try {
        std::ifstream stateStream(statePath, std::ios::binary);
        nlohmann::json state{};
        if (!stateStream || !(stateStream >> state) || !state.is_object() ||
            (state.value("schemaVersion", 0) != 1 && state.value("schemaVersion", 0) != 2) ||
            (state.value("schemaVersion", 0) == 2 && state.value("editorModulePresent", true)) ||
            state.value("preset", std::string{}) != "game-shipping" ||
            state.value("configuration", std::string{}) != "Shipping") {
            outMessage = "GameShipping build state is missing or invalid. "
                         "Run .\\Tools\\Build\\Build-Hikari.ps1 "
                         "-Preset game-shipping -Project \"" +
                         projectRoot.string() + "\" -Install.";
            return false;
        }

        std::string currentProjectFingerprint{};
        if (!ComputeProjectFingerprint(projectRoot, currentProjectFingerprint, outMessage)) {
            return false;
        }
        if (TEXT::ToLowerAsciiCopy(state.value("projectFingerprint", std::string{})) !=
            TEXT::ToLowerAsciiCopy(currentProjectFingerprint)) {
            outMessage = "GameShipping project module is stale relative to "
                         "the current project source. Rebuild GameShipping "
                         "before exporting.";
            return false;
        }

        const std::filesystem::path sdkRoot = sourceExe.parent_path() / "Sdk";
        std::string currentSdkFingerprint{};
        if (!ComputeSdkFingerprint(sdkRoot, currentSdkFingerprint, outMessage)) {
            return false;
        }
        if (TEXT::ToLowerAsciiCopy(state.value("sdkFingerprint", std::string{})) !=
            TEXT::ToLowerAsciiCopy(currentSdkFingerprint)) {
            outMessage = "GameShipping project module and installed SDK do "
                         "not match. Rebuild and install the complete "
                         "GameShipping product.";
            return false;
        }

        std::string moduleHash{};
        if (!ComputeGamePackageFileSha256(sourceProjectModule, moduleHash, &outMessage)) {
            return false;
        }
        if (TEXT::ToLowerAsciiCopy(state.value("moduleSha256", std::string{})) != TEXT::ToLowerAsciiCopy(moduleHash)) {
            outMessage = "Installed HIKARI_GameProject.dll does not match "
                         "its last-good GameShipping build state.";
            return false;
        }

        if (!ShippingEngineSourcesAreCurrent(sourceExe, outMessage)) {
            return false;
        }

        HMODULE module = LoadLibraryExW(sourceProjectModule.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
        if (module == nullptr) {
            outMessage = "Could not inspect HIKARI_GameProject.dll export "
                         "surface.";
            return false;
        }
        const bool queryPresent = GetProcAddress(module, "HIKARI_GetGameProjectModuleApi") != nullptr;
        FreeLibrary(module);
        if (!queryPresent) {
            outMessage = "HIKARI_GameProject.dll is missing the canonical "
                         "module API entry point.";
            return false;
        }
        outMessage.clear();
        return true;
    } catch (const std::exception& error) {
        outMessage = "GameShipping freshness validation failed: " + std::string(error.what());
        return false;
    }
}

} // namespace

std::filesystem::path ResolveExportEngineSourceRoot() { return ResolveEngineSourceRoot(); }

bool ResolveGameShippingHostExecutable(const std::filesystem::path& runtimeRoot, std::filesystem::path& executable,
                                       std::string& message) {
    try {
        std::ifstream stream(runtimeRoot / "Sdk/lib/cmake/HIKARI/HIKARI_GameHost.json");
        auto manifest = nlohmann::json::parse(stream, nullptr, false);
        if (!manifest.is_object() || manifest.value("schemaVersion", 0) != 1 ||
            manifest.value("sdkBinding", std::string{}) != "currentProcess") {
            message = "Shipping SDK を更新してください。EXE 名を変更できるランタイムが必要です。";
            return false;
        }
        std::filesystem::path filename;
        if (!ResolveExportExecutableFileName(manifest.at("executable").get<std::string>(), filename, message))
            return false;
        executable = runtimeRoot / filename;
        return true;
    } catch (const std::exception& error) {
        message = error.what();
        return false;
    }
}

bool ValidateGameShippingArtifacts(const std::filesystem::path& projectRoot, const std::filesystem::path& sourceExe,
                                   const std::filesystem::path& sourceProjectModule, std::string& outMessage) {
    return ValidateShippingArtifactsInternal(projectRoot, sourceExe, sourceProjectModule, outMessage);
}

} // namespace HIKARI::EDITOR
