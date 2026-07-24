#include "Vfx/Serialization/HIKARI_VfxParameterJson.h"

#include <string>
#include <utility>

namespace HIKARI::VFX::SERIALIZATION {
    namespace {
        ParamType ParseParameterType(
            const nlohmann::json& valueNode,
            ParamType fallback) {

            const std::string value = valueNode.is_string()
                ? valueNode.get<std::string>()
                : std::string{};
            if (value == "Float" || value == "float") return ParamType::Float;
            if (value == "Float2" || value == "float2") return ParamType::Float2;
            if (value == "Float3" || value == "float3") return ParamType::Float3;
            if (value == "Float4" || value == "float4") return ParamType::Float4;
            if (value == "Color") return ParamType::Color;
            if (value == "Color3" || value == "color3") return ParamType::Color3;
            if (value == "Color4" || value == "color4") return ParamType::Color4;
            if (value == "Toggle" || value == "toggle") return ParamType::Toggle;
            return fallback;
        }

        uint8_t ParseParameterChannel(const nlohmann::json& valueNode) {
            const std::string value = valueNode.is_string()
                ? valueNode.get<std::string>()
                : std::string{};
            if (value == "y" || value == "yz" || value == "yzw") return 1;
            if (value == "z" || value == "zw") return 2;
            if (value == "w") return 3;
            return 0;
        }

        void ReadFloatArray4(
            const nlohmann::json& arrayNode,
            float (&outValues)[4]) {

            if (arrayNode.is_number()) {
                outValues[0] = arrayNode.get<float>();
                return;
            }
            if (!arrayNode.is_array()) {
                return;
            }

            for (size_t index = 0;
                 index < 4 && index < arrayNode.size();
                 ++index) {
                if (arrayNode[index].is_number()) {
                    outValues[index] = arrayNode[index].get<float>();
                }
            }
        }

        ParamDesc ReadParameterDescriptor(const nlohmann::json& parameterNode) {
            ParamDesc parameter{};
            parameter.key = parameterNode.value("key", parameter.key);
            parameter.label = parameterNode.value(
                "label",
                parameterNode.value("displayName", parameter.label));
            parameter.type = ParseParameterType(
                parameterNode.value("type", nlohmann::json{}),
                parameter.type);

            if (parameterNode.contains("ref") &&
                parameterNode["ref"].is_object()) {
                const auto& ref = parameterNode["ref"];
                parameter.ref.slot = static_cast<uint8_t>(
                    ref.value("slot", static_cast<int>(parameter.ref.slot)));
                parameter.ref.channel = static_cast<uint8_t>(
                    ref.value("channel", static_cast<int>(parameter.ref.channel)));
            } else {
                parameter.ref.slot = static_cast<uint8_t>(
                    parameterNode.value(
                        "slot",
                        static_cast<int>(parameter.ref.slot)));
                parameter.ref.channel = ParseParameterChannel(
                    parameterNode.value("component", nlohmann::json{}));
            }

            ReadFloatArray4(
                parameterNode.value(
                    "defaultValues",
                    parameterNode.value("default", nlohmann::json{})),
                parameter.defaultValues);
            ReadFloatArray4(
                parameterNode.value(
                    "minValues",
                    parameterNode.value("min", nlohmann::json{})),
                parameter.minValues);
            ReadFloatArray4(
                parameterNode.value(
                    "maxValues",
                    parameterNode.value("max", nlohmann::json{})),
                parameter.maxValues);
            parameter.speed = parameterNode.value("speed", parameter.speed);
            return parameter;
        }
    }

    void ReadParameterDescriptorsFromJson(
        const nlohmann::json& parameterArray,
        std::vector<ParamDesc>& outParameters) {

        outParameters.clear();
        if (!parameterArray.is_array()) {
            return;
        }

        for (const auto& parameterNode : parameterArray) {
            if (parameterNode.is_object()) {
                outParameters.push_back(
                    ReadParameterDescriptor(parameterNode));
            }
        }
    }

} // namespace HIKARI::VFX::SERIALIZATION
