#pragma once

#include <filesystem>
#include <mutex>
#include <string>

#include <Windows.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::GFX::INTERNAL {

    enum class DxcRuntimePurpose {
        RuntimeSourceFallback,
        Cook,
    };

    struct DxcRuntimeSnapshot {
        Microsoft::WRL::ComPtr<IDxcUtils> utils{};
        Microsoft::WRL::ComPtr<IDxcCompiler3> compiler{};
        Microsoft::WRL::ComPtr<IDxcValidator> validator{};
        Microsoft::WRL::ComPtr<IDxcIncludeHandler>
            defaultIncludeHandler{};
        std::filesystem::path compilerPath{};
        std::filesystem::path validatorPath{};
        std::string compilerVersion{};
        HASH::Sha256Digest compilerBinaryHash{};
        HASH::Sha256Digest validatorBinaryHash{};
        bool cookContractVerified = false;
    };

    bool AcquireDxcRuntime(
        DxcRuntimePurpose purpose,
        DxcRuntimeSnapshot& outRuntime,
        std::string& outError) noexcept;

    std::mutex& GetDxcInvocationMutex() noexcept;

} // namespace HIKARI::GFX::INTERNAL
