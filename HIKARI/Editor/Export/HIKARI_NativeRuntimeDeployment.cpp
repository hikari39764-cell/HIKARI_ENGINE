#include "Editor/Export/HIKARI_NativeRuntimeDeployment.h"

#include <algorithm>
#include <cctype>
#include <system_error>

#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::EDITOR {

    namespace {
        bool IsFeatureOwnedRuntime(const std::filesystem::path& path) {
            const std::string name =
                TEXT::ToLowerAsciiCopy(path.filename().string());
            return name.rfind("sl.", 0) == 0 ||
                name == "nvngx_dlss.dll" ||
                name == "nvngx_dlssg.dll" ||
                name == "winpixeventruntime.dll";
        }

        bool IsAssimpRuntime(const std::filesystem::path& path) {
            const std::string name =
                TEXT::ToLowerAsciiCopy(path.filename().string());
            return name.rfind("assimp-", 0) == 0 &&
                TEXT::ToLowerAsciiCopy(path.extension().string()) == ".dll";
        }
    }

    NativeRuntimeDeploymentResult DeployNativeRuntimeDependencies(
        const std::filesystem::path& sourceDirectory,
        const std::filesystem::path& outputDirectory) {
        NativeRuntimeDeploymentResult result{};
        std::error_code ec{};
        if (!std::filesystem::is_directory(sourceDirectory, ec) || ec) {
            result.message =
                "Runtime dependency source directory is missing: " +
                sourceDirectory.string();
            return result;
        }

        std::filesystem::create_directories(outputDirectory, ec);
        if (ec) {
            result.message =
                "Could not create runtime dependency output directory: " +
                outputDirectory.string();
            return result;
        }

        bool assimpRuntimeFound = false;
        for (std::filesystem::directory_iterator it(sourceDirectory, ec), end;
             !ec && it != end;
             it.increment(ec)) {
            if (!it->is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }

            const std::filesystem::path source = it->path();
            if (TEXT::ToLowerAsciiCopy(source.extension().string()) != ".dll" ||
                IsFeatureOwnedRuntime(source)) {
                continue;
            }

            assimpRuntimeFound |= IsAssimpRuntime(source);
            const std::filesystem::path destination =
                outputDirectory / source.filename();
            std::filesystem::copy_file(
                source,
                destination,
                std::filesystem::copy_options::overwrite_existing,
                ec);
            if (ec) {
                result.message =
                    "Could not deploy native runtime dependency " +
                    source.string() + " to " + destination.string() +
                    ": " + ec.message();
                return result;
            }
            ++result.filesCopied;
        }

        if (ec) {
            result.message =
                "Could not enumerate runtime dependencies in " +
                sourceDirectory.string() + ": " + ec.message();
            return result;
        }
        if (!assimpRuntimeFound) {
            result.message =
                "Assimp runtime DLL is missing from build output: " +
                sourceDirectory.string();
            return result;
        }

        result.success = true;
        result.message =
            "Deployed " + std::to_string(result.filesCopied) +
            " native runtime DLL(s).";
        return result;
    }

} // namespace HIKARI::EDITOR
