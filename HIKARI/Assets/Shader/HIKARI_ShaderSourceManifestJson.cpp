#include "Assets/Shader/Internal/HIKARI_ShaderJson.h"
#include "Assets/Shader/HIKARI_ShaderSourceManifest.h"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <utility>

#include <json.hpp>

namespace HIKARI::ASSETS::SHADER {
bool ParseShaderSourceManifest(std::string_view jsonText, ShaderSourceManifest& outManifest, std::string& outError) {
    if (jsonText.empty() || jsonText.size() > kMaximumShaderManifestBytes) {
        outError = "shader source manifest size is invalid";
        return false;
    }
    try {
        bool depthExceeded = false;
        const nlohmann::json root = JSON::Parse(jsonText, depthExceeded);
        if (depthExceeded) {
            outError = "shader source manifest nesting is too deep";
            return false;
        }
        if (!root.is_object()) {
            outError = "shader source manifest root must be an object";
            return false;
        }
        if (!JSON::HasOnlyKeys(root,
                               {"schema", "version", "programId", "source", "engineInterfaceId",
                                "engineInterfaceVersion", "engineInterfaceHash", "rootLayoutId", "rootLayoutVersion",
                                "rootLayoutHash", "requiredCaps", "entries", "defines", "includeRoots"})) {
            outError = "shader source manifest contains unknown fields";
            return false;
        }
        ShaderSourceManifest manifest{};
        manifest.version = root.value("version", 0u);
        std::string schema{};
        std::string source{};
        if (!JSON::ReadString(root, "schema", schema) || schema != kShaderSourceManifestSchema ||
            !JSON::ReadString(root, "programId", manifest.programId) || !JSON::ReadString(root, "source", source) ||
            !JSON::ReadString(root, "engineInterfaceId", manifest.engineInterfaceId) ||
            !JSON::ReadHash(root, "engineInterfaceHash", manifest.engineInterfaceHash) ||
            !JSON::ReadString(root, "rootLayoutId", manifest.rootLayoutId) ||
            !JSON::ReadHash(root, "rootLayoutHash", manifest.rootLayoutHash)) {
            outError = "shader source manifest is missing required strings";
            return false;
        }
        manifest.sourcePath = source;
        manifest.engineInterfaceVersion = root.value("engineInterfaceVersion", 0u);
        manifest.rootLayoutVersion = root.value("rootLayoutVersion", 0u);

        const auto caps = root.find("requiredCaps");
        if (caps == root.end() || !caps->is_object()) {
            outError = "shader source manifest requiredCaps is missing";
            return false;
        }
        if (!JSON::HasOnlyKeys(*caps, {"minimumShaderModel", "minimumRootSignatureVersion", "requiredFeatureMask"})) {
            outError = "shader requiredCaps contains unknown fields";
            return false;
        }
        manifest.requiredCaps.minimumShaderModel = caps->value("minimumShaderModel", 0u);
        manifest.requiredCaps.minimumRootSignatureVersion = caps->value("minimumRootSignatureVersion", 0u);
        manifest.requiredCaps.requiredFeatureMask = caps->value("requiredFeatureMask", 0ull);

        const auto entries = root.find("entries");
        if (entries == root.end() || !JSON::ParseEntries(*entries, manifest.entries)) {
            outError = "shader entry table or record is invalid";
            return false;
        }
        if (const auto defines = root.find("defines");
            defines != root.end() && !JSON::ParseDefines(*defines, manifest.defines)) {
            outError = "shader define table or record is invalid";
            return false;
        }
        if (const auto roots = root.find("includeRoots");
            roots != root.end() && !JSON::ParseIncludeRoots(*roots, manifest.includeRoots)) {
            outError = "shader include-root table or value is invalid";
            return false;
        }
        if (!ValidateShaderSourceManifest(manifest, outError)) {
            return false;
        }
        outManifest = std::move(manifest);
        outError.clear();
        return true;
    } catch (const std::exception& error) {
        outError = "shader source manifest parse failed: " + std::string(error.what());
        return false;
    }
}

bool LoadShaderSourceManifest(const std::filesystem::path& path, ShaderSourceManifest& outManifest,
                              std::string& outError) {
    std::error_code ec{};
    const uintmax_t fileBytes = std::filesystem::file_size(path, ec);
    if (ec || fileBytes == 0u || fileBytes > kMaximumShaderManifestBytes) {
        outError = "shader source manifest file size is invalid";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        outError = "could not open shader source manifest";
        return false;
    }
    std::string text(static_cast<size_t>(fileBytes), '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(text.size()) ||
        input.peek() != std::char_traits<char>::eof()) {
        outError = "shader source manifest changed while reading";
        return false;
    }
    return ParseShaderSourceManifest(text, outManifest, outError);
}

bool SerializeCanonicalShaderSourceManifest(const ShaderSourceManifest& manifest, std::string& outJson,
                                            std::string& outError) {
    if (!ValidateShaderSourceManifest(manifest, outError)) {
        return false;
    }
    ShaderSourceManifest canonical = manifest;
    std::sort(canonical.entries.begin(), canonical.entries.end(),
              [](const ShaderEntryPoint& lhs, const ShaderEntryPoint& rhs) { return lhs.id < rhs.id; });
    std::sort(canonical.defines.begin(), canonical.defines.end(),
              [](const ShaderDefine& lhs, const ShaderDefine& rhs) { return lhs.name < rhs.name; });
    std::sort(canonical.includeRoots.begin(), canonical.includeRoots.end(),
              [](ShaderIncludeRoot lhs, ShaderIncludeRoot rhs) {
                  return static_cast<uint8_t>(lhs) < static_cast<uint8_t>(rhs);
              });

    nlohmann::ordered_json root{};
    root["schema"] = kShaderSourceManifestSchema;
    root["version"] = canonical.version;
    root["programId"] = canonical.programId;
    root["source"] = canonical.sourcePath.lexically_normal().generic_string();
    root["engineInterfaceId"] = canonical.engineInterfaceId;
    root["engineInterfaceVersion"] = canonical.engineInterfaceVersion;
    root["engineInterfaceHash"] = HASH::ToHex(canonical.engineInterfaceHash);
    root["rootLayoutId"] = canonical.rootLayoutId;
    root["rootLayoutVersion"] = canonical.rootLayoutVersion;
    root["rootLayoutHash"] = HASH::ToHex(canonical.rootLayoutHash);
    root["requiredCaps"] = {
        {"minimumShaderModel", canonical.requiredCaps.minimumShaderModel},
        {"minimumRootSignatureVersion", canonical.requiredCaps.minimumRootSignatureVersion},
        {"requiredFeatureMask", canonical.requiredCaps.requiredFeatureMask},
    };
    root["includeRoots"] = nlohmann::ordered_json::array();
    for (ShaderIncludeRoot includeRoot : canonical.includeRoots) {
        root["includeRoots"].push_back(ToString(includeRoot));
    }
    root["defines"] = nlohmann::ordered_json::array();
    for (const ShaderDefine& define : canonical.defines) {
        root["defines"].push_back({
            {"name", define.name},
            {"value", define.value},
        });
    }
    root["entries"] = nlohmann::ordered_json::array();
    for (const ShaderEntryPoint& entry : canonical.entries) {
        root["entries"].push_back({
            {"id", entry.id},
            {"stage", ToString(entry.stage)},
            {"entry", entry.entry},
            {"profile", entry.profile},
        });
    }
    outJson = root.dump();
    outError.clear();
    return true;
}

bool ComputeShaderSourceManifestHash(const ShaderSourceManifest& manifest, HASH::Sha256Digest& outHash,
                                     std::string& outError) {
    std::string canonical{};
    if (!SerializeCanonicalShaderSourceManifest(manifest, canonical, outError) ||
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
