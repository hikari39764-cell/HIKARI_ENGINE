#include "Assets/Shader/HIKARI_BuiltinShaderCatalog.h"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <utility>

#include <json.hpp>

namespace HIKARI::ASSETS::SHADER {
    namespace {

        bool HasOnlyKeys(
            const nlohmann::json& object,
            std::initializer_list<std::string_view> allowed) {
            if (!object.is_object()) {
                return false;
            }
            for (auto item = object.begin();
                item != object.end();
                ++item) {
                if (std::none_of(
                        allowed.begin(),
                        allowed.end(),
                        [&item](std::string_view key) {
                            return item.key() == key;
                        })) {
                    return false;
                }
            }
            return true;
        }

        bool ReadString(
            const nlohmann::json& object,
            const char* key,
            std::string& outValue) {
            const auto found = object.find(key);
            if (found == object.end() || !found->is_string()) {
                return false;
            }
            outValue = found->get<std::string>();
            return true;
        }

        bool ReadHash(
            const nlohmann::json& object,
            const char* key,
            HASH::Sha256Digest& outHash) {
            std::string text{};
            return ReadString(object, key, text) &&
                HASH::ParseSha256(text, outHash);
        }

        bool ParseEntries(
            const nlohmann::json& node,
            std::vector<ShaderEntryPoint>& outEntries) {
            if (!node.is_array() || node.empty() ||
                node.size() > kMaximumShaderEntryCount) {
                return false;
            }
            std::vector<ShaderEntryPoint> entries{};
            entries.reserve(node.size());
            for (const nlohmann::json& item : node) {
                ShaderEntryPoint entry{};
                std::string stage{};
                if (!HasOnlyKeys(
                        item,
                        { "id", "stage", "entry", "profile" }) ||
                    !ReadString(item, "id", entry.id) ||
                    !ReadString(item, "stage", stage) ||
                    !ReadString(item, "entry", entry.entry) ||
                    !ReadString(item, "profile", entry.profile) ||
                    !ParseShaderStage(stage, entry.stage)) {
                    return false;
                }
                entries.push_back(std::move(entry));
            }
            outEntries = std::move(entries);
            return true;
        }

        bool ParseDefines(
            const nlohmann::json& node,
            std::vector<ShaderDefine>& outDefines) {
            if (!node.is_array() ||
                node.size() > kMaximumShaderDefineCount) {
                return false;
            }
            std::vector<ShaderDefine> defines{};
            defines.reserve(node.size());
            for (const nlohmann::json& item : node) {
                ShaderDefine define{};
                if (!HasOnlyKeys(item, { "name", "value" }) ||
                    !ReadString(item, "name", define.name) ||
                    !ReadString(item, "value", define.value)) {
                    return false;
                }
                defines.push_back(std::move(define));
            }
            outDefines = std::move(defines);
            return true;
        }

        bool ParseIncludeRoots(
            const nlohmann::json& node,
            std::vector<ShaderIncludeRoot>& outRoots) {
            if (!node.is_array() ||
                node.size() >
                    static_cast<size_t>(ShaderIncludeRoot::Count)) {
                return false;
            }
            std::vector<ShaderIncludeRoot> roots{};
            roots.reserve(node.size());
            for (const nlohmann::json& item : node) {
                ShaderIncludeRoot root{};
                if (!item.is_string() ||
                    !ParseShaderIncludeRoot(
                        item.get<std::string>(),
                        root)) {
                    return false;
                }
                roots.push_back(root);
            }
            outRoots = std::move(roots);
            return true;
        }

        bool ParseContract(
            const nlohmann::json& node,
            BuiltinShaderCookContract& outContract) {
            if (!HasOnlyKeys(node, {
                    "engineInterfaceId",
                    "engineInterfaceVersion",
                    "engineInterfaceHash",
                    "rootLayoutId",
                    "rootLayoutVersion",
                    "rootLayoutHash",
                    "requiredCaps"
                })) {
                return false;
            }
            BuiltinShaderCookContract contract{};
            if (!ReadString(
                    node,
                    "engineInterfaceId",
                    contract.engineInterfaceId) ||
                !ReadHash(
                    node,
                    "engineInterfaceHash",
                    contract.engineInterfaceHash) ||
                !ReadString(
                    node,
                    "rootLayoutId",
                    contract.rootLayoutId) ||
                !ReadHash(
                    node,
                    "rootLayoutHash",
                    contract.rootLayoutHash)) {
                return false;
            }
            contract.engineInterfaceVersion =
                node.value("engineInterfaceVersion", 0u);
            contract.rootLayoutVersion =
                node.value("rootLayoutVersion", 0u);
            const auto caps = node.find("requiredCaps");
            if (caps == node.end() || !HasOnlyKeys(*caps, {
                    "minimumShaderModel",
                    "minimumRootSignatureVersion",
                    "requiredFeatureMask"
                })) {
                return false;
            }
            contract.requiredCaps.minimumShaderModel =
                caps->value("minimumShaderModel", 0u);
            contract.requiredCaps.minimumRootSignatureVersion =
                caps->value("minimumRootSignatureVersion", 0u);
            contract.requiredCaps.requiredFeatureMask =
                caps->value("requiredFeatureMask", 0ull);
            outContract = std::move(contract);
            return true;
        }

        bool ParseProgram(
            const nlohmann::json& node,
            BuiltinShaderProgramDefinition& outProgram) {
            if (!HasOnlyKeys(node, {
                    "assetGuid",
                    "programId",
                    "source",
                    "scope",
                    "state",
                    "owner",
                    "group",
                    "includeRoots",
                    "defines",
                    "entries",
                    "contract"
                })) {
                return false;
            }
            BuiltinShaderProgramDefinition program{};
            std::string source{};
            std::string scope{};
            std::string state{};
            const auto entries = node.find("entries");
            const auto defines = node.find("defines");
            const auto roots = node.find("includeRoots");
            if (!ReadString(
                    node,
                    "assetGuid",
                    program.assetGuid.value) ||
                !ReadString(node, "programId", program.programId) ||
                !ReadString(node, "source", source) ||
                !ReadString(node, "scope", scope) ||
                !ReadString(node, "state", state) ||
                !ReadString(node, "owner", program.owner) ||
                !ReadString(node, "group", program.group) ||
                entries == node.end() || defines == node.end() ||
                roots == node.end() ||
                !ParseBuiltinShaderScope(scope, program.scope) ||
                !ParseBuiltinShaderProgramState(state, program.state) ||
                !ParseEntries(*entries, program.entries) ||
                !ParseDefines(*defines, program.defines) ||
                !ParseIncludeRoots(*roots, program.includeRoots)) {
                return false;
            }
            program.sourcePath = source;
            const auto contract = node.find("contract");
            if (program.state == BuiltinShaderProgramState::Cooked) {
                if (contract == node.end() ||
                    !ParseContract(*contract, program.contract)) {
                    return false;
                }
            }
            else if (contract != node.end()) {
                return false;
            }
            outProgram = std::move(program);
            return true;
        }

        bool ParseExclusion(
            const nlohmann::json& node,
            BuiltinShaderSourceExclusion& outExclusion) {
            if (!HasOnlyKeys(node, { "source", "reason" })) {
                return false;
            }
            BuiltinShaderSourceExclusion exclusion{};
            std::string source{};
            std::string reason{};
            if (!ReadString(node, "source", source) ||
                !ReadString(node, "reason", reason) ||
                !ParseBuiltinShaderExclusionReason(
                    reason,
                    exclusion.reason)) {
                return false;
            }
            exclusion.sourcePath = source;
            outExclusion = std::move(exclusion);
            return true;
        }

    } // namespace

    bool ParseBuiltinShaderCatalog(
        std::string_view jsonText,
        BuiltinShaderCatalog& outCatalog,
        std::string& outError) {
        if (jsonText.empty() ||
            jsonText.size() > kMaximumBuiltinShaderCatalogBytes) {
            outError = "built-in shader catalog size is invalid";
            return false;
        }
        try {
            bool depthExceeded = false;
            const nlohmann::json::parser_callback_t depthGuard =
                [&depthExceeded](
                    int depth,
                    nlohmann::json::parse_event_t,
                    nlohmann::json&) {
                    if (depth > 32) {
                        depthExceeded = true;
                        return false;
                    }
                    return true;
                };
            const nlohmann::json root = nlohmann::json::parse(
                jsonText.begin(),
                jsonText.end(),
                depthGuard,
                true,
                false);
            if (depthExceeded || !HasOnlyKeys(root, {
                    "schema",
                    "version",
                    "programs",
                    "excludedSources"
                })) {
                outError = depthExceeded
                    ? "built-in shader catalog nesting is too deep"
                    : "built-in shader catalog root is invalid";
                return false;
            }
            std::string schema{};
            const auto programs = root.find("programs");
            const auto exclusions = root.find("excludedSources");
            BuiltinShaderCatalog catalog{};
            catalog.version = root.value("version", 0u);
            if (!ReadString(root, "schema", schema) ||
                schema != kBuiltinShaderCatalogSchema ||
                programs == root.end() || !programs->is_array() ||
                programs->empty() ||
                programs->size() >
                    kMaximumBuiltinShaderProgramCount ||
                exclusions == root.end() ||
                !exclusions->is_array() ||
                exclusions->size() >
                    kMaximumBuiltinShaderExclusionCount) {
                outError =
                    "built-in shader catalog schema or tables are invalid";
                return false;
            }
            catalog.programs.reserve(programs->size());
            for (const nlohmann::json& item : *programs) {
                BuiltinShaderProgramDefinition program{};
                if (!ParseProgram(item, program)) {
                    outError =
                        "built-in shader program record is invalid";
                    return false;
                }
                catalog.programs.push_back(std::move(program));
            }
            catalog.excludedSources.reserve(exclusions->size());
            for (const nlohmann::json& item : *exclusions) {
                BuiltinShaderSourceExclusion exclusion{};
                if (!ParseExclusion(item, exclusion)) {
                    outError =
                        "built-in shader exclusion record is invalid";
                    return false;
                }
                catalog.excludedSources.push_back(
                    std::move(exclusion));
            }
            if (!ValidateBuiltinShaderCatalog(catalog, outError)) {
                return false;
            }
            outCatalog = std::move(catalog);
            outError.clear();
            return true;
        }
        catch (const std::exception& error) {
            outError = "built-in shader catalog parse failed: " +
                std::string(error.what());
            return false;
        }
    }

    bool LoadBuiltinShaderCatalog(
        const std::filesystem::path& path,
        BuiltinShaderCatalog& outCatalog,
        std::string& outError) {
        std::error_code ec{};
        const uintmax_t fileBytes = std::filesystem::file_size(path, ec);
        if (ec || fileBytes == 0u ||
            fileBytes > kMaximumBuiltinShaderCatalogBytes) {
            outError = "built-in shader catalog file size is invalid";
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            outError = "could not open built-in shader catalog";
            return false;
        }
        std::string text(static_cast<size_t>(fileBytes), '\0');
        input.read(
            text.data(),
            static_cast<std::streamsize>(text.size()));
        if (!input || input.gcount() !=
                static_cast<std::streamsize>(text.size()) ||
            input.peek() != std::char_traits<char>::eof()) {
            outError = "built-in shader catalog changed while reading";
            return false;
        }
        return ParseBuiltinShaderCatalog(text, outCatalog, outError);
    }

} // namespace HIKARI::ASSETS::SHADER
