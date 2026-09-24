#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Shader/HIKARI_ShaderSourceManifest.h"
#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER {

inline constexpr uint32_t kShaderCookedArtifactVersion = 2u;
inline constexpr uint32_t kShaderCompilerContractVersion = 1u;
inline constexpr uint32_t kMaximumShaderDependencyCount = 512u;
inline constexpr uint32_t kMaximumShaderArgumentCount = 160u;
inline constexpr uint32_t kMaximumShaderBindingsPerEntry = 512u;
inline constexpr size_t kMaximumShaderArgumentBytes = 8u * 1024u;
inline constexpr uint64_t kMaximumShaderDependencyBytes = 16ull * 1024ull * 1024ull;
inline constexpr uint64_t kMaximumShaderSourceClosureBytes = 64ull * 1024ull * 1024ull;
inline constexpr size_t kMaximumShaderDxilBytesPerEntry = 16u * 1024u * 1024u;
inline constexpr size_t kMaximumShaderArtifactBytes = 64u * 1024u * 1024u;

enum class ShaderResourceBindingKind : uint8_t {
    ConstantBuffer = 0u,
    ShaderResource,
    UnorderedAccess,
    Sampler,
    Count,
};

struct ShaderSourceDependency {
    std::string logicalPath{};
    uint64_t byteSize = 0u;
    HASH::Sha256Digest contentHash{};
};

struct ShaderCompilerIdentity {
    std::string id{};
    std::string version{};
    uint32_t contractVersion = 0u;
    HASH::Sha256Digest binaryHash{};
    HASH::Sha256Digest validatorBinaryHash{};
};

struct ShaderResourceBinding {
    ShaderResourceBindingKind kind = ShaderResourceBindingKind::ShaderResource;
    uint32_t registerIndex = 0u;
    uint32_t registerSpace = 0u;
    uint32_t count = 1u;
    uint32_t visibilityMask = 0u;
};

struct ShaderCookedEntry {
    ShaderEntryPoint entry{};
    std::vector<std::string> compilerArguments{};
    HASH::Sha256Digest compilerArgumentsHash{};
    std::vector<ShaderResourceBinding> bindings{};
    HASH::Sha256Digest reflectionHash{};
    std::vector<uint8_t> dxil{};
    HASH::Sha256Digest dxilHash{};
};

struct ShaderCookedArtifact {
    uint32_t version = kShaderCookedArtifactVersion;
    AssetGuid sourceGuid{};
    std::string programId{};
    HASH::Sha256Digest manifestHash{};
    HASH::Sha256Digest sourceClosureHash{};
    std::vector<ShaderSourceDependency> dependencies{};
    ShaderCompilerIdentity compiler{};
    std::string engineInterfaceId{};
    uint32_t engineInterfaceVersion = 0u;
    HASH::Sha256Digest engineInterfaceHash{};
    std::string rootLayoutId{};
    uint32_t rootLayoutVersion = 0u;
    HASH::Sha256Digest rootLayoutHash{};
    ShaderDeviceCaps requiredCaps{};
    std::vector<ShaderCookedEntry> entries{};
};

bool ComputeShaderArgumentsHash(const std::vector<std::string>& arguments, HASH::Sha256Digest& outHash) noexcept;
bool ComputeShaderReflectionHash(const std::vector<ShaderResourceBinding>& bindings,
                                 HASH::Sha256Digest& outHash) noexcept;
bool ComputeShaderSourceClosureHash(const HASH::Sha256Digest& manifestHash,
                                    const std::vector<ShaderSourceDependency>& dependencies,
                                    HASH::Sha256Digest& outHash) noexcept;
bool ValidateShaderCookedArtifact(const ShaderCookedArtifact& artifact, std::string& outError);

} // namespace HIKARI::ASSETS::SHADER
