#include "HIKARI_MaterialSourceJson.h"

#include <algorithm>
#include <exception>
#include <utility>

#include <json.hpp>

#include "Assets/Material/Serialization/HIKARI_MaterialGraphJson.h"
#include "Assets/Material/Serialization/HIKARI_MaterialInstanceJson.h"
#include "Assets/Material/Serialization/HIKARI_MaterialPbrJson.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"

namespace HIKARI::MATERIAL_SERIALIZATION {
bool SerializeMaterialCompileInputJson(const MaterialAssetData& source, std::string& outJson, std::string& outError) {
    MaterialAssetData normalized = source;
    const auto stripParameterUi = [](auto& parameters) {
        for (auto& parameter : parameters) {
            parameter.group.clear();
            parameter.useUiRange = false;
            parameter.uiMinimum = 0.0f;
            parameter.uiMaximum = 1.0f;
        }
    };
    stripParameterUi(normalized.function.inputs);
    stripParameterUi(normalized.function.outputs);
    for (auto* graph : {&normalized.graph, &normalized.function.graph}) {
        stripParameterUi(graph->parameters);
        graph->groups.clear();
        for (auto& node : graph->nodes) {
            node.position = {};
            stripParameterUi(node.functionInputs);
            stripParameterUi(node.functionOutputs);
        }
    }
    return SerializeMaterialSourceJson(normalized, outJson, outError);
}

namespace {

namespace JsonMath = SERIALIZATION::JSON::MATH;

bool ReadParameterArray(const nlohmann::json& parent, const char* key,
                        std::vector<MaterialGraphParameterData>& outValues, std::string& outError) {
    outValues.clear();
    const auto array = parent.find(key);
    if (array == parent.end()) {
        return true;
    }
    if (!array->is_array()) {
        outError = std::string("material function field must be an array: ") + key;
        return false;
    }
    for (const nlohmann::json& item : *array) {
        MaterialGraphParameterData value{};
        if (!ReadMaterialParameterJson(item, value, outError)) {
            return false;
        }
        outValues.push_back(std::move(value));
    }
    return true;
}

nlohmann::json WriteParameterArray(std::vector<MaterialGraphParameterData> values) {
    std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    nlohmann::json array = nlohmann::json::array();
    for (const auto& value : values) {
        array.push_back(WriteMaterialParameterJson(value));
    }
    return array;
}

bool ReadGuidArray(const nlohmann::json& parent, const char* key, std::vector<AssetGuid>& outValues,
                   std::string& outError) {
    outValues.clear();
    const auto array = parent.find(key);
    if (array == parent.end()) {
        return true;
    }
    if (!array->is_array()) {
        outError = std::string("material GUID field must be an array: ") + key;
        return false;
    }
    for (const nlohmann::json& item : *array) {
        if (!item.is_string()) {
            outError = std::string("material GUID entry must be a string: ") + key;
            return false;
        }
        outValues.push_back(AssetGuid{item.get<std::string>()});
    }
    return true;
}

nlohmann::json WriteGuidArray(std::vector<AssetGuid> values) {
    std::sort(values.begin(), values.end(),
              [](const auto& left, const auto& right) { return left.value < right.value; });
    nlohmann::json array = nlohmann::json::array();
    for (const AssetGuid& value : values) {
        array.push_back(value.value);
    }
    return array;
}

bool ReadFeatureRequests(const nlohmann::json& parent, const char* key, std::vector<MaterialFeatureRequest>& outValues,
                         std::string& outError) {
    outValues.clear();
    const auto values = parent.find(key);
    if (values == parent.end()) {
        return true;
    }
    if (!values->is_array()) {
        outError = std::string("material feature field must be an array: ") + key;
        return false;
    }
    for (const nlohmann::json& item : *values) {
        MaterialFeatureRequest request{};
        if (!item.is_string() || !TryParseMaterialFeatureRequest(item.get<std::string>(), request)) {
            outError = "unknown material Render Feature request";
            return false;
        }
        outValues.push_back(request);
    }
    return true;
}

nlohmann::json WriteFeatureRequests(std::vector<MaterialFeatureRequest> values) {
    std::sort(values.begin(), values.end(),
              [](const auto left, const auto right) { return ToString(left) < ToString(right); });
    values.erase(std::unique(values.begin(), values.end()), values.end());
    nlohmann::json array = nlohmann::json::array();
    for (const MaterialFeatureRequest value : values) {
        array.push_back(std::string(ToString(value)));
    }
    return array;
}

bool ReadProfile(const nlohmann::json& node, MaterialProfileData& outProfile, std::string& outError) {
    if (!node.is_object()) {
        outError = "material profile section is missing";
        return false;
    }
    MaterialProfileData profile{};
    if (!TryParseMaterialTemplateId(node.value("baseTemplate", std::string{"GenericToon"}), profile.baseTemplate) ||
        !ReadGuidArray(node, "functionAssetGuids", profile.functionAssetGuids, outError) ||
        !ReadGuidArray(node, "rampAssetGuids", profile.rampAssetGuids, outError)) {
        if (outError.empty()) {
            outError = "unknown material profile baseTemplate";
        }
        return false;
    }
    if (!ReadFeatureRequests(node, "featureRequests", profile.featureRequests, outError)) {
        return false;
    }
    outProfile = std::move(profile);
    return true;
}

nlohmann::json WriteProfile(const MaterialProfileData& profile) {
    return nlohmann::json{
        {"baseTemplate", std::string(ToString(profile.baseTemplate))},
        {"functionAssetGuids", WriteGuidArray(profile.functionAssetGuids)},
        {"rampAssetGuids", WriteGuidArray(profile.rampAssetGuids)},
        {"featureRequests", WriteFeatureRequests(profile.featureRequests)},
    };
}

bool ReadRamp(const nlohmann::json& node, MaterialRampData& outRamp, std::string& outError) {
    if (!node.is_object()) {
        outError = "material ramp section is missing";
        return false;
    }
    MaterialRampData ramp{};
    if (!TryParseMaterialRampDimension(node.value("dimension", std::string{"OneD"}), ramp.dimension) ||
        !TryParseMaterialRampInterpolation(node.value("interpolation", std::string{"Linear"}), ramp.interpolation)) {
        outError = "unknown material ramp dimension or interpolation";
        return false;
    }
    ramp.width = node.value("width", ramp.width);
    ramp.height = node.value("height", ramp.height);
    if (const auto points = node.find("points"); points != node.end()) {
        if (!points->is_array()) {
            outError = "material ramp points must be an array";
            return false;
        }
        for (const nlohmann::json& item : *points) {
            MaterialRampPointData point{};
            point.id = item.value("id", std::string{});
            point.position = JSONREAD::Vec2Or(item.value("position", nlohmann::json::array()), point.position);
            point.color = JSONREAD::Vec4Or(item.value("color", nlohmann::json::array()), point.color);
            ramp.points.push_back(std::move(point));
        }
    }
    outRamp = std::move(ramp);
    return true;
}

nlohmann::json WriteRamp(const MaterialRampData& ramp) {
    auto points = ramp.points;
    std::sort(points.begin(), points.end(), [](const auto& left, const auto& right) {
        if (left.position.y != right.position.y) {
            return left.position.y < right.position.y;
        }
        if (left.position.x != right.position.x) {
            return left.position.x < right.position.x;
        }
        return left.id < right.id;
    });
    nlohmann::json pointArray = nlohmann::json::array();
    for (const auto& point : points) {
        pointArray.push_back(nlohmann::json{
            {"id", point.id},
            {"position", JsonMath::ToJsonArray(point.position)},
            {"color", JsonMath::ToJsonArray(point.color)},
        });
    }
    return nlohmann::json{
        {"dimension", std::string(ToString(ramp.dimension))},
        {"interpolation", std::string(ToString(ramp.interpolation))},
        {"width", ramp.width},
        {"height", ramp.height},
        {"points", std::move(pointArray)},
    };
}

bool ParseCurrent(const nlohmann::json& root, MaterialAssetData& outData, std::string& outError) {
    MaterialAssetData data{};
    data.version = kMaterialAssetSourceVersion;
    data.materialName = root.value("materialName", data.materialName);
    if (!TryParseMaterialAssetKind(root.value("kind", std::string{}), data.kind) ||
        !TryParseMaterialDomain(root.value("domain", std::string{"Surface"}), data.domain) ||
        !TryParseMaterialTemplateId(root.value("template", std::string{"StandardPBR"}), data.templateId)) {
        outError = "unknown Material kind, domain, or template";
        return false;
    }
    data.profileAssetGuid.value = root.value("profileAssetGuid", std::string{});
    if (!ReadFeatureRequests(root, "featureRequests", data.featureRequests, outError)) {
        return false;
    }

    if (const auto surface = root.find("surface"); surface != root.end() && surface->is_object()) {
        if (!TryParseMaterialAlphaMode(surface->value("alphaMode", std::string{"Opaque"}), data.surface.alphaMode)) {
            outError = "unknown Material alpha mode";
            return false;
        }
        if (!TryParseMaterialAlphaRepresentation(surface->value("alphaRepresentation", std::string{"Straight"}),
                                                 data.surface.alphaRepresentation)) {
            outError = "unknown Material alpha representation";
            return false;
        }
        data.surface.alphaCutoff = surface->value("alphaCutoff", data.surface.alphaCutoff);
        data.surface.doubleSided = surface->value("doubleSided", data.surface.doubleSided);
        data.surface.unlit = surface->value("unlit", data.surface.unlit);
    }
    data.pbr = ReadPbrMaterialJson(root.value("pbr", nlohmann::json::object()), data.materialName);
    data.pbr.doubleSided = data.surface.doubleSided;
    data.pbr.unlit = data.surface.unlit;

    switch (data.kind) {
    case MaterialAssetKind::Material:
        if (!ReadMaterialGraphJson(root.value("graph", nlohmann::json{}), data.graph, outError)) {
            return false;
        }
        break;
    case MaterialAssetKind::Function: {
        const nlohmann::json function = root.value("function", nlohmann::json{});
        if (!function.is_object() || !ReadParameterArray(function, "inputs", data.function.inputs, outError) ||
            !ReadParameterArray(function, "outputs", data.function.outputs, outError) ||
            !ReadMaterialGraphJson(function.value("graph", nlohmann::json{}), data.function.graph, outError)) {
            if (outError.empty()) {
                outError = "material function section is invalid";
            }
            return false;
        }
        break;
    }
    case MaterialAssetKind::Instance:
        if (!ReadMaterialInstanceJson(root.value("instance", nlohmann::json{}), data.instance, outError)) {
            return false;
        }
        break;
    case MaterialAssetKind::Profile:
        if (!ReadProfile(root.value("profile", nlohmann::json{}), data.profile, outError)) {
            return false;
        }
        break;
    case MaterialAssetKind::Ramp:
        if (!ReadRamp(root.value("ramp", nlohmann::json{}), data.ramp, outError)) {
            return false;
        }
        break;
    }
    outData = std::move(data);
    return true;
}

nlohmann::json ToJson(const MaterialAssetData& data) {
    nlohmann::json root{
        {"version", kMaterialAssetSourceVersion},
        {"kind", std::string(ToString(data.kind))},
        {"materialName", data.materialName},
        {"domain", std::string(ToString(data.domain))},
        {"template", std::string(ToString(data.templateId))},
        {"profileAssetGuid", data.profileAssetGuid.value},
        {"featureRequests", WriteFeatureRequests(data.featureRequests)},
        {"surface",
         {
             {"alphaMode", std::string(ToString(data.surface.alphaMode))},
             {"alphaRepresentation", std::string(ToString(data.surface.alphaRepresentation))},
             {"alphaCutoff", data.surface.alphaCutoff},
             {"doubleSided", data.surface.doubleSided},
             {"unlit", data.surface.unlit},
         }},
        {"pbr", WritePbrMaterialJson(ProjectPbrMaterialAssetData(data))},
    };
    switch (data.kind) {
    case MaterialAssetKind::Material: root["graph"] = WriteMaterialGraphJson(data.graph); break;
    case MaterialAssetKind::Function:
        root["function"] = nlohmann::json{
            {"inputs", WriteParameterArray(data.function.inputs)},
            {"outputs", WriteParameterArray(data.function.outputs)},
            {"graph", WriteMaterialGraphJson(data.function.graph)},
        };
        break;
    case MaterialAssetKind::Instance: root["instance"] = WriteMaterialInstanceJson(data.instance); break;
    case MaterialAssetKind::Profile: root["profile"] = WriteProfile(data.profile); break;
    case MaterialAssetKind::Ramp: root["ramp"] = WriteRamp(data.ramp); break;
    }
    return root;
}

} // namespace

bool ParseMaterialSourceJson(std::string_view jsonText, MaterialAssetData& outData, std::string& outError,
                             std::vector<std::string>* outDiagnostics) {
    outError.clear();
    if (outDiagnostics) {
        outDiagnostics->clear();
    }
    try {
        nlohmann::json root = nlohmann::json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            outError = "invalid material JSON document";
            return false;
        }
        const uint32_t version = root.value("version", 0u);
        if (version != kMaterialAssetSourceVersion) {
            outError = "unsupported material source version " + std::to_string(version) +
                       " (supported: " + std::to_string(kMaterialAssetSourceVersion) + ")";
            return false;
        }
        return ParseCurrent(root, outData, outError);
    } catch (const std::exception& exception) {
        outError = "material JSON schema error: " + std::string(exception.what());
        return false;
    }
}

bool SerializeMaterialSourceJson(const MaterialAssetData& data, std::string& outJsonText, std::string& outError) {
    try {
        outJsonText = ToJson(data).dump(2);
        outError.clear();
        return true;
    } catch (const std::exception& exception) {
        outError = "failed to serialize Material JSON: " + std::string(exception.what());
        return false;
    }
}

bool LoadMaterialSourceJson(const std::filesystem::path& path, MaterialAssetData& outData, std::string& outError,
                            std::vector<std::string>* outDiagnostics) {
    nlohmann::json root{};
    if (!SERIALIZATION::JSON::ReadJsonFile(path, root, &outError)) {
        return false;
    }
    return ParseMaterialSourceJson(root.dump(), outData, outError, outDiagnostics);
}

bool SaveMaterialSourceJson(const std::filesystem::path& path, const MaterialAssetData& data, std::string& outError) {
    try {
        return SERIALIZATION::JSON::WriteJsonFile(path, ToJson(data), &outError);
    } catch (const std::exception& exception) {
        outError = "failed to save Material JSON: " + std::string(exception.what());
        return false;
    }
}

} // namespace HIKARI::MATERIAL_SERIALIZATION
