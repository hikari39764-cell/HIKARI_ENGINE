#include "RenderContracts/Shader/HIKARI_BuiltinShaderCatalog.h"

#include <utility>

#include <json.hpp>

namespace HIKARI::ASSETS::SHADER {

bool SerializeCanonicalBuiltinShaderCatalog(const BuiltinShaderCatalog& catalog, std::string& outJson,
                                            std::string& outError) {
    if (!ValidateBuiltinShaderCatalog(catalog, outError)) {
        return false;
    }
    nlohmann::ordered_json root{};
    root["schema"] = kBuiltinShaderCatalogSchema;
    root["version"] = catalog.version;
    root["programs"] = nlohmann::ordered_json::array();
    for (const BuiltinShaderProgramDefinition& program : catalog.programs) {
        nlohmann::ordered_json node{};
        node["assetGuid"] = program.assetGuid.value;
        node["programId"] = program.programId;
        node["source"] = program.sourcePath.generic_string();
        node["scope"] = ToString(program.scope);
        node["state"] = ToString(program.state);
        node["owner"] = program.owner;
        node["group"] = program.group;
        node["includeRoots"] = nlohmann::ordered_json::array();
        for (ShaderIncludeRoot includeRoot : program.includeRoots) {
            node["includeRoots"].push_back(ToString(includeRoot));
        }
        node["defines"] = nlohmann::ordered_json::array();
        for (const ShaderDefine& define : program.defines) {
            node["defines"].push_back({
                {"name", define.name},
                {"value", define.value},
            });
        }
        node["entries"] = nlohmann::ordered_json::array();
        for (const ShaderEntryPoint& entry : program.entries) {
            node["entries"].push_back({
                {"id", entry.id},
                {"stage", ToString(entry.stage)},
                {"entry", entry.entry},
                {"profile", entry.profile},
            });
        }
        if (program.state == BuiltinShaderProgramState::Cooked) {
            node["contract"] = {
                {"engineInterfaceId", program.contract.engineInterfaceId},
                {"engineInterfaceVersion", program.contract.engineInterfaceVersion},
                {"engineInterfaceHash", HASH::ToHex(program.contract.engineInterfaceHash)},
                {"rootLayoutId", program.contract.rootLayoutId},
                {"rootLayoutVersion", program.contract.rootLayoutVersion},
                {"rootLayoutHash", HASH::ToHex(program.contract.rootLayoutHash)},
                {"requiredCaps",
                 {
                     {"minimumShaderModel", program.contract.requiredCaps.minimumShaderModel},
                     {"minimumRootSignatureVersion", program.contract.requiredCaps.minimumRootSignatureVersion},
                     {"requiredFeatureMask", program.contract.requiredCaps.requiredFeatureMask},
                 }},
            };
        }
        root["programs"].push_back(std::move(node));
    }
    root["excludedSources"] = nlohmann::ordered_json::array();
    for (const BuiltinShaderSourceExclusion& exclusion : catalog.excludedSources) {
        root["excludedSources"].push_back({
            {"source", exclusion.sourcePath.generic_string()},
            {"reason", ToString(exclusion.reason)},
        });
    }
    outJson = root.dump();
    outError.clear();
    return true;
}

bool ComputeBuiltinShaderCatalogHash(const BuiltinShaderCatalog& catalog, HASH::Sha256Digest& outHash,
                                     std::string& outError) {
    std::string canonical{};
    if (!SerializeCanonicalBuiltinShaderCatalog(catalog, canonical, outError) ||
        !HASH::ComputeSha256(canonical, outHash)) {
        if (outError.empty()) {
            outError = "could not hash canonical built-in shader catalog";
        }
        return false;
    }
    outError.clear();
    return true;
}

} // namespace HIKARI::ASSETS::SHADER
