#pragma once

#include <d3d12.h>

#include "RenderContracts/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "RenderContracts/Shader/HIKARI_TemporalShaderContract.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"

namespace HIKARI::RENDER3D::TEMPORAL::INTERNAL {

// PSO 作成完了まで呼び出し元が immutable DXIL の lease を保持する。
inline ASSETS::SHADER::ShaderAssetResolveResult LoadTemporalShader(ASSETS::SHADER::TemporalShaderPass pass,
                                                                   const char* entryId,
                                                                   ASSETS::SHADER::ShaderStage stage) {
    namespace SHADER = ASSETS::SHADER;
    const auto index = static_cast<size_t>(pass);
    if (index >= SHADER::kTemporalShaders.size()) {
        return {};
    }
    const auto contract = SHADER::MakeTemporalShaderContract(pass, {60u, 10u, 0u});
    auto result = SHADER::ResolveBuiltinShaderEntry(
        SHADER::kTemporalShaders[index].programId,
        {entryId, stage, stage == SHADER::ShaderStage::Vertex ? "vs_6_0" : "ps_6_0"}, contract);
    std::string diagnostic{};
    if (!result || !SHADER::ValidateTemporalShaderArtifact(*result.lease.GetArtifact(), diagnostic)) {
        DEBUGLOG::PushRenderError(
            "Temporal cooked shader unavailable: " + std::string(SHADER::kTemporalShaders[index].programId) + "/" +
            entryId + ": " + (result ? diagnostic : result.diagnostic));
        return {};
    }
    return result;
}

inline D3D12_SHADER_BYTECODE TemporalShaderBytecode(const ASSETS::SHADER::ShaderAssetResolveResult& shader) noexcept {
    const auto bytes = shader.lease.GetDxil();
    return {bytes.data(), bytes.size()};
}

} // namespace HIKARI::RENDER3D::TEMPORAL::INTERNAL
