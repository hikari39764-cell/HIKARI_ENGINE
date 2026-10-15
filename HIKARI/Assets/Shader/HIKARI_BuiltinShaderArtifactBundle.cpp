#include "Assets/Shader/Internal/HIKARI_ShaderJson.h"
#include "Core/IO/HIKARI_PathNormalization.h"
#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"

#include <unordered_set>

#include <json.hpp>

#include "Assets/Formats/HIKARI_HshadercFormat.h"
#include "Assets/Shader/HIKARI_BuiltinShaderCatalog.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI::ASSETS::SHADER {
namespace {

bool ResolveContainedRegularFile(const std::filesystem::path& root, const std::filesystem::path& relative,
                                 std::filesystem::path& out, std::error_code& error) {
    out = std::filesystem::weakly_canonical(root / relative, error);
    const std::filesystem::path physicalRelative =
        error ? std::filesystem::path{} : std::filesystem::relative(out, root, error);
    return !error && !physicalRelative.empty() && !physicalRelative.is_absolute() &&
           !PATHS::HasParentTraversal(physicalRelative) && std::filesystem::is_regular_file(out, error) && !error;
}

const BuiltinShaderProgramDefinition* FindProgram(const BuiltinShaderCatalog& catalog,
                                                  std::string_view programId) noexcept {
    for (const BuiltinShaderProgramDefinition& program : catalog.programs) {
        if (program.programId == programId) {
            return &program;
        }
    }
    return nullptr;
}

bool MatchesContract(const ShaderCookedArtifact& artifact, const BuiltinShaderCookContract& contract) noexcept {
    return artifact.engineInterfaceId == contract.engineInterfaceId &&
           artifact.engineInterfaceVersion == contract.engineInterfaceVersion &&
           artifact.engineInterfaceHash == contract.engineInterfaceHash &&
           artifact.rootLayoutId == contract.rootLayoutId && artifact.rootLayoutVersion == contract.rootLayoutVersion &&
           artifact.rootLayoutHash == contract.rootLayoutHash &&
           artifact.requiredCaps.minimumShaderModel == contract.requiredCaps.minimumShaderModel &&
           artifact.requiredCaps.minimumRootSignatureVersion == contract.requiredCaps.minimumRootSignatureVersion &&
           artifact.requiredCaps.requiredFeatureMask == contract.requiredCaps.requiredFeatureMask;
}

bool MatchesEntries(const ShaderCookedArtifact& artifact, const BuiltinShaderProgramDefinition& program) noexcept {
    if (artifact.entries.size() != program.entries.size()) {
        return false;
    }
    for (size_t index = 0u; index < program.entries.size(); ++index) {
        const ShaderEntryPoint& expected = program.entries[index];
        const ShaderEntryPoint& actual = artifact.entries[index].entry;
        if (actual.id != expected.id || actual.stage != expected.stage || actual.entry != expected.entry ||
            actual.profile != expected.profile) {
            return false;
        }
    }
    return true;
}

} // namespace

bool LoadBuiltinShaderArtifactBundle(const std::filesystem::path& requestedShaderRoot,
                                     std::vector<BuiltinShaderArtifactProgram>& outPrograms,
                                     std::string& outError) noexcept {
    try {
        outPrograms.clear();
        outError.clear();
        std::error_code pathError{};
        const std::filesystem::path shaderRoot = std::filesystem::weakly_canonical(requestedShaderRoot, pathError);
        const std::filesystem::path cookedRoot = std::filesystem::weakly_canonical(shaderRoot / "Cooked", pathError);
        const std::filesystem::path cookedRelative =
            pathError ? std::filesystem::path{} : std::filesystem::relative(cookedRoot, shaderRoot, pathError);
        std::filesystem::path catalogPath{};
        std::filesystem::path indexPath{};
        if (pathError || shaderRoot.empty() || cookedRoot.empty() || cookedRelative.empty() ||
            cookedRelative.is_absolute() || PATHS::HasParentTraversal(cookedRelative) ||
            !ResolveContainedRegularFile(shaderRoot, kBuiltinShaderCatalogFilename, catalogPath, pathError) ||
            !ResolveContainedRegularFile(cookedRoot, "BuiltinShaderArtifacts.json", indexPath, pathError)) {
            outError = "built-in Shader authority escapes its root";
            return false;
        }

        BuiltinShaderCatalog catalog{};
        HASH::Sha256Digest catalogHash{};
        std::string validationError{};
        if (!LoadBuiltinShaderCatalog(catalogPath, catalog, validationError) ||
            !ComputeBuiltinShaderCatalogHash(catalog, catalogHash, validationError)) {
            outError = "built-in Shader catalog is invalid: " + validationError;
            return false;
        }

        nlohmann::json index{};
        HASH::Sha256Digest indexedCatalogHash{};
        if (!SERIALIZATION::JSON::ReadJsonFile(indexPath, index, &validationError) || !index.is_object() ||
            index.value("schema", std::string{}) != "HIKARI.BuiltinShaderArtifacts" ||
            index.value("version", 0u) != 1u || !JSON::ReadHash(index, "catalogHash", indexedCatalogHash) ||
            indexedCatalogHash != catalogHash) {
            outError = "built-in Shader index differs from its catalog";
            return false;
        }
        const auto programs = index.find("programs");
        size_t expectedCookedCount = 0u;
        for (const BuiltinShaderProgramDefinition& program : catalog.programs) {
            expectedCookedCount += program.state == BuiltinShaderProgramState::Cooked ? 1u : 0u;
        }
        if (programs == index.end() || !programs->is_array() || programs->size() != expectedCookedCount ||
            expectedCookedCount == 0u) {
            outError = "built-in Shader index program count differs";
            return false;
        }

        std::unordered_set<std::string> indexedPrograms{};
        std::vector<BuiltinShaderArtifactProgram> validated{};
        validated.reserve(expectedCookedCount);
        for (const nlohmann::json& node : *programs) {
            if (!node.is_object()) {
                outError = "built-in Shader artifact record is invalid";
                return false;
            }
            const std::string programId = node.value("programId", std::string{});
            const std::string assetGuid = node.value("assetGuid", std::string{});
            const std::filesystem::path relative = node.value("artifact", std::string{});
            const uint32_t entryCount = node.value("entryCount", 0u);
            HASH::Sha256Digest artifactHash{};
            HASH::Sha256Digest sourceClosureHash{};
            const BuiltinShaderProgramDefinition* program = FindProgram(catalog, programId);
            if (program == nullptr || program->state != BuiltinShaderProgramState::Cooked ||
                program->assetGuid.value != assetGuid || program->entries.size() != entryCount ||
                !indexedPrograms.insert(programId).second || relative.empty() || relative.is_absolute() ||
                relative.has_root_name() || relative.has_root_directory() || PATHS::HasParentTraversal(relative) ||
                relative.extension() != ".hshaderc" || !JSON::ReadHash(node, "artifactHash", artifactHash) ||
                !JSON::ReadHash(node, "sourceClosureHash", sourceClosureHash)) {
                outError = "built-in Shader index identity is invalid";
                return false;
            }

            ShaderSourceManifest expectedManifest{};
            HASH::Sha256Digest expectedManifestHash{};
            if (!BuildBuiltinShaderSourceManifest(*program, expectedManifest, validationError) ||
                !ComputeShaderSourceManifestHash(expectedManifest, expectedManifestHash, validationError)) {
                outError = "built-in Shader manifest is invalid";
                return false;
            }
            std::filesystem::path artifactPath{};
            if (!ResolveContainedRegularFile(cookedRoot, relative, artifactPath, pathError)) {
                outError = "built-in Shader artifact escapes its root";
                return false;
            }
            HASH::Sha256Digest actualHash{};
            auto artifact = std::make_shared<ShaderCookedArtifact>();
            if (!HASH::ComputeFileSha256(artifactPath, actualHash, validationError) || actualHash != artifactHash ||
                !ReadHshadercFile(artifactPath, *artifact, validationError) ||
                artifact->sourceGuid.value != assetGuid || artifact->programId != programId ||
                artifact->manifestHash != expectedManifestHash || artifact->sourceClosureHash != sourceClosureHash ||
                !MatchesContract(*artifact, program->contract) || !MatchesEntries(*artifact, *program)) {
                outError = "built-in HSHADERC admission failed: " + validationError;
                return false;
            }
            validated.push_back({programId, program->assetGuid, std::move(artifact)});
        }
        outPrograms = std::move(validated);
        return true;
    } catch (...) {
        outPrograms.clear();
        outError = "built-in Shader bundle validation threw an exception";
        return false;
    }
}

} // namespace HIKARI::ASSETS::SHADER
