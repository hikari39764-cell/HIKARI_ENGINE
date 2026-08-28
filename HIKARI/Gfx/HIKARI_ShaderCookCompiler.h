#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Core/Hashing/HIKARI_Sha256.h"
#include "Gfx/HIKARI_ShaderCompiler.h"

namespace HIKARI::GFX {

    inline constexpr uint32_t kShaderCookCompilerContractVersion = 1u;
    inline constexpr uint32_t kMaximumShaderCookEntries = 64u;
    inline constexpr uint32_t kMaximumShaderCookDefines = 128u;
    inline constexpr uint32_t kMaximumShaderCookArguments = 160u;
    inline constexpr uint32_t kMaximumShaderCookDependencies = 512u;
    inline constexpr uint32_t kMaximumShaderCookBindings = 512u;
    inline constexpr size_t kMaximumShaderCookPathBytes = 1024u;
    inline constexpr uint64_t kMaximumShaderCookSourceFileBytes =
        16ull * 1024ull * 1024ull;
    inline constexpr uint64_t kMaximumShaderCookClosureBytes =
        64ull * 1024ull * 1024ull;
    inline constexpr size_t kMaximumShaderCookDxilBytes =
        16u * 1024u * 1024u;
    inline constexpr size_t kMaximumShaderCookArtifactBytes =
        64u * 1024u * 1024u;

    enum class ShaderCookIncludeRoot : uint8_t {
        Project = 0u,
        EngineSdk,
        Count,
    };

    enum class ShaderCookBindingKind : uint8_t {
        ConstantBuffer = 0u,
        ShaderResource,
        UnorderedAccess,
        Sampler,
        Count,
    };

    namespace ShaderCookDeviceFeature {
        inline constexpr uint64_t WaveOperations = 1ull << 0u;
        inline constexpr uint64_t MeshShader = 1ull << 1u;
        inline constexpr uint64_t RayTracing = 1ull << 2u;
        inline constexpr uint64_t SamplerFeedback = 1ull << 3u;
        inline constexpr uint64_t All = WaveOperations | MeshShader |
            RayTracing | SamplerFeedback;
    }

    struct ShaderCookDefine {
        std::string name{};
        std::string value{};
    };

    struct ShaderCookEntryRequest {
        std::string id{};
        ShaderStage stage = ShaderStage::Pixel;
        std::string entryPoint{};
        std::string profile{};
    };

    struct ShaderCookContractIdentity {
        std::string id{};
        uint32_t version = 0u;
        HASH::Sha256Digest hash{};
    };

    struct ShaderCookRequiredCaps {
        uint32_t minimumShaderModel = 0u;
        uint32_t minimumRootSignatureVersion = 0u;
        uint64_t requiredFeatureMask = 0u;
    };

    struct ShaderCookRequest {
        std::filesystem::path projectShaderRoot{};
        std::filesystem::path engineSdkIncludeRoot{};
        std::filesystem::path sourcePath{};
        std::vector<ShaderCookEntryRequest> entries{};
        std::vector<ShaderCookDefine> defines{};
        std::vector<ShaderCookIncludeRoot> includeRoots{};
        ShaderCookContractIdentity engineInterface{};
        ShaderCookContractIdentity rootLayout{};
        ShaderCookRequiredCaps requiredCaps{};
    };

    struct ShaderCookDependency {
        std::string logicalPath{};
        uint64_t byteSize = 0u;
        HASH::Sha256Digest contentHash{};
    };

    struct ShaderCookBinding {
        ShaderCookBindingKind kind =
            ShaderCookBindingKind::ShaderResource;
        uint32_t registerIndex = 0u;
        uint32_t registerSpace = 0u;
        uint32_t count = 1u;
        uint32_t visibilityMask = 0u;
    };

    struct ShaderCookToolchainIdentity {
        uint32_t contractVersion = 0u;
        std::string compilerId{};
        std::string compilerVersion{};
        HASH::Sha256Digest compilerBinaryHash{};
        HASH::Sha256Digest validatorBinaryHash{};
    };

    struct ShaderCookEntryOutput {
        std::string id{};
        // Logical source identity is carried by the program dependency
        // closure. Physical include roots are translated only at invocation.
        std::vector<std::string> canonicalArguments{};
        std::vector<ShaderCookBinding> bindings{};
        std::vector<uint8_t> dxil{};
    };

    struct ShaderCookOutput {
        bool success = false;
        ShaderCookToolchainIdentity toolchain{};
        ShaderCookContractIdentity engineInterface{};
        ShaderCookContractIdentity rootLayout{};
        ShaderCookRequiredCaps requiredCaps{};
        std::vector<ShaderCookDependency> dependencies{};
        std::vector<ShaderCookEntryOutput> entries{};
        std::string diagnostics{};
    };

    const char* ToString(ShaderCookIncludeRoot root) noexcept;

    bool BuildCanonicalShaderCookArguments(
        const ShaderCookRequest& request,
        const ShaderCookEntryRequest& entry,
        std::vector<std::string>& outArguments,
        std::string& outError) noexcept;

    bool CompileShaderProgramForCook(
        const ShaderCookRequest& request,
        ShaderCookOutput& outOutput) noexcept;

    bool VerifyShaderCookDependencies(
        const ShaderCookRequest& request,
        const std::vector<ShaderCookDependency>& dependencies,
        std::string& outError) noexcept;

} // namespace HIKARI::GFX
