#include "RenderContracts/Shader/Internal/HIKARI_HshadercMetadata.h"
#include "RenderContracts/Shader/HIKARI_HshadercFormat.h"
#include "RenderContracts/Shader/HIKARI_ShaderParameterSerialization.h"

#include <utility>

#include <json.hpp>

namespace HIKARI::ASSETS::SHADER::INTERNAL {

bool SerializeHshadercMetadata(const ShaderCookedArtifact& artifact, std::span<const HshadercDxilRange> dxilRanges,
                               std::string& outJson, std::string& outError) {
    if (dxilRanges.size() != artifact.entries.size() || !ValidateShaderCookedArtifact(artifact, outError)) {
        if (outError.empty()) {
            outError = "shader metadata DXIL range count mismatch";
        }
        return false;
    }

    nlohmann::ordered_json root{};
    root["schema"] = "HIKARI.ShaderCookedArtifact";
    root["version"] = artifact.version;
    root["sourceGuid"] = artifact.sourceGuid.value;
    root["programId"] = artifact.programId;
    root["manifestHash"] = HASH::ToHex(artifact.manifestHash);
    root["sourceClosureHash"] = HASH::ToHex(artifact.sourceClosureHash);
    root["compiler"] = {
        {"id", artifact.compiler.id},
        {"version", artifact.compiler.version},
        {"contractVersion", artifact.compiler.contractVersion},
        {"binaryHash", HASH::ToHex(artifact.compiler.binaryHash)},
        {"validatorBinaryHash", HASH::ToHex(artifact.compiler.validatorBinaryHash)},
    };
    root["engineInterface"] = {
        {"id", artifact.engineInterfaceId},
        {"version", artifact.engineInterfaceVersion},
        {"hash", HASH::ToHex(artifact.engineInterfaceHash)},
    };
    root["rootLayout"] = {
        {"id", artifact.rootLayoutId},
        {"version", artifact.rootLayoutVersion},
        {"hash", HASH::ToHex(artifact.rootLayoutHash)},
    };
    root["requiredCaps"] = {
        {"minimumShaderModel", artifact.requiredCaps.minimumShaderModel},
        {"minimumRootSignatureVersion", artifact.requiredCaps.minimumRootSignatureVersion},
        {"requiredFeatureMask", artifact.requiredCaps.requiredFeatureMask},
    };
    if (!artifact.parameters.empty()) {
        root["parameters"] = WriteShaderParameterDefinitions(artifact.parameters);
    }

    root["dependencies"] = nlohmann::ordered_json::array();
    for (const ShaderSourceDependency& dependency : artifact.dependencies) {
        root["dependencies"].push_back({
            {"logicalPath", dependency.logicalPath},
            {"byteSize", dependency.byteSize},
            {"contentHash", HASH::ToHex(dependency.contentHash)},
        });
    }

    root["entries"] = nlohmann::ordered_json::array();
    for (size_t index = 0u; index < artifact.entries.size(); ++index) {
        const ShaderCookedEntry& entry = artifact.entries[index];
        nlohmann::ordered_json item{};
        item["id"] = entry.entry.id;
        item["stage"] = ToString(entry.entry.stage);
        item["entry"] = entry.entry.entry;
        item["profile"] = entry.entry.profile;
        item["compilerArguments"] = entry.compilerArguments;
        item["compilerArgumentsHash"] = HASH::ToHex(entry.compilerArgumentsHash);
        item["reflectionHash"] = HASH::ToHex(entry.reflectionHash);
        item["dxilOffset"] = dxilRanges[index].offset;
        item["dxilBytes"] = dxilRanges[index].size;
        item["dxilHash"] = HASH::ToHex(entry.dxilHash);
        item["bindings"] = nlohmann::ordered_json::array();
        for (const ShaderResourceBinding& binding : entry.bindings) {
            item["bindings"].push_back({
                {"kind", static_cast<uint32_t>(binding.kind)},
                {"registerIndex", binding.registerIndex},
                {"registerSpace", binding.registerSpace},
                {"count", binding.count},
                {"visibilityMask", binding.visibilityMask},
            });
        }
        root["entries"].push_back(std::move(item));
    }

    outJson = root.dump();
    if (outJson.empty() || outJson.size() > kMaximumHshadercMetadataBytes) {
        outError = "shader cooked metadata exceeds its size limit";
        outJson.clear();
        return false;
    }
    outError.clear();
    return true;
}

} // namespace HIKARI::ASSETS::SHADER::INTERNAL
