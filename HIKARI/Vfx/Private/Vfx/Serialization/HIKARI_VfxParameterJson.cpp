#include "Vfx/Serialization/HIKARI_VfxParameterJson.h"

#include <string>
#include <unordered_set>
#include <utility>

#include "Core/Serialization/Json/HIKARI_JsonStrict.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Vfx/Parameters/HIKARI_VfxParameterValues.h"

namespace HIKARI::VFX::SERIALIZATION {
namespace {
constexpr TEXT::EnumName<ParamType> kParamTypeNames[]{
    {ParamType::Float, "Float"},   {ParamType::Float2, "Float2"}, {ParamType::Float3, "Float3"},
    {ParamType::Float4, "Float4"}, {ParamType::Color, "Color"},   {ParamType::Color3, "Color3"},
    {ParamType::Color4, "Color4"}, {ParamType::Toggle, "Toggle"},
};

// 1〜4 個の有限数。足りない成分は 0 のまま。
bool ReadComponents(const nlohmann::json& node, const char* key, float (&out)[4], std::string& error,
                    const std::string& path) {
    const auto member = node.find(key);
    if (member == node.end() || !member->is_array() || member->empty() || member->size() > 4u) {
        return JSONSTRICT::Fail(error, JSONSTRICT::FieldPath(path, key) + " must be an array of one to four numbers");
    }
    for (size_t index = 0; index < member->size(); ++index) {
        const nlohmann::json& value = (*member)[index];
        if (!value.is_number() || !JSONSTRICT::IsFloatRange(value.get<double>())) {
            return JSONSTRICT::Fail(error, JSONSTRICT::FieldPath(path, key) + " must contain finite numbers");
        }
        out[index] = value.get<float>();
    }
    return true;
}

bool ReadParameterDescriptor(const nlohmann::json& node, size_t slotCount, ParamDesc& out, std::string& error,
                             const std::string& path) {
    if (!node.is_object()) {
        return JSONSTRICT::Fail(error, path + " must be an object");
    }
    std::string typeName;
    if (!node.contains("key") || !JSONSTRICT::ReadString(node, "key", {}, out.key, error, path) || out.key.empty()) {
        return JSONSTRICT::Fail(error, path + ".key must be a non-empty string");
    }
    if (!JSONSTRICT::ReadString(node, "label", out.key, out.label, error, path) ||
        !JSONSTRICT::ReadString(node, "type", {}, typeName, error, path) ||
        !JSONSTRICT::ReadFloat(node, "speed", out.speed, out.speed, error, path)) {
        return false;
    }
    const auto type = TEXT::TryParseEnum(kParamTypeNames, typeName);
    if (!type) {
        return JSONSTRICT::Fail(error, path + ".type is not a known parameter type");
    }
    out.type = *type;
    const auto ref = node.find("ref");
    uint32_t slot = 0u;
    uint32_t channel = 0u;
    if (ref == node.end() || !ref->is_object() || !ref->contains("slot") || !ref->contains("channel")) {
        return JSONSTRICT::Fail(error, path + ".ref must contain slot and channel");
    }
    if (!JSONSTRICT::ReadUint32(*ref, "slot", 0u, slot, error, path + ".ref") ||
        !JSONSTRICT::ReadUint32(*ref, "channel", 0u, channel, error, path + ".ref")) {
        return false;
    }
    if (slot >= slotCount || channel + GetParameterComponentCount(out.type) > 4u) {
        return JSONSTRICT::Fail(error, path + ".ref points outside the parameter block");
    }
    out.ref.slot = static_cast<uint8_t>(slot);
    out.ref.channel = static_cast<uint8_t>(channel);
    return ReadComponents(node, "defaultValues", out.defaultValues, error, path) &&
           ReadComponents(node, "minValues", out.minValues, error, path) &&
           ReadComponents(node, "maxValues", out.maxValues, error, path);
}
} // namespace

bool ReadParameterDescriptorsFromJson(const nlohmann::json& parameterArray, size_t slotCount,
                                      std::vector<ParamDesc>& outParameters, std::string& outError) {
    outParameters.clear();
    if (!parameterArray.is_array()) {
        return JSONSTRICT::Fail(outError, "params must be an array");
    }
    std::unordered_set<std::string> keys;
    for (size_t index = 0; index < parameterArray.size(); ++index) {
        ParamDesc parameter{};
        const std::string path = "params[" + std::to_string(index) + "]";
        if (!ReadParameterDescriptor(parameterArray[index], slotCount, parameter, outError, path)) {
            return false;
        }
        if (!keys.insert(parameter.key).second) {
            return JSONSTRICT::Fail(outError, path + ".key is duplicated: " + parameter.key);
        }
        outParameters.push_back(std::move(parameter));
    }
    return true;
}

} // namespace HIKARI::VFX::SERIALIZATION
