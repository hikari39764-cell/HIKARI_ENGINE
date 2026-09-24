#pragma once
#include "Assets/Formats/HIKARI_HshadercFormat.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Gfx/Internal/HIKARI_DxcRuntime.h"
#include "Gfx/Internal/HIKARI_ShaderIncludePlan.h"
#include <algorithm>

namespace HIKARI::TOOLS {
// キャッシュも正式な HSHADERC を検証する。時刻だけで DXIL を再利用しない。
inline bool TryReuseBuiltinShader(const std::filesystem::path& outputRoot, const nlohmann::json& previousIndex,
                                  const AssetGuid& guid, const HASH::Sha256Digest& manifestHash,
                                  const GFX::ShaderCookRequest& request, ASSETS::SHADER::ShaderCookedArtifact& artifact,
                                  std::string& includePlanHash) {
    try {
        std::string error;
        GFX::INTERNAL::ShaderCookRoot primary;
        std::vector<GFX::INTERNAL::ShaderCookRoot> roots;
        GFX::INTERNAL::ShaderCookFile source;
        std::shared_ptr<const GFX::INTERNAL::ShaderIncludePlan> plan;
        if (!GFX::INTERNAL::BuildShaderCookRoots(request, roots, primary, error) ||
            !GFX::INTERNAL::ReadShaderCookFile(primary, request.sourcePath, ".hlsl", source, error) ||
            !GFX::INTERNAL::BuildShaderIncludePlan(source, primary, roots, plan, error))
            return false;
        nlohmann::json resolution = nlohmann::json::object();
        for (const auto& [alias, file] : plan->aliases)
            resolution[alias] = file->dependency.logicalPath;
        HASH::Sha256Digest resolutionHash;
        if (!HASH::ComputeSha256(resolution.dump(), resolutionHash))
            return false;
        includePlanHash = HASH::ToHex(resolutionHash);
        if (!previousIndex.is_object() || !previousIndex.contains("programs"))
            return false;
        const auto& programs = previousIndex.at("programs");
        const auto found = std::find_if(programs.begin(), programs.end(), [&](const auto& p) {
            return p.value("assetGuid", std::string{}) == guid.value;
        });
        if (found == programs.end())
            return false;
        const std::filesystem::path relative(found->at("artifact").template get<std::string>());
        if (relative.is_absolute() || relative.empty())
            return false;
        for (const auto& part : relative)
            if (part == "..")
                return false;
        HASH::Sha256Digest fileHash;
        const auto path = outputRoot / relative;
        if (!HASH::ComputeFileSha256(path, fileHash, error) ||
            HASH::ToHex(fileHash) != found->at("artifactHash").get<std::string>() ||
            !ASSETS::SHADER::ReadHshadercFile(path, artifact, error) || artifact.sourceGuid != guid ||
            artifact.manifestHash != manifestHash)
            return false;
        GFX::INTERNAL::DxcRuntimeSnapshot runtime;
        if (!GFX::INTERNAL::AcquireDxcRuntime(GFX::INTERNAL::DxcRuntimePurpose::Cook, runtime, error) ||
            artifact.compiler.contractVersion != GFX::kShaderCookCompilerContractVersion ||
            artifact.compiler.binaryHash != runtime.compilerBinaryHash ||
            artifact.compiler.validatorBinaryHash != runtime.validatorBinaryHash)
            return false;
        if (request.entries.size() != artifact.entries.size())
            return false;
        for (const auto& entry : request.entries) {
            const auto cached = std::find_if(artifact.entries.begin(), artifact.entries.end(),
                                             [&](const auto& e) { return e.entry.id == entry.id; });
            std::vector<std::string> arguments;
            if (cached == artifact.entries.end() ||
                !GFX::BuildCanonicalShaderCookArguments(request, entry, arguments, error) ||
                arguments != cached->compilerArguments)
                return false;
        }
        std::vector<GFX::ShaderCookDependency> dependencies;
        for (const auto& d : artifact.dependencies)
            dependencies.push_back({d.logicalPath, d.byteSize, d.contentHash});
        if (!GFX::VerifyShaderCookDependencies(request, dependencies, error))
            return false;
        // 再解決して追加ファイルによる include の隠蔽も検出する。
        if (found->contains("includePlanHash"))
            return found->at("includePlanHash").get<std::string>() == includePlanHash;
        // 旧 index は解決表を持たない。未使用 include がある場合は一度だけ cook。
        for (const auto& [alias, file] : plan->aliases) {
            if (std::none_of(dependencies.begin(), dependencies.end(), [&](const auto& d) {
                    return d.logicalPath == file->dependency.logicalPath &&
                           d.contentHash == file->dependency.contentHash;
                }))
                return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace HIKARI::TOOLS
