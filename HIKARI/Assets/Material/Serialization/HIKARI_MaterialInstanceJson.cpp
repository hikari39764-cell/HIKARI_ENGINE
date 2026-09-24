#include "HIKARI_MaterialInstanceJson.h"

#include <algorithm>
#include <utility>

#include "Assets/Material/Serialization/HIKARI_MaterialGraphJson.h"

namespace HIKARI::MATERIAL_SERIALIZATION {

bool ReadMaterialInstanceJson(const nlohmann::json& node, MaterialInstanceData& outInstance, std::string& outError) {
    if (!node.is_object()) {
        outError = "material instance section is missing";
        return false;
    }
    MaterialInstanceData instance{};
    instance.parentMaterialGuid.value = node.value("parentMaterialGuid", std::string{});
    const auto parameters = node.find("parameterOverrides");
    if (parameters != node.end()) {
        if (!parameters->is_array()) {
            outError = "material instance parameterOverrides must be an array";
            return false;
        }
        for (const nlohmann::json& item : *parameters) {
            MaterialInstanceParameterOverrideData overrideData{};
            overrideData.parameterId = item.value("parameterId", std::string{});
            if (!ReadMaterialValueJson(item.value("value", nlohmann::json{}), overrideData.value, outError)) {
                return false;
            }
            instance.parameterOverrides.push_back(std::move(overrideData));
        }
    }
    const auto resources = node.find("resourceOverrides");
    if (resources != node.end()) {
        if (!resources->is_array()) {
            outError = "material instance resourceOverrides must be an array";
            return false;
        }
        for (const nlohmann::json& item : *resources) {
            instance.resourceOverrides.push_back(MaterialInstanceResourceOverrideData{
                item.value("resourceId", std::string{}), AssetGuid{item.value("assetGuid", std::string{})}});
        }
    }
    outInstance = std::move(instance);
    return true;
}

nlohmann::json WriteMaterialInstanceJson(const MaterialInstanceData& instance) {
    auto parameters = instance.parameterOverrides;
    auto resources = instance.resourceOverrides;
    std::sort(parameters.begin(), parameters.end(),
              [](const auto& left, const auto& right) { return left.parameterId < right.parameterId; });
    std::sort(resources.begin(), resources.end(),
              [](const auto& left, const auto& right) { return left.resourceId < right.resourceId; });
    nlohmann::json parameterArray = nlohmann::json::array();
    for (const auto& value : parameters) {
        parameterArray.push_back(nlohmann::json{
            {"parameterId", value.parameterId},
            {"value", WriteMaterialValueJson(value.value)},
        });
    }
    nlohmann::json resourceArray = nlohmann::json::array();
    for (const auto& value : resources) {
        resourceArray.push_back(nlohmann::json{
            {"resourceId", value.resourceId},
            {"assetGuid", value.assetGuid.value},
        });
    }
    return nlohmann::json{
        {"parentMaterialGuid", instance.parentMaterialGuid.value},
        {"parameterOverrides", std::move(parameterArray)},
        {"resourceOverrides", std::move(resourceArray)},
    };
}

} // namespace HIKARI::MATERIAL_SERIALIZATION
