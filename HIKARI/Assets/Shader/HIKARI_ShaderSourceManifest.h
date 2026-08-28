#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER {

    inline constexpr uint32_t kShaderSourceManifestVersion = 2u;
    inline constexpr std::string_view kShaderSourceManifestSchema =
        "HIKARI.ShaderSource";
    inline constexpr uint32_t kMaximumShaderEntryCount = 64u;
    inline constexpr uint32_t kMaximumShaderDefineCount = 128u;
    inline constexpr size_t kMaximumShaderIdentifierBytes = 128u;
    inline constexpr size_t kMaximumShaderPathBytes = 1024u;
    inline constexpr size_t kMaximumShaderDefineValueBytes = 256u;
    inline constexpr size_t kMaximumShaderManifestBytes = 1024u * 1024u;

    enum class ShaderStage : uint8_t {
        Vertex = 0u,
        Pixel,
        Compute,
        Amplification,
        Mesh,
        Count,
    };

    enum class ShaderIncludeRoot : uint8_t {
        Project = 0u,
        EngineSdk,
        Count,
    };

    namespace ShaderDeviceFeature {
        inline constexpr uint64_t WaveOperations = 1ull << 0u;
        inline constexpr uint64_t MeshShader = 1ull << 1u;
        inline constexpr uint64_t RayTracing = 1ull << 2u;
        inline constexpr uint64_t SamplerFeedback = 1ull << 3u;
        inline constexpr uint64_t All = WaveOperations |
            MeshShader | RayTracing | SamplerFeedback;
    }

    struct ShaderDefine {
        std::string name{};
        std::string value{};
    };

    struct ShaderEntryPoint {
        std::string id{};
        ShaderStage stage = ShaderStage::Pixel;
        std::string entry{};
        std::string profile{};
    };

    struct ShaderDeviceCaps {
        uint32_t minimumShaderModel = 60u;
        uint32_t minimumRootSignatureVersion = 10u;
        uint64_t requiredFeatureMask = 0u;
    };

    struct ShaderSourceManifest {
        uint32_t version = kShaderSourceManifestVersion;
        std::string programId{};
        std::filesystem::path sourcePath{};
        std::vector<ShaderEntryPoint> entries{};
        // Stage 1 permutations are program-wide; entry-specific define sets
        // require a later explicit schema version.
        std::vector<ShaderDefine> defines{};
        // Includes are literal quoted/angle .hlsl, .hlsli, or .h paths. Macro
        // operands are intentionally unsupported, and every textual branch
        // must resolve during the bounded preflight plan.
        std::vector<ShaderIncludeRoot> includeRoots{};
        std::string engineInterfaceId{};
        uint32_t engineInterfaceVersion = 0u;
        HASH::Sha256Digest engineInterfaceHash{};
        std::string rootLayoutId{};
        uint32_t rootLayoutVersion = 0u;
        HASH::Sha256Digest rootLayoutHash{};
        ShaderDeviceCaps requiredCaps{};
    };

    const char* ToString(ShaderStage stage) noexcept;
    bool ParseShaderStage(
        std::string_view text,
        ShaderStage& outStage) noexcept;
    const char* ToString(ShaderIncludeRoot root) noexcept;
    bool ParseShaderIncludeRoot(
        std::string_view text,
        ShaderIncludeRoot& outRoot) noexcept;

    bool ValidateShaderSourceManifest(
        const ShaderSourceManifest& manifest,
        std::string& outError);
    bool ParseShaderSourceManifest(
        std::string_view jsonText,
        ShaderSourceManifest& outManifest,
        std::string& outError);
    bool LoadShaderSourceManifest(
        const std::filesystem::path& path,
        ShaderSourceManifest& outManifest,
        std::string& outError);
    bool SerializeCanonicalShaderSourceManifest(
        const ShaderSourceManifest& manifest,
        std::string& outJson,
        std::string& outError);
    // Hashes only the canonical .hshader manifest. ShaderCooker later combines
    // this digest with bounded main-source/include byte hashes to form the
    // authoritative sourceClosureHash.
    bool ComputeShaderSourceManifestHash(
        const ShaderSourceManifest& manifest,
        HASH::Sha256Digest& outHash,
        std::string& outError);

} // namespace HIKARI::ASSETS::SHADER
