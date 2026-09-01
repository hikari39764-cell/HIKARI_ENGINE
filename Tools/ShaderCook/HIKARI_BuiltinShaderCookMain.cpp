#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <json.hpp>

#include "Assets/Formats/HIKARI_HshadercFormat.h"
#include "Assets/Shader/HIKARI_AutomaticExposureShaderContract.h"
#include "Assets/Shader/HIKARI_BuiltinShaderCatalog.h"
#include "Assets/Shader/HIKARI_DepthPyramidShaderContract.h"
#include "Assets/Shader/HIKARI_PostEffectShaderContract.h"
#include "Assets/Shader/HIKARI_Render2DBuiltinShaderContract.h"
#include "Assets/Shader/HIKARI_ShaderCookedArtifactBuilder.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Gfx/HIKARI_ShaderCookCompiler.h"

namespace {

    struct Arguments {
        std::filesystem::path catalog{};
        std::filesystem::path shaderRoot{};
        std::filesystem::path outputRoot{};
    };

    bool ParseArguments(
        int count,
        char** values,
        Arguments& out) {
        Arguments parsed{};
        for (int index = 1; index + 1 < count; index += 2) {
            const std::string_view key(values[index]);
            const std::filesystem::path value(values[index + 1]);
            if (key == "--catalog") {
                parsed.catalog = value;
            } else if (key == "--shader-root") {
                parsed.shaderRoot = value;
            } else if (key == "--output-root") {
                parsed.outputRoot = value;
            } else {
                return false;
            }
        }
        if (count != 7 || parsed.catalog.empty() ||
            parsed.shaderRoot.empty() || parsed.outputRoot.empty()) {
            return false;
        }
        std::error_code error{};
        parsed.catalog = std::filesystem::weakly_canonical(
            parsed.catalog,
            error);
        if (error) {
            return false;
        }
        parsed.shaderRoot = std::filesystem::weakly_canonical(
            parsed.shaderRoot,
            error);
        if (error) {
            return false;
        }
        parsed.outputRoot = std::filesystem::absolute(
            parsed.outputRoot,
            error).lexically_normal();
        if (error ||
            !std::filesystem::is_regular_file(parsed.catalog) ||
            !std::filesystem::is_directory(parsed.shaderRoot)) {
            return false;
        }
        out = std::move(parsed);
        return true;
    }

    bool CookProgram(
        const HIKARI::ASSETS::SHADER::BuiltinShaderProgramDefinition&
            program,
        const Arguments& arguments,
        nlohmann::ordered_json& outIndex,
        std::string& outError) {
        namespace SHADER = HIKARI::ASSETS::SHADER;
        SHADER::ShaderSourceManifest manifest{};
        HIKARI::HASH::Sha256Digest manifestHash{};
        if (!SHADER::BuildBuiltinShaderSourceManifest(
                program,
                manifest,
                outError) ||
            !SHADER::ComputeShaderSourceManifestHash(
                manifest,
                manifestHash,
                outError)) {
            return false;
        }

        HIKARI::GFX::ShaderCookRequest request{};
        if (!SHADER::BuildShaderCookRequest(
                manifest,
                SHADER::ShaderIncludeRoot::EngineSdk,
                {},
                arguments.shaderRoot,
                request,
                outError)) {
            return false;
        }
        HIKARI::GFX::ShaderCookOutput compiled{};
        if (!HIKARI::GFX::CompileShaderProgramForCook(
                request,
                compiled)) {
            outError = compiled.diagnostics.empty()
                ? "DXC cook failed"
                : compiled.diagnostics;
            return false;
        }

        SHADER::ShaderCookedArtifact artifact{};
        if (!SHADER::BuildShaderCookedArtifact(
                program.assetGuid,
                manifest,
                manifestHash,
                SHADER::ShaderIncludeRoot::EngineSdk,
                compiled,
                artifact,
                outError)) {
            return false;
        }
        if (program.contract.engineInterfaceId ==
                SHADER::kPostEffectEngineInterfaceId &&
            !SHADER::ValidatePostEffectShaderArtifact(
                artifact,
                outError)) {
            return false;
        }
        if (program.contract.engineInterfaceId ==
                SHADER::kAutomaticExposureEngineInterfaceId &&
            (artifact.entries.size() != 1u ||
             !SHADER::ValidateAutomaticExposureShaderArtifact(
                artifact,
                artifact.entries.front(),
                outError))) {
            return false;
        }
        if (program.contract.engineInterfaceId ==
                SHADER::kDepthPyramidEngineInterfaceId &&
            (artifact.entries.size() != 1u ||
             !SHADER::ValidateDepthPyramidShaderArtifact(
                artifact,
                artifact.entries.front(),
                outError))) {
            return false;
        }
        if (program.contract.engineInterfaceId ==
                SHADER::kRender2DBuiltinEngineInterfaceId &&
            !SHADER::ValidateRender2DBuiltinShaderArtifact(
                artifact,
                outError)) {
            return false;
        }
        std::vector<uint8_t> bytes{};
        HIKARI::HASH::Sha256Digest artifactHash{};
        if (!SHADER::SerializeHshaderc(artifact, bytes, outError) ||
            !HIKARI::HASH::ComputeSha256(
                std::as_bytes(std::span<const uint8_t>(bytes)),
                artifactHash)) {
            if (outError.empty()) {
                outError = "could not hash HSHADERC output";
            }
            return false;
        }
        const std::filesystem::path relative =
            std::filesystem::path("Programs") /
            program.assetGuid.value /
            ("shader-" + HIKARI::HASH::ToHex(artifactHash) +
             ".hshaderc");
        const std::filesystem::path output =
            arguments.outputRoot / relative;
        if (!SHADER::WriteHshadercFile(output, artifact, outError)) {
            return false;
        }
        outIndex.push_back({
            { "assetGuid", program.assetGuid.value },
            { "programId", program.programId },
            { "artifact", relative.generic_string() },
            { "artifactHash", HIKARI::HASH::ToHex(artifactHash) },
            { "sourceClosureHash",
              HIKARI::HASH::ToHex(artifact.sourceClosureHash) },
            { "entryCount", artifact.entries.size() },
        });
        return true;
    }

    bool RemoveStaleArtifacts(
        const Arguments& arguments,
        const nlohmann::ordered_json& programs,
        std::string& outError) {
        std::unordered_set<std::string> retained{};
        for (const nlohmann::ordered_json& program : programs) {
            retained.insert(
                std::filesystem::path(
                    program.value("artifact", std::string{}))
                    .lexically_normal()
                    .generic_string());
        }
        const std::filesystem::path programsRoot =
            arguments.outputRoot / "Programs";
        std::error_code error{};
        if (!std::filesystem::is_directory(programsRoot, error)) {
            return !error;
        }
        for (std::filesystem::recursive_directory_iterator iterator(
                 programsRoot,
                 std::filesystem::directory_options::skip_permission_denied,
                 error),
             end;
             iterator != end;
             iterator.increment(error)) {
            if (error) {
                outError = "could not enumerate stale HSHADERC artifacts";
                return false;
            }
            if (!iterator->is_regular_file(error) || error ||
                iterator->path().extension() != ".hshaderc") {
                error.clear();
                continue;
            }
            const std::string relative = std::filesystem::relative(
                iterator->path(),
                arguments.outputRoot,
                error).lexically_normal().generic_string();
            if (error) {
                outError = "could not classify stale HSHADERC artifact";
                return false;
            }
            if (!retained.contains(relative) &&
                !std::filesystem::remove(iterator->path(), error)) {
                outError = "could not remove stale HSHADERC artifact";
                return false;
            }
        }
        return true;
    }

} // namespace

int main(int argc, char** argv) {
    namespace SHADER = HIKARI::ASSETS::SHADER;
    Arguments arguments{};
    if (!ParseArguments(argc, argv, arguments)) {
        std::cerr << "usage: HIKARI_BuiltinShaderCookTool "
            "--catalog <json> --shader-root <dir> --output-root <dir>\n";
        return 2;
    }

    SHADER::BuiltinShaderCatalog catalog{};
    HIKARI::HASH::Sha256Digest catalogHash{};
    std::string error{};
    if (!SHADER::LoadBuiltinShaderCatalog(
            arguments.catalog,
            catalog,
            error) ||
        !SHADER::ComputeBuiltinShaderCatalogHash(
            catalog,
            catalogHash,
            error)) {
        std::cerr << "catalog admission failed: " << error << '\n';
        return 3;
    }
    std::error_code directoryError{};
    std::filesystem::create_directories(
        arguments.outputRoot,
        directoryError);
    if (directoryError) {
        std::cerr << "could not create output root\n";
        return 4;
    }

    nlohmann::ordered_json programs = nlohmann::ordered_json::array();
    uint32_t cookedCount = 0u;
    for (const SHADER::BuiltinShaderProgramDefinition& program :
         catalog.programs) {
        if (program.state != SHADER::BuiltinShaderProgramState::Cooked) {
            continue;
        }
        if (!CookProgram(program, arguments, programs, error)) {
            std::cerr << "cook failed for " << program.programId << ": "
                << error << '\n';
            return 5;
        }
        ++cookedCount;
    }
    if (cookedCount == 0u) {
        std::cerr << "catalog contains no Cooked program\n";
        return 6;
    }
    const nlohmann::ordered_json index{
        { "schema", "HIKARI.BuiltinShaderArtifacts" },
        { "version", 1u },
        { "catalogHash", HIKARI::HASH::ToHex(catalogHash) },
        { "programs", programs },
    };
    if (!HIKARI::SERIALIZATION::JSON::WriteJsonFile(
            arguments.outputRoot / "BuiltinShaderArtifacts.json",
            index,
            &error)) {
        std::cerr << "index publication failed: " << error << '\n';
        return 7;
    }
    if (!RemoveStaleArtifacts(arguments, programs, error)) {
        std::cerr << "stale artifact cleanup failed: " << error << '\n';
        return 8;
    }
    std::cout << "Built-in HSHADERC cook passed: " << cookedCount
        << " programs\n";
    return 0;
}
