#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Shader/HIKARI_ShaderSourceManifest.h"

namespace HIKARI::ASSETS::SHADER {

    inline constexpr uint32_t kBuiltinShaderCatalogVersion = 1u;
    inline constexpr std::string_view kBuiltinShaderCatalogSchema =
        "HIKARI.BuiltinShaderCatalog";
    inline constexpr std::string_view kBuiltinShaderCatalogFilename =
        "BuiltinShaderCatalog.json";
    inline constexpr uint32_t kMaximumBuiltinShaderProgramCount = 128u;
    inline constexpr uint32_t kMaximumBuiltinShaderExclusionCount = 64u;
    inline constexpr size_t kMaximumBuiltinShaderCatalogBytes =
        1024u * 1024u;

    enum class BuiltinShaderProgramState : uint8_t {
        Pending = 0u,
        Cooked,
        Count,
    };

    enum class BuiltinShaderScope : uint8_t {
        Runtime = 0u,
        ToolOnly,
        Count,
    };

    enum class BuiltinShaderExclusionReason : uint8_t {
        IncludeOnly = 0u,
        Unreferenced,
        LegacyToolOnly,
        Count,
    };

    struct BuiltinShaderCookContract {
        std::string engineInterfaceId{};
        uint32_t engineInterfaceVersion = 0u;
        HASH::Sha256Digest engineInterfaceHash{};
        std::string rootLayoutId{};
        uint32_t rootLayoutVersion = 0u;
        HASH::Sha256Digest rootLayoutHash{};
        ShaderDeviceCaps requiredCaps{ 0u, 0u, 0u };
    };

    struct BuiltinShaderProgramDefinition {
        AssetGuid assetGuid{};
        std::string programId{};
        std::filesystem::path sourcePath{};
        std::string owner{};
        std::string group{};
        BuiltinShaderScope scope = BuiltinShaderScope::Runtime;
        BuiltinShaderProgramState state =
            BuiltinShaderProgramState::Pending;
        std::vector<ShaderEntryPoint> entries{};
        std::vector<ShaderDefine> defines{};
        std::vector<ShaderIncludeRoot> includeRoots{};
        BuiltinShaderCookContract contract{};
    };

    struct BuiltinShaderSourceExclusion {
        std::filesystem::path sourcePath{};
        BuiltinShaderExclusionReason reason =
            BuiltinShaderExclusionReason::Unreferenced;
    };

    struct BuiltinShaderCatalog {
        uint32_t version = kBuiltinShaderCatalogVersion;
        std::vector<BuiltinShaderProgramDefinition> programs{};
        std::vector<BuiltinShaderSourceExclusion> excludedSources{};
    };

    const char* ToString(BuiltinShaderProgramState state) noexcept;
    bool ParseBuiltinShaderProgramState(
        std::string_view text,
        BuiltinShaderProgramState& outState) noexcept;
    const char* ToString(BuiltinShaderScope scope) noexcept;
    bool ParseBuiltinShaderScope(
        std::string_view text,
        BuiltinShaderScope& outScope) noexcept;
    const char* ToString(BuiltinShaderExclusionReason reason) noexcept;
    bool ParseBuiltinShaderExclusionReason(
        std::string_view text,
        BuiltinShaderExclusionReason& outReason) noexcept;

    bool ValidateBuiltinShaderCatalog(
        const BuiltinShaderCatalog& catalog,
        std::string& outError);
    bool ParseBuiltinShaderCatalog(
        std::string_view jsonText,
        BuiltinShaderCatalog& outCatalog,
        std::string& outError);
    bool LoadBuiltinShaderCatalog(
        const std::filesystem::path& path,
        BuiltinShaderCatalog& outCatalog,
        std::string& outError);
    bool SerializeCanonicalBuiltinShaderCatalog(
        const BuiltinShaderCatalog& catalog,
        std::string& outJson,
        std::string& outError);
    bool ComputeBuiltinShaderCatalogHash(
        const BuiltinShaderCatalog& catalog,
        HASH::Sha256Digest& outHash,
        std::string& outError);
    bool BuildBuiltinShaderSourceManifest(
        const BuiltinShaderProgramDefinition& program,
        ShaderSourceManifest& outManifest,
        std::string& outError);

} // namespace HIKARI::ASSETS::SHADER
