#pragma once
#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <json.hpp>
#include "Assets/Shader/HIKARI_ShaderSourceManifest.h"
#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER::JSON {
inline bool HasOnlyKeys(const nlohmann::json& object, std::initializer_list<std::string_view> allowed) {
    if (!object.is_object())
        return false;
    for (auto item = object.begin(); item != object.end(); ++item) {
        if (std::none_of(allowed.begin(), allowed.end(), [&item](std::string_view key) { return item.key() == key; }))
            return false;
    }
    return true;
}
inline bool ReadString(const nlohmann::json& object, const char* key, std::string& out) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string())
        return false;
    out = found->get<std::string>();
    return true;
}
inline bool ReadHash(const nlohmann::json& object, const char* key, HASH::Sha256Digest& out) {
    std::string text;
    return ReadString(object, key, text) && HASH::ParseSha256(text, out);
}
inline nlohmann::json Parse(std::string_view text, bool& depthExceeded) {
    depthExceeded = false;
    return nlohmann::json::parse(
        text.begin(), text.end(),
        [&depthExceeded](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
            if (depth <= 32)
                return true;
            depthExceeded = true;
            return false;
        },
        true, false);
}

inline bool ParseEntries(const nlohmann::json& node, std::vector<ShaderEntryPoint>& outEntries) {
    if (!node.is_array() || node.size() > kMaximumShaderEntryCount) {
        return false;
    }
    std::vector<ShaderEntryPoint> entries{};
    entries.reserve(node.size());
    for (const nlohmann::json& item : node) {
        ShaderEntryPoint entry{};
        std::string stage{};
        if (!JSON::HasOnlyKeys(item, {"id", "stage", "entry", "profile"}) || !JSON::ReadString(item, "id", entry.id) ||
            !JSON::ReadString(item, "stage", stage) || !JSON::ReadString(item, "entry", entry.entry) ||
            !JSON::ReadString(item, "profile", entry.profile) || !ParseShaderStage(stage, entry.stage)) {
            return false;
        }
        entries.push_back(std::move(entry));
    }
    outEntries = std::move(entries);
    return true;
}

inline bool ParseDefines(const nlohmann::json& node, std::vector<ShaderDefine>& outDefines) {
    if (!node.is_array() || node.size() > kMaximumShaderDefineCount) {
        return false;
    }
    std::vector<ShaderDefine> defines{};
    defines.reserve(node.size());
    for (const nlohmann::json& item : node) {
        ShaderDefine define{};
        if (!JSON::HasOnlyKeys(item, {"name", "value"}) || !JSON::ReadString(item, "name", define.name) ||
            !JSON::ReadString(item, "value", define.value)) {
            return false;
        }
        defines.push_back(std::move(define));
    }
    outDefines = std::move(defines);
    return true;
}

inline bool ParseIncludeRoots(const nlohmann::json& node, std::vector<ShaderIncludeRoot>& outRoots) {
    if (!node.is_array() || node.size() > static_cast<size_t>(ShaderIncludeRoot::Count)) {
        return false;
    }
    std::vector<ShaderIncludeRoot> roots{};
    roots.reserve(node.size());
    for (const nlohmann::json& item : node) {
        ShaderIncludeRoot root{};
        if (!item.is_string() || !ParseShaderIncludeRoot(item.get<std::string>(), root)) {
            return false;
        }
        roots.push_back(root);
    }
    outRoots = std::move(roots);
    return true;
}

} // namespace HIKARI::ASSETS::SHADER::JSON
