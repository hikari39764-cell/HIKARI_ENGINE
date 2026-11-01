#include "Assets/Sprite/HIKARI_SpriteMaterialSerialization.h"

#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <json.hpp>

#include "RenderContracts/Shader/HIKARI_ShaderParameterSerialization.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"
#include "Core/Text/HIKARI_Utf8.h"
#include "Core/Binding/HIKARI_BindingValue.h"

namespace HIKARI::SPRITE {
namespace {

bool HasOnlyKeys(const nlohmann::json& object, std::initializer_list<std::string_view> keys, std::string& outError,
                 std::string_view path) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        const std::string_view key = it.key();
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            outError = std::string(path) + " contains an unknown field: " + it.key();
            return false;
        }
    }
    return true;
}

bool ValidateStoredShape(const SpriteMaterialAssetData& asset, std::string& outError) {
    if (asset.version != kSpriteMaterialAssetVersion || asset.name.empty() || asset.name.size() > 128u ||
        asset.name.find('\0') != std::string::npos || !TEXT::IsValidUtf8(asset.name) ||
        !asset.shaderAssetGuid.IsValid() || asset.parameters.size() > ASSETS::SHADER::kShaderParameterSlotCount) {
        outError = "SpriteMaterial version, name, Shader GUID, or parameter count is invalid";
        return false;
    }
    std::unordered_set<std::string_view> keys{};
    for (const ASSETS::SHADER::ShaderParameterOverride& parameter : asset.parameters) {
        if (parameter.key.empty() || parameter.key.size() > ASSETS::SHADER::kShaderParameterMetadataMaximumBytes ||
            parameter.key.find('\0') != std::string::npos || !TEXT::IsValidUtf8(parameter.key) ||
            !keys.emplace(parameter.key).second || BINDING::GetValueComponentCount(parameter.value.type) == 0u ||
            !BINDING::IsValueFinite(parameter.value) ||
            (parameter.value.type == BINDING::ValueType::Int &&
             (parameter.value.integer < -ASSETS::SHADER::kShaderParameterMaximumExactInteger ||
              parameter.value.integer > ASSETS::SHADER::kShaderParameterMaximumExactInteger))) {
            outError = "SpriteMaterial contains an invalid or duplicate parameter value";
            return false;
        }
    }
    return true;
}

bool ReadAssetFields(const nlohmann::json& root, bool artifact, const AssetGuid* expectedSourceGuid,
                     SpriteMaterialAssetData& outAsset, std::string& outError) {
    if (!root.is_object()) {
        return JSONSTRICT::Fail(outError, "material must be an object");
    }
    const bool validFields =
        artifact ? HasOnlyKeys(root, {"format", "version", "sourceGuid", "name", "shaderAssetGuid", "parameters"},
                               outError, "material")
                 : HasOnlyKeys(root, {"version", "name", "shaderAssetGuid", "parameters"}, outError, "material");
    if (!validFields) {
        return false;
    }
    if (!root.contains("version") || !root.contains("name") || !root.contains("shaderAssetGuid") ||
        !root.contains("parameters")) {
        return JSONSTRICT::Fail(outError, "material is missing a required field");
    }

    SpriteMaterialAssetData asset{};
    if (!JSONSTRICT::ReadUint32(root, "version", 0u, asset.version, outError, "material") ||
        asset.version != kSpriteMaterialAssetVersion) {
        return JSONSTRICT::Fail(outError, "unsupported SpriteMaterial version");
    }
    if (artifact) {
        std::string format{};
        std::string sourceGuid{};
        if (!JSONSTRICT::ReadString(root, "format", {}, format, outError, "material") ||
            !JSONSTRICT::ReadString(root, "sourceGuid", {}, sourceGuid, outError, "material")) {
            return false;
        }
        if (format != kSpriteMaterialRuntimeFormat || !IsValidAssetGuid(sourceGuid) ||
            (expectedSourceGuid != nullptr && sourceGuid != expectedSourceGuid->value)) {
            return JSONSTRICT::Fail(outError, "SpriteMaterial runtime artifact format or source GUID does not match");
        }
        asset.sourceAssetGuid = AssetGuid{std::move(sourceGuid)};
    }
    if (!JSONSTRICT::ReadString(root, "name", {}, asset.name, outError, "material") ||
        !JSONSTRICT::ReadString(root, "shaderAssetGuid", {}, asset.shaderAssetGuid.value, outError, "material") ||
        !asset.shaderAssetGuid.IsValid() ||
        !ParseSpriteMaterialParameterValues(root.at("parameters"), asset.parameters, outError)) {
        if (outError.empty()) {
            outError = "SpriteMaterial name and Shader asset GUID must be valid";
        }
        return false;
    }
    if (!ValidateStoredShape(asset, outError)) {
        return false;
    }
    outAsset = std::move(asset);
    outError.clear();
    return true;
}

nlohmann::ordered_json ToJson(const SpriteMaterialAssetData& asset, const AssetGuid* sourceGuid) {
    nlohmann::ordered_json result{};
    if (sourceGuid != nullptr) {
        result["format"] = kSpriteMaterialRuntimeFormat;
    }
    result["version"] = asset.version;
    if (sourceGuid != nullptr) {
        result["sourceGuid"] = sourceGuid->value;
    }
    result["name"] = asset.name;
    result["shaderAssetGuid"] = asset.shaderAssetGuid.value;
    result["parameters"] = WriteSpriteMaterialParameterValues(asset.parameters);
    return result;
}

} // namespace

bool ParseSpriteMaterialParameterValues(const nlohmann::json& node,
                                        std::vector<ASSETS::SHADER::ShaderParameterOverride>& outValues,
                                        std::string& outError, std::string_view path) {
    if (!node.is_object()) {
        return JSONSTRICT::Fail(outError, std::string(path) + " must be an object keyed by parameter ID");
    }
    if (node.size() > ASSETS::SHADER::kShaderParameterSlotCount) {
        return JSONSTRICT::Fail(outError, std::string(path) + " exceeds the Shader parameter slot limit");
    }
    std::vector<ASSETS::SHADER::ShaderParameterOverride> values{};
    values.reserve(node.size());
    for (auto it = node.begin(); it != node.end(); ++it) {
        if (it.key().empty() || it.key().size() > ASSETS::SHADER::kShaderParameterMetadataMaximumBytes ||
            it.key().find('\0') != std::string::npos || !TEXT::IsValidUtf8(it.key())) {
            return JSONSTRICT::Fail(outError, std::string(path) + " contains an invalid parameter key");
        }
        const std::string valuePath = std::string(path) + "." + it.key();
        if (!it.value().is_object() || !HasOnlyKeys(it.value(), {"type", "value"}, outError, valuePath) ||
            !it.value().contains("type") || !it.value().contains("value")) {
            if (outError.empty()) {
                outError = valuePath + " must contain type and value";
            }
            return false;
        }
        std::string typeName{};
        if (!JSONSTRICT::ReadString(it.value(), "type", {}, typeName, outError, valuePath)) {
            return false;
        }
        BINDING::ValueType type{};
        if (!TEXT::TryParseEnum(BINDING::kValueTypeNames, typeName, type)) {
            return JSONSTRICT::Fail(outError, valuePath + ".type is unknown");
        }
        ASSETS::SHADER::ShaderParameterOverride parameter{};
        parameter.key = it.key();
        if (!ASSETS::SHADER::ReadShaderParameterValue(it.value().at("value"), type, parameter.value, outError)) {
            outError = valuePath + ".value: " + outError;
            return false;
        }
        if (!BINDING::IsValueFinite(parameter.value) ||
            (type == BINDING::ValueType::Int &&
             (parameter.value.integer < -ASSETS::SHADER::kShaderParameterMaximumExactInteger ||
              parameter.value.integer > ASSETS::SHADER::kShaderParameterMaximumExactInteger))) {
            return JSONSTRICT::Fail(outError, valuePath + ".value is not finite or exactly representable");
        }
        values.push_back(std::move(parameter));
    }
    outValues = std::move(values);
    outError.clear();
    return true;
}

nlohmann::ordered_json WriteSpriteMaterialParameterValues(
    std::span<const ASSETS::SHADER::ShaderParameterOverride> values) {
    std::vector<const ASSETS::SHADER::ShaderParameterOverride*> sorted{};
    sorted.reserve(values.size());
    for (const ASSETS::SHADER::ShaderParameterOverride& value : values) {
        sorted.push_back(&value);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto* lhs, const auto* rhs) { return lhs->key < rhs->key; });
    nlohmann::ordered_json result = nlohmann::ordered_json::object();
    for (const ASSETS::SHADER::ShaderParameterOverride* parameter : sorted) {
        const bool validType = BINDING::GetValueComponentCount(parameter->value.type) != 0u;
        result[parameter->key] =
            nlohmann::ordered_json{{"type", validType ? BINDING::ToString(parameter->value.type) : std::string_view{}},
                                   {"value", ASSETS::SHADER::WriteShaderParameterValue(parameter->value)}};
    }
    return result;
}

bool ValidateSpriteMaterialAsset(const SpriteMaterialAssetData& asset,
                                 std::span<const ASSETS::SHADER::ShaderParameterDefinition> definitions,
                                 std::string& outError) {
    if (!ValidateStoredShape(asset, outError)) {
        return false;
    }
    ASSETS::SHADER::ShaderParameterSlots slots{};
    return ASSETS::SHADER::PackShaderParameterValues(definitions, asset.parameters, slots, outError);
}

bool LoadSpriteMaterialAssetData(const std::filesystem::path& path, SpriteMaterialAssetData& outAsset,
                                 std::string& outError) {
    nlohmann::json root{};
    if (!SERIALIZATION::JSON::ReadJsonFile(path, root, &outError)) {
        return false;
    }
    const bool artifact = root.is_object() && root.contains("format");
    return ReadAssetFields(root, artifact, nullptr, outAsset, outError);
}

bool WriteSpriteMaterialAsset(const std::filesystem::path& path, const SpriteMaterialAssetData& asset,
                              std::string& outError) {
    if (!ValidateStoredShape(asset, outError)) {
        return false;
    }
    return SERIALIZATION::JSON::WriteJsonFile(path, ToJson(asset, nullptr), &outError);
}

bool WriteSpriteMaterialAssetArtifact(const std::filesystem::path& path, const AssetGuid& sourceGuid,
                                      const SpriteMaterialAssetData& asset, std::string& outError) {
    if (!sourceGuid.IsValid() || !ValidateStoredShape(asset, outError)) {
        if (outError.empty()) {
            outError = "SpriteMaterial artifact source GUID is invalid";
        }
        return false;
    }
    return SERIALIZATION::JSON::WriteJsonFile(path, ToJson(asset, &sourceGuid), &outError);
}

bool LoadSpriteMaterialAssetArtifact(const std::filesystem::path& path, const AssetGuid& expectedSourceGuid,
                                     SpriteMaterialAssetData& outAsset, std::string& outError) {
    nlohmann::json root{};
    if (!expectedSourceGuid.IsValid() || !SERIALIZATION::JSON::ReadJsonFile(path, root, &outError)) {
        if (outError.empty()) {
            outError = "SpriteMaterial artifact source GUID is invalid";
        }
        return false;
    }
    return ReadAssetFields(root, true, &expectedSourceGuid, outAsset, outError);
}

} // namespace HIKARI::SPRITE
