#include "Gfx/Internal/HIKARI_DxcRuntime.h"

#include <Windows.h>
#include <winver.h>

#include <algorithm>
#include <exception>
#include <mutex>
#include <vector>

#include "Core/HIKARI_Logger.h"
#include "Gfx/Internal/HIKARI_DxcCookToolchainConfig.h"

namespace HIKARI::GFX::INTERNAL {
namespace {

struct DxcRuntimeState {
    bool attempted = false;
    bool ready = false;
    HMODULE dxilModule = nullptr;
    HMODULE compilerModule = nullptr;
    DxcCreateInstanceProc createInstance = nullptr;
    DxcRuntimeSnapshot snapshot{};
    std::string lastError{};
};

DxcRuntimeState gRuntimeFallback{};
DxcRuntimeState gCookRuntime{};
std::mutex gRuntimeMutex{};
std::mutex gInvocationMutex{};

std::filesystem::path CanonicalPath(const std::filesystem::path& path) {
    std::error_code ec{};
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? std::filesystem::absolute(path, ec) : canonical;
}

std::filesystem::path GetModulePath(HMODULE module) {
    if (module == nullptr) {
        return {};
    }
    std::wstring buffer(32768u, L'\0');
    const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0u || length >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return CanonicalPath(buffer);
}

std::filesystem::path GetHostDirectory() { return GetModulePath(GetModuleHandleW(nullptr)).parent_path(); }

void ReleaseLoadedModules(DxcRuntimeState& state) noexcept {
    if (state.compilerModule != nullptr) {
        FreeLibrary(state.compilerModule);
        state.compilerModule = nullptr;
    }
    if (state.dxilModule != nullptr) {
        FreeLibrary(state.dxilModule);
        state.dxilModule = nullptr;
    }
    state.snapshot.compilerPath.clear();
    state.snapshot.validatorPath.clear();
    state.snapshot.cookContractVerified = false;
}

bool TryLoadFromDirectory(DxcRuntimeState& state, const std::filesystem::path& directory, bool cookContractVerified) {
    std::error_code ec{};
    const std::filesystem::path compilerPath = directory / L"dxcompiler.dll";
    const std::filesystem::path validatorPath = directory / L"dxil.dll";
    if (!std::filesystem::is_regular_file(compilerPath, ec) || ec ||
        !std::filesystem::is_regular_file(validatorPath, ec) || ec) {
        return false;
    }

    state.dxilModule = LoadLibraryW(validatorPath.c_str());
    if (state.dxilModule == nullptr) {
        return false;
    }
    state.compilerModule = LoadLibraryW(compilerPath.c_str());
    if (state.compilerModule == nullptr) {
        ReleaseLoadedModules(state);
        return false;
    }
    state.snapshot.compilerPath = GetModulePath(state.compilerModule);
    state.snapshot.validatorPath = GetModulePath(state.dxilModule);
    state.snapshot.cookContractVerified = cookContractVerified;
    if (state.snapshot.compilerPath.empty() || state.snapshot.validatorPath.empty()) {
        ReleaseLoadedModules(state);
        return false;
    }
    return true;
}

bool TryLoadDefault(DxcRuntimeState& state) {
    state.dxilModule = LoadLibraryW(L"dxil.dll");
    if (state.dxilModule == nullptr) {
        return false;
    }
    state.compilerModule = LoadLibraryW(L"dxcompiler.dll");
    if (state.compilerModule == nullptr) {
        ReleaseLoadedModules(state);
        return false;
    }
    state.snapshot.compilerPath = GetModulePath(state.compilerModule);
    state.snapshot.validatorPath = GetModulePath(state.dxilModule);
    state.snapshot.cookContractVerified = false;
    if (state.snapshot.compilerPath.empty() || state.snapshot.validatorPath.empty()) {
        ReleaseLoadedModules(state);
        return false;
    }
    return true;
}

bool TryLoadFromWindowsKits(DxcRuntimeState& state) {
    const std::filesystem::path kitsBin = L"C:\\Program Files (x86)\\Windows Kits\\10\\bin";
    std::error_code ec{};
    if (!std::filesystem::is_directory(kitsBin, ec) || ec) {
        return false;
    }

    std::vector<std::filesystem::path> candidates{};
    std::filesystem::directory_iterator iterator(kitsBin, ec);
    const std::filesystem::directory_iterator end{};
    for (; !ec && iterator != end; iterator.increment(ec)) {
        if (!iterator->is_directory(ec)) {
            ec.clear();
            continue;
        }
        const std::filesystem::path x64 = iterator->path() / L"x64";
        if (std::filesystem::is_regular_file(x64 / L"dxcompiler.dll", ec) && !ec &&
            std::filesystem::is_regular_file(x64 / L"dxil.dll", ec) && !ec) {
            candidates.push_back(x64);
        }
        ec.clear();
    }
    std::sort(candidates.begin(), candidates.end(), std::greater<std::filesystem::path>{});
    for (const std::filesystem::path& directory : candidates) {
        if (TryLoadFromDirectory(state, directory, false)) {
            return true;
        }
    }
    return false;
}

bool ReadFileVersion(const std::filesystem::path& path, std::string& outVersion) {
    DWORD ignored = 0u;
    const DWORD bytes = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (bytes == 0u) {
        return false;
    }
    std::vector<uint8_t> data(bytes);
    if (!GetFileVersionInfoW(path.c_str(), 0u, bytes, data.data())) {
        return false;
    }
    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoBytes = 0u;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &infoBytes) || info == nullptr ||
        infoBytes < sizeof(VS_FIXEDFILEINFO) || info->dwSignature != 0xfeef04bdu) {
        return false;
    }
    outVersion = std::to_string(HIWORD(info->dwFileVersionMS)) + "." + std::to_string(LOWORD(info->dwFileVersionMS)) +
                 "." + std::to_string(HIWORD(info->dwFileVersionLS)) + "." +
                 std::to_string(LOWORD(info->dwFileVersionLS));
    return true;
}

bool LoadConfiguredCookToolchain(DxcRuntimeState& state) {
    const std::filesystem::path root = GetHostDirectory();
    const std::filesystem::path compilerPath = root / L"dxcompiler.dll";
    const std::filesystem::path validatorPath = root / L"dxil.dll";
    HASH::Sha256Digest expectedCompiler{};
    HASH::Sha256Digest expectedValidator{};
    HASH::Sha256Digest actualCompiler{};
    HASH::Sha256Digest actualValidator{};
    std::string hashError{};
    if (root.empty() || !HASH::ParseSha256(CONFIG::kDxcCookCompilerSha256, expectedCompiler) ||
        !HASH::ParseSha256(CONFIG::kDxcCookValidatorSha256, expectedValidator) ||
        !HASH::ComputeFileSha256(compilerPath, actualCompiler, hashError) ||
        !HASH::ComputeFileSha256(validatorPath, actualValidator, hashError) || actualCompiler != expectedCompiler ||
        actualValidator != expectedValidator || !TryLoadFromDirectory(state, root, true)) {
        state.lastError = "configured DXC cook toolchain is missing or changed";
        return false;
    }
    std::error_code ec{};
    if (!std::filesystem::equivalent(compilerPath, state.snapshot.compilerPath, ec) || ec ||
        !std::filesystem::equivalent(validatorPath, state.snapshot.validatorPath, ec) || ec) {
        ReleaseLoadedModules(state);
        state.lastError = "loaded DXC modules do not match the configured files";
        return false;
    }
    return true;
}

bool InitializeRuntime(DxcRuntimeState& state, DxcRuntimePurpose purpose) {
    state.attempted = true;
    if (purpose == DxcRuntimePurpose::Cook) {
        if (!LoadConfiguredCookToolchain(state)) {
            return false;
        }
    } else {
        const std::filesystem::path hostDirectory = GetHostDirectory();
        if ((hostDirectory.empty() || !TryLoadFromDirectory(state, hostDirectory, false)) &&
#if defined(HIKARI_SHIPPING)
            true
#else
            !TryLoadDefault(state) && !TryLoadFromWindowsKits(state)
#endif
        ) {
            state.lastError = "dxcompiler.dll and dxil.dll were not found";
            return false;
        }
    }

    state.createInstance =
        reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(state.compilerModule, "DxcCreateInstance"));
    if (state.createInstance == nullptr) {
        state.lastError = "DxcCreateInstance is missing from dxcompiler.dll";
        return false;
    }

    HRESULT result = state.createInstance(CLSID_DxcUtils, IID_PPV_ARGS(state.snapshot.utils.GetAddressOf()));
    if (FAILED(result)) {
        state.lastError = "could not create IDxcUtils";
        return false;
    }
    result = state.createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(state.snapshot.compiler.GetAddressOf()));
    if (FAILED(result)) {
        state.lastError = "could not create IDxcCompiler3";
        return false;
    }
    if (purpose != DxcRuntimePurpose::Cook) {
        result = state.snapshot.utils->CreateDefaultIncludeHandler(state.snapshot.defaultIncludeHandler.GetAddressOf());
        if (FAILED(result)) {
            state.lastError = "could not create the default DXC include handler";
            return false;
        }
    }
    result = state.createInstance(CLSID_DxcValidator, IID_PPV_ARGS(state.snapshot.validator.GetAddressOf()));
    if (FAILED(result)) {
        state.snapshot.validator.Reset();
    }

    std::string hashError{};
    ReadFileVersion(state.snapshot.compilerPath, state.snapshot.compilerVersion);
    HASH::ComputeFileSha256(state.snapshot.compilerPath, state.snapshot.compilerBinaryHash, hashError);
    hashError.clear();
    HASH::ComputeFileSha256(state.snapshot.validatorPath, state.snapshot.validatorBinaryHash, hashError);

    if (purpose == DxcRuntimePurpose::Cook) {
        HASH::Sha256Digest expectedCompiler{};
        HASH::Sha256Digest expectedValidator{};
        if (!HASH::ParseSha256(CONFIG::kDxcCookCompilerSha256, expectedCompiler) ||
            !HASH::ParseSha256(CONFIG::kDxcCookValidatorSha256, expectedValidator) ||
            state.snapshot.compilerBinaryHash != expectedCompiler ||
            state.snapshot.validatorBinaryHash != expectedValidator) {
            state.lastError = "loaded DXC toolchain changed during acquisition";
            return false;
        }
    }

    state.ready = true;
    HIKARI_LOG_INFO("[ShaderCompiler] DXC runtime loaded: " + state.snapshot.compilerPath.generic_string());
    return true;
}

} // namespace

bool AcquireDxcRuntime(DxcRuntimePurpose purpose, DxcRuntimeSnapshot& outRuntime, std::string& outError) noexcept {
    try {
        std::scoped_lock lock(gRuntimeMutex);
        DxcRuntimeState& state = purpose == DxcRuntimePurpose::Cook ? gCookRuntime : gRuntimeFallback;
        if (!state.ready && !state.attempted && !InitializeRuntime(state, purpose)) {
            outError = state.lastError;
            return false;
        }
        if (!state.ready) {
            outError = state.lastError.empty() ? "DXC runtime is unavailable" : state.lastError;
            return false;
        }
        if (purpose == DxcRuntimePurpose::Cook &&
            (!state.snapshot.cookContractVerified || state.snapshot.validator == nullptr ||
             state.snapshot.compilerVersion.empty() || state.snapshot.compilerBinaryHash.IsZero() ||
             state.snapshot.validatorBinaryHash.IsZero())) {
            outError = "Shader cook requires its configured DXC compiler and "
                       "validator with complete provenance";
            return false;
        }
        outRuntime = state.snapshot;
        outError.clear();
        return true;
    } catch (const std::exception& error) {
        outError = std::string("DXC runtime acquisition failed: ") + error.what();
        return false;
    }
}

std::mutex& GetDxcInvocationMutex() noexcept { return gInvocationMutex; }

} // namespace HIKARI::GFX::INTERNAL
