#include "Editor/Export/HIKARI_NativeRuntimeDeployment.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>
#include <Windows.h>
#include <cstring>
#include <set>
#include "Core/Text/HIKARI_UtfPath.h"

#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::EDITOR {

bool ValidateNativeRuntimeDependencies(const std::filesystem::path& packageDirectory, std::string& message) {
    // Windows 10/11 の標準 DLL だけを OS 所有として許可する。
    // 開発 PC の System32/PATH にある VC runtime は依存解決に使わない。
    static const std::set<std::string> systemDlls{"advapi32.dll", "bcrypt.dll",         "bcryptprimitives.dll",
                                                  "cabinet.dll",  "cfgmgr32.dll",       "comctl32.dll",
                                                  "comdlg32.dll", "crypt32.dll",        "d3d11.dll",
                                                  "d3d12.dll",    "d3dcompiler_47.dll", "dbghelp.dll",
                                                  "dwmapi.dll",   "dxcore.dll",         "dxgi.dll",
                                                  "gdi32.dll",    "imm32.dll",          "iphlpapi.dll",
                                                  "kernel32.dll", "kernelbase.dll",     "msvcrt.dll",
                                                  "ntdll.dll",    "ole32.dll",          "oleaut32.dll",
                                                  "powrprof.dll", "propsys.dll",        "psapi.dll",
                                                  "rpcrt4.dll",   "secur32.dll",        "setupapi.dll",
                                                  "shell32.dll",  "shlwapi.dll",        "ucrtbase.dll",
                                                  "user32.dll",   "userenv.dll",        "uxtheme.dll",
                                                  "version.dll",  "winhttp.dll",        "wininet.dll",
                                                  "winmm.dll",    "wintrust.dll",       "ws2_32.dll",
                                                  "wtsapi32.dll", "xinput9_1_0.dll",    "xaudio2_9.dll"};
    for (const auto& file : std::filesystem::directory_iterator(packageDirectory)) {
        const auto extension = TEXT::ToLowerAsciiCopy(file.path().extension().string());
        if (!file.is_regular_file() || (extension != ".exe" && extension != ".dll"))
            continue;
        // データとしてマップし、DLL 初期化や依存 DLL の実行は行わない。
        const HMODULE image = LoadLibraryExW(file.path().c_str(), nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (!image) {
            message = "Invalid native runtime image: " + TEXT::PathToUtf8(file.path());
            return false;
        }
        struct ImageLease {
            HMODULE value;
            ~ImageLease() { FreeLibrary(value); }
        } lease{image};
        const auto* base = reinterpret_cast<const unsigned char*>(reinterpret_cast<uintptr_t>(image) & ~uintptr_t(3));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            message = "The game package requires x64 native libraries: " + TEXT::PathToUtf8(file.path());
            return false;
        }
        const auto imageSize = nt->OptionalHeader.SizeOfImage;
        const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!directory.VirtualAddress)
            continue;
        if (directory.VirtualAddress >= imageSize || directory.Size > imageSize - directory.VirtualAddress) {
            message = "Invalid native import directory: " + TEXT::PathToUtf8(file.path());
            return false;
        }
        bool terminated = false;
        for (size_t offset = 0; offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= directory.Size;
             offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
            const auto* entry =
                reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress + offset);
            if (!entry->Name) {
                terminated = true;
                break;
            }
            if (entry->Name >= imageSize || !std::memchr(base + entry->Name, 0, imageSize - entry->Name))
                break;
            const std::string dependency = TEXT::ToLowerAsciiCopy(reinterpret_cast<const char*>(base + entry->Name));
            if (dependency.find_first_of("/\\:") != std::string::npos)
                break;
            if (systemDlls.contains(dependency) || dependency.starts_with("api-ms-win-") ||
                dependency.starts_with("ext-ms-win-"))
                continue;
            if (!std::filesystem::is_regular_file(packageDirectory / dependency)) {
                message = "Game package dependency missing: " + dependency + " (required by " +
                          TEXT::PathToUtf8(file.path().filename()) + "). Rebuild/install the paired Shipping runtime.";
                return false;
            }
        }
        if (!terminated) {
            message = "Malformed native imports: " + TEXT::PathToUtf8(file.path());
            return false;
        }
    }
    message.clear();
    return true;
}

namespace {
bool IsFeatureOwnedRuntime(const std::filesystem::path& path) {
    const std::string name = TEXT::ToLowerAsciiCopy(path.filename().string());
    return name.rfind("sl.", 0) == 0 || name == "nvngx_dlss.dll" || name == "nvngx_dlssg.dll" ||
           name == "winpixeventruntime.dll";
}

bool IsGameProjectModule(const std::filesystem::path& path) {
    return TEXT::ToLowerAsciiCopy(path.filename().string()) == "hikari_gameproject.dll";
}

bool IsSourceImporterRuntime(const std::filesystem::path& path) {
    const std::string name = TEXT::ToLowerAsciiCopy(path.filename().string());
    return name.rfind("assimp", 0) == 0 || name.rfind("coacd", 0) == 0 || name.rfind("xatlas", 0) == 0 ||
           name.rfind("directxtex", 0) == 0;
}
} // namespace

NativeRuntimeDeploymentResult DeployNativeRuntimeDependencies(const std::filesystem::path& sourceDirectory,
                                                              const std::filesystem::path& outputDirectory) {
    NativeRuntimeDeploymentResult result{};
    std::error_code ec{};
    if (!std::filesystem::is_directory(sourceDirectory, ec) || ec) {
        result.message = "Runtime dependency source directory is missing: " + sourceDirectory.string();
        return result;
    }

    std::filesystem::create_directories(outputDirectory, ec);
    if (ec) {
        result.message = "Could not create runtime dependency output directory: " + outputDirectory.string();
        return result;
    }

    for (std::filesystem::directory_iterator it(sourceDirectory, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }

        const std::filesystem::path source = it->path();
        if (TEXT::ToLowerAsciiCopy(source.extension().string()) != ".dll" || IsFeatureOwnedRuntime(source) ||
            IsGameProjectModule(source) || IsSourceImporterRuntime(source)) {
            continue;
        }

        const std::filesystem::path destination = outputDirectory / source.filename();
        std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            result.message = "Could not deploy native runtime dependency " + source.string() + " to " +
                             destination.string() + ": " + ec.message();
            return result;
        }
        ++result.filesCopied;
    }

    if (ec) {
        result.message =
            "Could not enumerate runtime dependencies in " + sourceDirectory.string() + ": " + ec.message();
        return result;
    }
    result.success = true;
    result.message = "Deployed " + std::to_string(result.filesCopied) + " native runtime DLL(s).";
    return result;
}

} // namespace HIKARI::EDITOR
