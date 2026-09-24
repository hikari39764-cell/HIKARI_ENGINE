#include "Core/HIKARI_StartupTimer.h"
#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Assets/Shader/HIKARI_RenderGeometryShaderContract.h"

namespace HIKARI::ASSETS::SHADER {
namespace {

struct ResolverState {
    std::mutex mutex{};
    bool initialized = false;
    bool failed = false;
    std::string diagnostic{};
    std::vector<BuiltinShaderArtifactProgram> programs{};
};

ResolverState& GetState() noexcept {
    static ResolverState state{};
    return state;
}

ShaderAssetResolveResult Failure(ShaderAssetResolveCode code, std::string diagnostic) {
    ShaderAssetResolveResult result{};
    result.code = code;
    result.diagnostic = std::move(diagnostic);
    return result;
}

bool Initialize(ResolverState& state) noexcept {
    if (state.initialized) {
        return !state.failed;
    }
    state.initialized = true;
    CORE::StartupTimer bundleTimer("Shaders.load_builtin_bundle");
    state.programs.clear();
    state.failed = !LoadBuiltinShaderArtifactBundle(GFX::ResolveShaderSourcePath(L"HIKARI/Shaders"), state.programs,
                                                    state.diagnostic);
    return !state.failed;
}

} // namespace

ShaderAssetResolveResult ResolveBuiltinShaderEntry(std::string_view programId, const ShaderAssetEntryExpectation& entry,
                                                   const ShaderAssetContractExpectation& contract) noexcept {
    try {
        ResolverState& state = GetState();
        const std::lock_guard lock(state.mutex);
        if (!Initialize(state)) {
            return Failure(ShaderAssetResolveCode::ArtifactUnavailable, state.diagnostic);
        }
        const BuiltinShaderArtifactProgram* match = nullptr;
        for (const BuiltinShaderArtifactProgram& program : state.programs) {
            if (program.programId != programId) {
                continue;
            }
            if (match != nullptr) {
                return Failure(ShaderAssetResolveCode::ProgramAmbiguous, "built-in Shader program ID is duplicated");
            }
            match = &program;
        }
        if (match == nullptr) {
            return Failure(ShaderAssetResolveCode::ProgramNotFound, "built-in Shader program is not cooked");
        }
        return ShaderAssetResolver::ResolveArtifact(match->artifact, entry, contract);
    } catch (...) {
        return Failure(ShaderAssetResolveCode::ArtifactInvalid, "built-in Shader resolver threw an exception");
    }
}

ShaderAssetResolveResult ResolveBuiltinRenderGeometryShaderEntry(
    std::string_view programId, const ShaderAssetEntryExpectation& entry,
    const ShaderAssetContractExpectation& contract) noexcept {
    auto resolved = ResolveBuiltinShaderEntry(programId, entry, contract);
    if (!resolved)
        return resolved;
    try {
        const auto* artifact = resolved.lease.GetArtifact();
        std::string diagnostic;
        if (!artifact || !ValidateRenderGeometryShaderArtifact(*artifact, diagnostic) ||
            resolved.lease.GetDxil().empty()) {
            return Failure(ShaderAssetResolveCode::ArtifactInvalid,
                           diagnostic.empty() ? "invalid RenderGeometry shader artifact" : diagnostic);
        }
        return resolved;
    } catch (...) {
        return Failure(ShaderAssetResolveCode::ArtifactInvalid, "RenderGeometry validation failed");
    }
}

void ResetBuiltinShaderArtifactResolver() noexcept {
    ResolverState& state = GetState();
    const std::lock_guard lock(state.mutex);
    state.initialized = false;
    state.failed = false;
    state.diagnostic.clear();
    state.programs.clear();
}

} // namespace HIKARI::ASSETS::SHADER
