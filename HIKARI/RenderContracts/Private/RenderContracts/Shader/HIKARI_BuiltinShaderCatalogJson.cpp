#include "RenderContracts/Shader/Internal/HIKARI_ShaderJson.h"
#include "RenderContracts/Shader/HIKARI_BuiltinShaderCatalog.h"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <utility>

#include <json.hpp>

namespace HIKARI::ASSETS::SHADER {
namespace {

bool ParseContract(const nlohmann::json& node, BuiltinShaderCookContract& outContract) {
    if (!JSON::HasOnlyKeys(node, {"engineInterfaceId", "engineInterfaceVersion", "engineInterfaceHash", "rootLayoutId",
                                  "rootLayoutVersion", "rootLayoutHash", "requiredCaps"})) {
        return false;
    }
    BuiltinShaderCookContract contract{};
    if (!JSON::ReadString(node, "engineInterfaceId", contract.engineInterfaceId) ||
        !JSON::ReadHash(node, "engineInterfaceHash", contract.engineInterfaceHash) ||
        !JSON::ReadString(node, "rootLayoutId", contract.rootLayoutId) ||
        !JSON::ReadHash(node, "rootLayoutHash", contract.rootLayoutHash)) {
        return false;
    }
    contract.engineInterfaceVersion = node.value("engineInterfaceVersion", 0u);
    contract.rootLayoutVersion = node.value("rootLayoutVersion", 0u);
    const auto caps = node.find("requiredCaps");
    if (caps == node.end() ||
        !JSON::HasOnlyKeys(*caps, {"minimumShaderModel", "minimumRootSignatureVersion", "requiredFeatureMask"})) {
        return false;
    }
    contract.requiredCaps.minimumShaderModel = caps->value("minimumShaderModel", 0u);
    contract.requiredCaps.minimumRootSignatureVersion = caps->value("minimumRootSignatureVersion", 0u);
    contract.requiredCaps.requiredFeatureMask = caps->value("requiredFeatureMask", 0ull);
    outContract = std::move(contract);
    return true;
}

bool ParseProgram(const nlohmann::json& node, BuiltinShaderProgramDefinition& outProgram) {
    if (!JSON::HasOnlyKeys(node, {"assetGuid", "programId", "source", "scope", "state", "owner", "group",
                                  "includeRoots", "defines", "entries", "contract"})) {
        return false;
    }
    BuiltinShaderProgramDefinition program{};
    std::string source{};
    std::string scope{};
    std::string state{};
    const auto entries = node.find("entries");
    const auto defines = node.find("defines");
    const auto roots = node.find("includeRoots");
    if (!JSON::ReadString(node, "assetGuid", program.assetGuid.value) ||
        !JSON::ReadString(node, "programId", program.programId) || !JSON::ReadString(node, "source", source) ||
        !JSON::ReadString(node, "scope", scope) || !JSON::ReadString(node, "state", state) ||
        !JSON::ReadString(node, "owner", program.owner) || !JSON::ReadString(node, "group", program.group) ||
        entries == node.end() || defines == node.end() || roots == node.end() ||
        !ParseBuiltinShaderScope(scope, program.scope) || !ParseBuiltinShaderProgramState(state, program.state) ||
        !JSON::ParseEntries(*entries, program.entries) || program.entries.empty() ||
        !JSON::ParseDefines(*defines, program.defines) || !JSON::ParseIncludeRoots(*roots, program.includeRoots)) {
        return false;
    }
    program.sourcePath = source;
    const auto contract = node.find("contract");
    if (program.state == BuiltinShaderProgramState::Cooked) {
        if (contract == node.end() || !ParseContract(*contract, program.contract)) {
            return false;
        }
    } else if (contract != node.end()) {
        return false;
    }
    outProgram = std::move(program);
    return true;
}

bool ParseExclusion(const nlohmann::json& node, BuiltinShaderSourceExclusion& outExclusion) {
    if (!JSON::HasOnlyKeys(node, {"source", "reason"})) {
        return false;
    }
    BuiltinShaderSourceExclusion exclusion{};
    std::string source{};
    std::string reason{};
    if (!JSON::ReadString(node, "source", source) || !JSON::ReadString(node, "reason", reason) ||
        !ParseBuiltinShaderExclusionReason(reason, exclusion.reason)) {
        return false;
    }
    exclusion.sourcePath = source;
    outExclusion = std::move(exclusion);
    return true;
}

} // namespace

bool ParseBuiltinShaderCatalog(std::string_view jsonText, BuiltinShaderCatalog& outCatalog, std::string& outError) {
    if (jsonText.empty() || jsonText.size() > kMaximumBuiltinShaderCatalogBytes) {
        outError = "built-in shader catalog size is invalid";
        return false;
    }
    try {
        bool depthExceeded = false;
        const nlohmann::json root = JSON::Parse(jsonText, depthExceeded);
        if (depthExceeded || !JSON::HasOnlyKeys(root, {"schema", "version", "programs", "excludedSources"})) {
            outError = depthExceeded ? "built-in shader catalog nesting is too deep"
                                     : "built-in shader catalog root is invalid";
            return false;
        }
        std::string schema{};
        const auto programs = root.find("programs");
        const auto exclusions = root.find("excludedSources");
        BuiltinShaderCatalog catalog{};
        catalog.version = root.value("version", 0u);
        if (!JSON::ReadString(root, "schema", schema) || schema != kBuiltinShaderCatalogSchema ||
            programs == root.end() || !programs->is_array() || programs->empty() ||
            programs->size() > kMaximumBuiltinShaderProgramCount || exclusions == root.end() ||
            !exclusions->is_array() || exclusions->size() > kMaximumBuiltinShaderExclusionCount) {
            outError = "built-in shader catalog schema or tables are invalid";
            return false;
        }
        catalog.programs.reserve(programs->size());
        for (const nlohmann::json& item : *programs) {
            BuiltinShaderProgramDefinition program{};
            if (!ParseProgram(item, program)) {
                outError = "built-in shader program record is invalid";
                return false;
            }
            catalog.programs.push_back(std::move(program));
        }
        catalog.excludedSources.reserve(exclusions->size());
        for (const nlohmann::json& item : *exclusions) {
            BuiltinShaderSourceExclusion exclusion{};
            if (!ParseExclusion(item, exclusion)) {
                outError = "built-in shader exclusion record is invalid";
                return false;
            }
            catalog.excludedSources.push_back(std::move(exclusion));
        }
        if (!ValidateBuiltinShaderCatalog(catalog, outError)) {
            return false;
        }
        outCatalog = std::move(catalog);
        outError.clear();
        return true;
    } catch (const std::exception& error) {
        outError = "built-in shader catalog parse failed: " + std::string(error.what());
        return false;
    }
}

bool LoadBuiltinShaderCatalog(const std::filesystem::path& path, BuiltinShaderCatalog& outCatalog,
                              std::string& outError) {
    std::error_code ec{};
    const uintmax_t fileBytes = std::filesystem::file_size(path, ec);
    if (ec || fileBytes == 0u || fileBytes > kMaximumBuiltinShaderCatalogBytes) {
        outError = "built-in shader catalog file size is invalid";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        outError = "could not open built-in shader catalog";
        return false;
    }
    std::string text(static_cast<size_t>(fileBytes), '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(text.size()) ||
        input.peek() != std::char_traits<char>::eof()) {
        outError = "built-in shader catalog changed while reading";
        return false;
    }
    return ParseBuiltinShaderCatalog(text, outCatalog, outError);
}

} // namespace HIKARI::ASSETS::SHADER
