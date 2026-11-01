#include "RenderContracts/Shader/Internal/HIKARI_ShaderIdentifier.h"
#include "Core/IO/HIKARI_PathNormalization.h"
#include "RenderContracts/Shader/HIKARI_BuiltinShaderCatalog.h"

#include <filesystem>
#include <unordered_set>
#include <utility>

#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::ASSETS::SHADER {
namespace {

bool IsSafeEngineShaderPath(const std::filesystem::path& path) {
    if (!PATHS::IsSafeRelativePath(path) || path.lexically_normal() != path ||
        TEXT::ToLowerAsciiCopy(path.extension().string()) != ".hlsl") {
        return false;
    }
    const std::string generic = path.generic_string();
    return !generic.empty() && generic.size() <= kMaximumShaderPathBytes && generic.find(':') == std::string::npos;
}

bool ContractIsEmpty(const BuiltinShaderCookContract& contract) noexcept {
    return contract.engineInterfaceId.empty() && contract.engineInterfaceVersion == 0u &&
           contract.engineInterfaceHash.IsZero() && contract.rootLayoutId.empty() && contract.rootLayoutVersion == 0u &&
           contract.rootLayoutHash.IsZero() && contract.requiredCaps.minimumShaderModel == 0u &&
           contract.requiredCaps.minimumRootSignatureVersion == 0u && contract.requiredCaps.requiredFeatureMask == 0u;
}

bool ValidateProgram(const BuiltinShaderProgramDefinition& program, std::string& outError) {
    if (!IsValidAssetGuid(program.assetGuid.value) ||
        TEXT::ToLowerAsciiCopy(program.assetGuid.value) != program.assetGuid.value ||
        !INTERNAL::IsIdentifier(program.programId, true) || !IsSafeEngineShaderPath(program.sourcePath) ||
        !INTERNAL::IsIdentifier(program.owner, true) ||
        (!program.group.empty() && !INTERNAL::IsIdentifier(program.group, true)) ||
        program.scope >= BuiltinShaderScope::Count || program.state >= BuiltinShaderProgramState::Count ||
        program.entries.empty() || program.entries.size() > kMaximumShaderEntryCount ||
        program.defines.size() > kMaximumShaderDefineCount || program.includeRoots.size() != 1u ||
        program.includeRoots[0] != ShaderIncludeRoot::EngineSdk) {
        outError = "built-in shader program identity is invalid";
        return false;
    }

    std::string previousEntry{};
    std::unordered_set<std::string> symbols{};
    for (const ShaderEntryPoint& entry : program.entries) {
        uint32_t shaderModel = 0;
        if (!INTERNAL::IsIdentifier(entry.id, true) || !INTERNAL::IsIdentifier(entry.entry, false) ||
            entry.stage >= ShaderStage::Count || !ValidateShaderEntryProfile(entry.stage, entry.profile, shaderModel) ||
            (program.state == BuiltinShaderProgramState::Cooked &&
             program.contract.requiredCaps.minimumShaderModel < shaderModel) ||
            (!previousEntry.empty() && entry.id <= previousEntry) ||
            !symbols.insert(entry.entry + "\n" + entry.profile).second) {
            outError = "built-in shader entry table is invalid or unsorted";
            return false;
        }
        previousEntry = entry.id;
    }

    std::string previousDefine{};
    for (const ShaderDefine& define : program.defines) {
        if (!INTERNAL::IsIdentifier(define.name, false) || define.value.size() > kMaximumShaderDefineValueBytes ||
            define.value.find('\0') != std::string::npos ||
            (!previousDefine.empty() && define.name <= previousDefine)) {
            outError = "built-in shader define table is invalid or unsorted";
            return false;
        }
        previousDefine = define.name;
    }

    if (program.state == BuiltinShaderProgramState::Pending) {
        if (!ContractIsEmpty(program.contract)) {
            outError = "pending built-in shader has a cook contract";
            return false;
        }
        return true;
    }
    ShaderSourceManifest manifest{};
    return BuildBuiltinShaderSourceManifest(program, manifest, outError);
}

template <class TEnum, class TToString>
bool ParseEnum(std::string_view text, TEnum count, TToString toString, TEnum& outValue) noexcept {
    for (uint32_t index = 0u; index < static_cast<uint32_t>(count); ++index) {
        const auto value = static_cast<TEnum>(index);
        if (text == toString(value)) {
            outValue = value;
            return true;
        }
    }
    return false;
}

} // namespace

const char* ToString(BuiltinShaderProgramState state) noexcept {
    switch (state) {
    case BuiltinShaderProgramState::Pending: return "Pending";
    case BuiltinShaderProgramState::Cooked: return "Cooked";
    case BuiltinShaderProgramState::Count: break;
    }
    return "Unknown";
}

bool ParseBuiltinShaderProgramState(std::string_view text, BuiltinShaderProgramState& outState) noexcept {
    return ParseEnum(
        text, BuiltinShaderProgramState::Count, [](BuiltinShaderProgramState value) { return ToString(value); },
        outState);
}

const char* ToString(BuiltinShaderScope scope) noexcept {
    switch (scope) {
    case BuiltinShaderScope::Runtime: return "Runtime";
    case BuiltinShaderScope::ToolOnly: return "ToolOnly";
    case BuiltinShaderScope::Count: break;
    }
    return "Unknown";
}

bool ParseBuiltinShaderScope(std::string_view text, BuiltinShaderScope& outScope) noexcept {
    return ParseEnum(
        text, BuiltinShaderScope::Count, [](BuiltinShaderScope value) { return ToString(value); }, outScope);
}

const char* ToString(BuiltinShaderExclusionReason reason) noexcept {
    switch (reason) {
    case BuiltinShaderExclusionReason::IncludeOnly: return "IncludeOnly";
    case BuiltinShaderExclusionReason::Unreferenced: return "Unreferenced";
    case BuiltinShaderExclusionReason::LegacyToolOnly: return "LegacyToolOnly";
    case BuiltinShaderExclusionReason::Count: break;
    }
    return "Unknown";
}

bool ParseBuiltinShaderExclusionReason(std::string_view text, BuiltinShaderExclusionReason& outReason) noexcept {
    return ParseEnum(
        text, BuiltinShaderExclusionReason::Count, [](BuiltinShaderExclusionReason value) { return ToString(value); },
        outReason);
}

bool ValidateBuiltinShaderCatalog(const BuiltinShaderCatalog& catalog, std::string& outError) {
    if (catalog.version != kBuiltinShaderCatalogVersion || catalog.programs.empty() ||
        catalog.programs.size() > kMaximumBuiltinShaderProgramCount ||
        catalog.excludedSources.size() > kMaximumBuiltinShaderExclusionCount) {
        outError = "built-in shader catalog version or count is invalid";
        return false;
    }

    std::string previousId{};
    std::unordered_set<std::string> assetGuids{};
    std::unordered_set<std::string> programSources{};
    for (const BuiltinShaderProgramDefinition& program : catalog.programs) {
        if (!ValidateProgram(program, outError) || (!previousId.empty() && program.programId <= previousId) ||
            !assetGuids.insert(program.assetGuid.value).second) {
            if (outError.empty()) {
                outError = "built-in shader programs are unsorted or duplicated";
            }
            return false;
        }
        previousId = program.programId;
        programSources.insert(TEXT::ToLowerAsciiCopy(program.sourcePath.generic_string()));
    }

    std::string previousExcluded{};
    for (const BuiltinShaderSourceExclusion& exclusion : catalog.excludedSources) {
        const std::string sourceKey = TEXT::ToLowerAsciiCopy(exclusion.sourcePath.generic_string());
        if (!IsSafeEngineShaderPath(exclusion.sourcePath) || exclusion.reason >= BuiltinShaderExclusionReason::Count ||
            (!previousExcluded.empty() && sourceKey <= previousExcluded) || programSources.contains(sourceKey)) {
            outError = "built-in shader exclusions are invalid or duplicated";
            return false;
        }
        previousExcluded = sourceKey;
    }
    outError.clear();
    return true;
}

bool BuildBuiltinShaderSourceManifest(const BuiltinShaderProgramDefinition& program, ShaderSourceManifest& outManifest,
                                      std::string& outError) {
    if (program.state != BuiltinShaderProgramState::Cooked) {
        outError = "built-in shader program is not artifact-enabled";
        return false;
    }
    ShaderSourceManifest manifest{};
    manifest.programId = program.programId;
    manifest.sourcePath = program.sourcePath;
    manifest.entries = program.entries;
    manifest.defines = program.defines;
    manifest.includeRoots = program.includeRoots;
    manifest.engineInterfaceId = program.contract.engineInterfaceId;
    manifest.engineInterfaceVersion = program.contract.engineInterfaceVersion;
    manifest.engineInterfaceHash = program.contract.engineInterfaceHash;
    manifest.rootLayoutId = program.contract.rootLayoutId;
    manifest.rootLayoutVersion = program.contract.rootLayoutVersion;
    manifest.rootLayoutHash = program.contract.rootLayoutHash;
    manifest.requiredCaps = program.contract.requiredCaps;
    if (!ValidateShaderSourceManifest(manifest, outError)) {
        return false;
    }
    outManifest = std::move(manifest);
    outError.clear();
    return true;
}

} // namespace HIKARI::ASSETS::SHADER
