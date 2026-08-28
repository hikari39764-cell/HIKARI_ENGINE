#include "Assets/Shader/HIKARI_ShaderSourceManifest.h"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <utility>

#include <json.hpp>

namespace HIKARI::ASSETS::SHADER {
    namespace {

        bool ReadRequiredString(
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

        bool HasOnlyKeys(
            const nlohmann::json& object,
            std::initializer_list<std::string_view> allowed) {
            if (!object.is_object()) {
                return false;
            }
            for (auto item = object.begin(); item != object.end(); ++item) {
                const bool known = std::any_of(
                    allowed.begin(),
                    allowed.end(),
                    [&item](std::string_view key) {
                        return item.key() == key;
                    });
                if (!known) {
                    return false;
                }
            }
            return true;
        }

    } // namespace

    bool ParseShaderSourceManifest(
        std::string_view jsonText,
        ShaderSourceManifest& outManifest,
        std::string& outError) {
        if (jsonText.empty() ||
            jsonText.size() > kMaximumShaderManifestBytes) {
            outError = "shader source manifest size is invalid";
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
            if (depthExceeded) {
                outError = "shader source manifest nesting is too deep";
                return false;
            }
            if (!root.is_object()) {
                outError = "shader source manifest root must be an object";
                return false;
            }
            if (!HasOnlyKeys(root, {
                    "schema",
                    "version",
                    "programId",
                    "source",
                    "engineInterfaceId",
                    "engineInterfaceVersion",
                    "rootLayoutId",
                    "requiredCaps",
                    "entries",
                    "defines",
                    "includeRoots"
                })) {
                outError = "shader source manifest contains unknown fields";
                return false;
            }
            ShaderSourceManifest manifest{};
            manifest.version = root.value("version", 0u);
            std::string schema{};
            std::string source{};
            if (!ReadRequiredString(root, "schema", schema) ||
                schema != kShaderSourceManifestSchema ||
                !ReadRequiredString(root, "programId", manifest.programId) ||
                !ReadRequiredString(root, "source", source) ||
                !ReadRequiredString(
                    root,
                    "engineInterfaceId",
                    manifest.engineInterfaceId) ||
                !ReadRequiredString(
                    root,
                    "rootLayoutId",
                    manifest.rootLayoutId)) {
                outError = "shader source manifest is missing required strings";
                return false;
            }
            manifest.sourcePath = source;
            manifest.engineInterfaceVersion =
                root.value("engineInterfaceVersion", 0u);

            const auto caps = root.find("requiredCaps");
            if (caps == root.end() || !caps->is_object()) {
                outError = "shader source manifest requiredCaps is missing";
                return false;
            }
            if (!HasOnlyKeys(*caps, {
                    "minimumShaderModel",
                    "minimumRootSignatureVersion",
                    "requiredFeatureMask"
                })) {
                outError = "shader requiredCaps contains unknown fields";
                return false;
            }
            manifest.requiredCaps.minimumShaderModel =
                caps->value("minimumShaderModel", 0u);
            manifest.requiredCaps.minimumRootSignatureVersion =
                caps->value("minimumRootSignatureVersion", 0u);
            manifest.requiredCaps.requiredFeatureMask =
                caps->value("requiredFeatureMask", 0ull);

            const auto entries = root.find("entries");
            if (entries == root.end() || !entries->is_array() ||
                entries->size() > kMaximumShaderEntryCount) {
                outError = "shader source manifest entries are invalid";
                return false;
            }
            for (const nlohmann::json& item : *entries) {
                ShaderEntryPoint entry{};
                std::string stage{};
                if (!HasOnlyKeys(item, {
                        "id", "stage", "entry", "profile"
                    }) ||
                    !ReadRequiredString(item, "id", entry.id) ||
                    !ReadRequiredString(item, "stage", stage) ||
                    !ReadRequiredString(item, "entry", entry.entry) ||
                    !ReadRequiredString(item, "profile", entry.profile) ||
                    !ParseShaderStage(stage, entry.stage)) {
                    outError = "shader entry record is invalid";
                    return false;
                }
                manifest.entries.push_back(std::move(entry));
            }

            const auto defines = root.find("defines");
            if (defines != root.end()) {
                if (!defines->is_array() ||
                    defines->size() > kMaximumShaderDefineCount) {
                    outError = "shader define table is invalid";
                    return false;
                }
                for (const nlohmann::json& item : *defines) {
                    ShaderDefine define{};
                    if (!HasOnlyKeys(item, { "name", "value" }) ||
                        !ReadRequiredString(item, "name", define.name) ||
                        !ReadRequiredString(item, "value", define.value)) {
                        outError = "shader define record is invalid";
                        return false;
                    }
                    manifest.defines.push_back(std::move(define));
                }
            }

            const auto includeRoots = root.find("includeRoots");
            if (includeRoots != root.end()) {
                if (!includeRoots->is_array() ||
                    includeRoots->size() >
                        static_cast<size_t>(ShaderIncludeRoot::Count)) {
                    outError = "shader include-root table is invalid";
                    return false;
                }
                for (const nlohmann::json& item : *includeRoots) {
                    ShaderIncludeRoot includeRoot{};
                    if (!item.is_string() ||
                        !ParseShaderIncludeRoot(
                            item.get<std::string>(),
                            includeRoot)) {
                        outError = "shader include-root value is invalid";
                        return false;
                    }
                    manifest.includeRoots.push_back(includeRoot);
                }
            }
            if (!ValidateShaderSourceManifest(manifest, outError)) {
                return false;
            }
            outManifest = std::move(manifest);
            outError.clear();
            return true;
        }
        catch (const std::exception& error) {
            outError = "shader source manifest parse failed: " +
                std::string(error.what());
            return false;
        }
    }

    bool LoadShaderSourceManifest(
        const std::filesystem::path& path,
        ShaderSourceManifest& outManifest,
        std::string& outError) {
        std::error_code ec{};
        const uintmax_t fileBytes = std::filesystem::file_size(path, ec);
        if (ec || fileBytes == 0u ||
            fileBytes > kMaximumShaderManifestBytes) {
            outError = "shader source manifest file size is invalid";
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            outError = "could not open shader source manifest";
            return false;
        }
        std::string text(static_cast<size_t>(fileBytes), '\0');
        input.read(
            text.data(),
            static_cast<std::streamsize>(text.size()));
        if (!input || input.gcount() !=
                static_cast<std::streamsize>(text.size()) ||
            input.peek() != std::char_traits<char>::eof()) {
            outError = "shader source manifest changed while reading";
            return false;
        }
        return ParseShaderSourceManifest(text, outManifest, outError);
    }

    bool SerializeCanonicalShaderSourceManifest(
        const ShaderSourceManifest& manifest,
        std::string& outJson,
        std::string& outError) {
        if (!ValidateShaderSourceManifest(manifest, outError)) {
            return false;
        }
        ShaderSourceManifest canonical = manifest;
        std::sort(
            canonical.entries.begin(),
            canonical.entries.end(),
            [](const ShaderEntryPoint& lhs,
               const ShaderEntryPoint& rhs) {
                return lhs.id < rhs.id;
            });
        std::sort(
            canonical.defines.begin(),
            canonical.defines.end(),
            [](const ShaderDefine& lhs, const ShaderDefine& rhs) {
                return lhs.name < rhs.name;
            });
        std::sort(
            canonical.includeRoots.begin(),
            canonical.includeRoots.end(),
            [](ShaderIncludeRoot lhs, ShaderIncludeRoot rhs) {
                return static_cast<uint8_t>(lhs) <
                    static_cast<uint8_t>(rhs);
            });

        nlohmann::ordered_json root{};
        root["schema"] = kShaderSourceManifestSchema;
        root["version"] = canonical.version;
        root["programId"] = canonical.programId;
        root["source"] =
            canonical.sourcePath.lexically_normal().generic_string();
        root["engineInterfaceId"] = canonical.engineInterfaceId;
        root["engineInterfaceVersion"] =
            canonical.engineInterfaceVersion;
        root["rootLayoutId"] = canonical.rootLayoutId;
        root["requiredCaps"] = {
            { "minimumShaderModel",
              canonical.requiredCaps.minimumShaderModel },
            { "minimumRootSignatureVersion",
              canonical.requiredCaps.minimumRootSignatureVersion },
            { "requiredFeatureMask",
              canonical.requiredCaps.requiredFeatureMask },
        };
        root["includeRoots"] = nlohmann::ordered_json::array();
        for (ShaderIncludeRoot includeRoot : canonical.includeRoots) {
            root["includeRoots"].push_back(ToString(includeRoot));
        }
        root["defines"] = nlohmann::ordered_json::array();
        for (const ShaderDefine& define : canonical.defines) {
            root["defines"].push_back({
                { "name", define.name },
                { "value", define.value },
            });
        }
        root["entries"] = nlohmann::ordered_json::array();
        for (const ShaderEntryPoint& entry : canonical.entries) {
            root["entries"].push_back({
                { "id", entry.id },
                { "stage", ToString(entry.stage) },
                { "entry", entry.entry },
                { "profile", entry.profile },
            });
        }
        outJson = root.dump();
        outError.clear();
        return true;
    }

    bool ComputeShaderSourceManifestHash(
        const ShaderSourceManifest& manifest,
        HASH::Sha256Digest& outHash,
        std::string& outError) {
        std::string canonical{};
        if (!SerializeCanonicalShaderSourceManifest(
                manifest,
                canonical,
                outError) ||
            !HASH::ComputeSha256(canonical, outHash)) {
            if (outError.empty()) {
                outError = "could not hash shader source manifest";
            }
            return false;
        }
        outError.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::SHADER
