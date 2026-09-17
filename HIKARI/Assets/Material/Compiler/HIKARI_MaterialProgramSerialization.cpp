#include "HIKARI_MaterialProgramSerialization.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

#include <json.hpp>

#include "Assets/Material/Serialization/HIKARI_MaterialGraphJson.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"

namespace HIKARI::MATERIAL_SERIALIZATION {

    namespace {

        namespace JsonMath = SERIALIZATION::JSON::MATH;

        std::string_view DiagnosticSeverityToString(
            MaterialCompileDiagnosticSeverity severity) noexcept {
            switch (severity) {
            case MaterialCompileDiagnosticSeverity::Info: return "Info";
            case MaterialCompileDiagnosticSeverity::Warning: return "Warning";
            case MaterialCompileDiagnosticSeverity::Error: return "Error";
            }
            return "Error";
        }

        bool ParseSeverity(
            std::string_view text,
            MaterialCompileDiagnosticSeverity& outSeverity) noexcept {

            if (text == "Info") {
                outSeverity = MaterialCompileDiagnosticSeverity::Info;
                return true;
            }
            if (text == "Warning") {
                outSeverity = MaterialCompileDiagnosticSeverity::Warning;
                return true;
            }
            if (text == "Error") {
                outSeverity = MaterialCompileDiagnosticSeverity::Error;
                return true;
            }
            return false;
        }

        nlohmann::json WriteProgram(const MaterialProgramIR& program) {
            auto parameters = program.parameters;
            auto resources = program.resources;
            auto dependencies = program.dependencyGuids;
            auto features = program.featureRequests;
            std::sort(parameters.begin(), parameters.end(), [](const auto& left, const auto& right) {
                return left.parameterId < right.parameterId;
            });
            std::sort(resources.begin(), resources.end(), [](const auto& left, const auto& right) {
                return left.resourceId < right.resourceId;
            });
            std::sort(dependencies.begin(), dependencies.end(), [](const auto& left, const auto& right) {
                return left.value < right.value;
            });
            std::sort(features.begin(), features.end(), [](const auto left, const auto right) {
                return ToString(left) < ToString(right);
            });

            nlohmann::json parameterArray = nlohmann::json::array();
            for (const auto& parameter : parameters) {
                nlohmann::json parameterJson{
                    { "parameterId", parameter.parameterId },
                    { "type", std::string(ToString(parameter.type)) },
                    { "byteOffset", parameter.byteOffset },
                    { "byteSize", parameter.byteSize },
                    { "defaultValue", WriteMaterialValueJson(parameter.defaultValue) },
                };
                if (!parameter.externalBindingId.empty()) {
                    parameterJson["externalBindingId"] =
                        parameter.externalBindingId;
                }
                parameterArray.push_back(std::move(parameterJson));
            }
            nlohmann::json resourceArray = nlohmann::json::array();
            for (const auto& resource : resources) {
                resourceArray.push_back(nlohmann::json{
                    { "resourceId", resource.resourceId },
                    { "type", std::string(ToString(resource.type)) },
                    { "semantic", std::string(ToString(resource.semantic)) },
                    { "assetGuid", resource.assetGuid.value },
                    { "resourceIndex", resource.resourceIndex },
                    { "texCoord", resource.texCoord },
                    { "uvScale", JsonMath::ToJsonArray(resource.uvScale) },
                    { "uvOffset", JsonMath::ToJsonArray(resource.uvOffset) },
                    { "uvRotation", resource.uvRotation },
                });
            }
            nlohmann::json instructionArray = nlohmann::json::array();
            for (const MaterialProgramInstruction& instruction :
                program.instructions) {
                nlohmann::json propertyArray = nlohmann::json::array();
                for (const MaterialNodePropertyData& property :
                    instruction.properties) {
                    propertyArray.push_back(nlohmann::json{
                        { "id", property.id },
                        { "value", WriteMaterialValueJson(property.value) },
                    });
                }
                instructionArray.push_back(nlohmann::json{
                    { "opcode", static_cast<uint32_t>(instruction.opcode) },
                    { "valueType", std::string(ToString(instruction.valueType)) },
                    { "inputPins", instruction.inputPins },
                    { "inputRegisters", instruction.inputRegisters },
                    { "outputPins", instruction.outputPins },
                    { "outputRegisters", instruction.outputRegisters },
                    { "bindingIndex", instruction.bindingIndex },
                    { "immediate", WriteMaterialValueJson(instruction.immediate) },
                    { "properties", std::move(propertyArray) },
                    { "assetReference", instruction.assetReference.value },
                    { "sourceNodeId", instruction.sourceNodeId },
                });
            }
            nlohmann::json dependencyArray = nlohmann::json::array();
            for (const AssetGuid& dependency : dependencies) {
                dependencyArray.push_back(dependency.value);
            }
            nlohmann::json featureArray = nlohmann::json::array();
            for (const auto feature : features) {
                featureArray.push_back(std::string(ToString(feature)));
            }

            return nlohmann::json{
                { "version", program.version },
                { "key", {
                    { "structureHash", program.key.structureHash },
                    { "domain", std::string(ToString(program.key.domain)) },
                    { "capabilityMask", program.key.capabilityMask },
                } },
                { "sourceKind", std::string(ToString(program.sourceKind)) },
                { "domain", std::string(ToString(program.domain)) },
                { "template", std::string(ToString(program.templateId)) },
                { "surface", {
                    { "alphaMode", std::string(ToString(program.surface.alphaMode)) },
                    { "alphaRepresentation", std::string(ToString(program.surface.alphaRepresentation)) },
                    { "alphaCutoff", program.surface.alphaCutoff },
                    { "doubleSided", program.surface.doubleSided },
                    { "unlit", program.surface.unlit },
                } },
                { "capabilities", program.capabilities },
                { "featureRequests", std::move(featureArray) },
                { "parameters", std::move(parameterArray) },
                { "resources", std::move(resourceArray) },
                { "instructions", std::move(instructionArray) },
                { "registerCount", program.registerCount },
                { "surfaceOutputRegister", program.surfaceOutputRegister },
                { "dependencyGuids", std::move(dependencyArray) },
                { "parentMaterialGuid", program.parentMaterialGuid.value },
                { "profileAssetGuid", program.profileAssetGuid.value },
                { "programId", program.programId },
            };
        }

        bool ReadProgram(
            const nlohmann::json& node,
            MaterialProgramIR& outProgram,
            std::string& outError) {

            if (!node.is_object()) {
                outError = "compiled MaterialProgram must be an object";
                return false;
            }
            MaterialProgramIR program{};
            const uint32_t storedVersion = node.value("version", 0u);
            if (storedVersion != 3u &&
                storedVersion != 4u &&
                storedVersion != 5u &&
                storedVersion != 6u &&
                storedVersion != kMaterialProgramIrVersion) {
                outError = "unsupported MaterialProgram IR version " +
                    std::to_string(storedVersion) +
                    "; recompile canonical Material v6 source";
                return false;
            }
            program.version = kMaterialProgramIrVersion;
            if (!TryParseMaterialAssetKind(
                    node.value("sourceKind", std::string{}), program.sourceKind) ||
                !TryParseMaterialDomain(
                    node.value("domain", std::string{}), program.domain) ||
                !TryParseMaterialTemplateId(
                    node.value("template", std::string{}), program.templateId)) {
                outError = "compiled MaterialProgram contains an unknown source kind, domain, or template";
                return false;
            }
            const nlohmann::json key = node.value("key", nlohmann::json{});
            program.key.structureHash = key.value("structureHash", uint64_t{ 0u });
            program.key.capabilityMask = key.value("capabilityMask", uint64_t{ 0u });
            if (!TryParseMaterialDomain(
                    key.value("domain", std::string{}), program.key.domain)) {
                outError = "compiled MaterialProgram key has an unknown domain";
                return false;
            }
            const nlohmann::json surface = node.value("surface", nlohmann::json{});
            if (!TryParseMaterialAlphaMode(
                    surface.value("alphaMode", std::string{}),
                    program.surface.alphaMode)) {
                outError = "compiled MaterialProgram has an unknown alpha mode";
                return false;
            }
            if (!TryParseMaterialAlphaRepresentation(
                    surface.value(
                        "alphaRepresentation",
                        std::string{ "Straight" }),
                    program.surface.alphaRepresentation)) {
                outError = "compiled MaterialProgram has an unknown alpha representation";
                return false;
            }
            program.surface.alphaCutoff = surface.value("alphaCutoff", 0.5f);
            program.surface.doubleSided = surface.value("doubleSided", false);
            program.surface.unlit = surface.value("unlit", false);
            program.capabilities = node.value("capabilities", uint64_t{ 0u });
            program.parentMaterialGuid.value =
                node.value("parentMaterialGuid", std::string{});
            program.profileAssetGuid.value =
                node.value("profileAssetGuid", std::string{});
            program.programId = node.value("programId", std::string{});

            if (const auto features = node.find("featureRequests"); features != node.end()) {
                if (!features->is_array()) {
                    outError = "compiled MaterialProgram featureRequests must be an array";
                    return false;
                }
                for (const nlohmann::json& item : *features) {
                    MaterialFeatureRequest feature{};
                    if (!item.is_string() ||
                        !TryParseMaterialFeatureRequest(item.get<std::string>(), feature)) {
                        outError = "compiled MaterialProgram contains an unknown feature request";
                        return false;
                    }
                    program.featureRequests.push_back(feature);
                }
            }
            if (const auto parameters = node.find("parameters"); parameters != node.end()) {
                if (!parameters->is_array()) {
                    outError = "compiled MaterialProgram parameters must be an array";
                    return false;
                }
                for (const nlohmann::json& item : *parameters) {
                    MaterialProgramParameterBinding parameter{};
                    parameter.parameterId = item.value("parameterId", std::string{});
                    if (!TryParseMaterialValueType(item.value("type", std::string{}), parameter.type) ||
                        !ReadMaterialValueJson(item.value("defaultValue", nlohmann::json{}),
                            parameter.defaultValue, outError)) {
                        if (outError.empty()) {
                            outError = "compiled MaterialProgram contains an unknown parameter type";
                        }
                        return false;
                    }
                    parameter.byteOffset = item.value("byteOffset", 0u);
                    parameter.byteSize = item.value("byteSize", 0u);
                    parameter.externalBindingId = item.value(
                        "externalBindingId", std::string{});
                    program.parameters.push_back(std::move(parameter));
                }
            }
            if (const auto resources = node.find("resources"); resources != node.end()) {
                if (!resources->is_array()) {
                    outError = "compiled MaterialProgram resources must be an array";
                    return false;
                }
                for (const nlohmann::json& item : *resources) {
                    MaterialProgramResourceBinding resource{};
                    resource.resourceId = item.value("resourceId", std::string{});
                    if (!TryParseMaterialResourceType(item.value("type", std::string{}), resource.type) ||
                        !TryParseMaterialResourceSemantic(item.value("semantic", std::string{}),
                            resource.semantic)) {
                        outError = "compiled MaterialProgram contains an unknown resource type or semantic";
                        return false;
                    }
                    resource.assetGuid.value = item.value("assetGuid", std::string{});
                    resource.resourceIndex = item.value("resourceIndex", 0u);
                    resource.texCoord = item.value("texCoord", 0u);
                    resource.uvScale = JSONREAD::Vec2Or(
                        item.value("uvScale", nlohmann::json::array()), resource.uvScale);
                    resource.uvOffset = JSONREAD::Vec2Or(
                        item.value("uvOffset", nlohmann::json::array()), resource.uvOffset);
                    resource.uvRotation = item.value("uvRotation", resource.uvRotation);
                    program.resources.push_back(std::move(resource));
                }
            }
            program.registerCount = node.value("registerCount", 0u);
                program.surfaceOutputRegister = node.value(
                    "surfaceOutputRegister",
                    kInvalidMaterialProgramRegister);
                const auto instructions = node.find("instructions");
                if (instructions == node.end() || !instructions->is_array()) {
                    outError = "compiled MaterialProgram instructions must be an array";
                    return false;
                }
                for (const nlohmann::json& item : *instructions) {
                    MaterialProgramInstruction instruction{};
                    const uint32_t opcode = item.value(
                        "opcode",
                        (std::numeric_limits<uint32_t>::max)());
                    const uint32_t legacyMaximumOpcode = storedVersion == 3u
                        ? static_cast<uint32_t>(
                            MaterialProgramOpcode::CorneaHighlight)
                        : static_cast<uint32_t>(
                            MaterialProgramOpcode::Fresnel);
                    const bool particleTerminal =
                        storedVersion >= 6u &&
                        opcode == static_cast<uint32_t>(
                            MaterialProgramOpcode::ParticleOutput);
                    const bool particleInput = storedVersion >= 7u &&
                        opcode >= static_cast<uint32_t>(MaterialProgramOpcode::ParticleState) &&
                        opcode <= static_cast<uint32_t>(MaterialProgramOpcode::ParticleUv);
                    if ((opcode > legacyMaximumOpcode && !particleTerminal && !particleInput) ||
                        !TryParseMaterialValueType(
                            item.value("valueType", std::string{}),
                            instruction.valueType)) {
                        outError = "compiled MaterialProgram contains an unknown instruction opcode or type";
                        return false;
                    }
                    instruction.opcode =
                        static_cast<MaterialProgramOpcode>(opcode);
                    instruction.inputPins = item.value(
                        "inputPins", std::vector<std::string>{});
                    instruction.inputRegisters = item.value(
                        "inputRegisters", std::vector<uint32_t>{});
                    instruction.outputPins = item.value(
                        "outputPins", std::vector<std::string>{});
                    instruction.outputRegisters = item.value(
                        "outputRegisters", std::vector<uint32_t>{});
                    instruction.bindingIndex = item.value(
                        "bindingIndex", kInvalidMaterialProgramRegister);
                    if (!ReadMaterialValueJson(
                            item.value("immediate", nlohmann::json{}),
                            instruction.immediate,
                            outError)) {
                        return false;
                    }
                    instruction.assetReference.value = item.value(
                        "assetReference", std::string{});
                    instruction.sourceNodeId = item.value(
                        "sourceNodeId", std::string{});
                    if (const auto properties = item.find("properties");
                        properties != item.end()) {
                        if (!properties->is_array()) {
                            outError = "compiled MaterialProgram instruction properties must be an array";
                            return false;
                        }
                        for (const nlohmann::json& propertyNode : *properties) {
                            MaterialNodePropertyData property{};
                            property.id = propertyNode.value("id", std::string{});
                            if (!ReadMaterialValueJson(
                                    propertyNode.value("value", nlohmann::json{}),
                                    property.value,
                                    outError)) {
                                return false;
                            }
                            instruction.properties.push_back(std::move(property));
                        }
                    }
                    program.instructions.push_back(std::move(instruction));
            }
            if (const auto dependencies = node.find("dependencyGuids");
                dependencies != node.end()) {
                if (!dependencies->is_array()) {
                    outError = "compiled MaterialProgram dependencyGuids must be an array";
                    return false;
                }
                for (const nlohmann::json& item : *dependencies) {
                    if (!item.is_string()) {
                        outError = "compiled MaterialProgram dependency GUID must be a string";
                        return false;
                    }
                    program.dependencyGuids.push_back(AssetGuid{ item.get<std::string>() });
                }
            }
            outProgram = std::move(program);
            return true;
        }

    } // namespace

    bool SerializeMaterialCompileResult(
        const MaterialCompileResult& result,
        std::string& outJsonText,
        std::string& outError) {

        try {
            nlohmann::json diagnosticArray = nlohmann::json::array();
            for (const MaterialCompileDiagnostic& diagnostic : result.diagnostics) {
                diagnosticArray.push_back(nlohmann::json{
                    { "severity", std::string(DiagnosticSeverityToString(diagnostic.severity)) },
                    { "code", diagnostic.code },
                    { "message", diagnostic.message },
                    { "subjectId", diagnostic.subjectId },
                });
            }
            const nlohmann::json root{
                { "success", result.success },
                { "usedFallback", result.usedFallback },
                { "program", WriteProgram(result.program) },
                { "diagnostics", std::move(diagnosticArray) },
            };
            outJsonText = root.dump(2);
            outError.clear();
            return true;
        } catch (const std::exception& exception) {
            outError = "failed to serialize MaterialProgram: " +
                std::string(exception.what());
            return false;
        }
    }

    bool ParseMaterialCompileResult(
        std::string_view jsonText,
        MaterialCompileResult& outResult,
        std::string& outError) {

        try {
            const nlohmann::json root = nlohmann::json::parse(
                jsonText.begin(), jsonText.end(), nullptr, false);
            if (root.is_discarded() || !root.is_object()) {
                outError = "invalid compiled MaterialProgram JSON";
                return false;
            }
            MaterialCompileResult result{};
            result.success = root.value("success", false);
            result.usedFallback = root.value("usedFallback", false);
            if (!ReadProgram(root.value("program", nlohmann::json{}),
                    result.program, outError)) {
                return false;
            }
            if (const auto diagnostics = root.find("diagnostics");
                diagnostics != root.end()) {
                if (!diagnostics->is_array()) {
                    outError = "compiled MaterialProgram diagnostics must be an array";
                    return false;
                }
                for (const nlohmann::json& item : *diagnostics) {
                    MaterialCompileDiagnostic diagnostic{};
                    if (!ParseSeverity(item.value("severity", std::string{}),
                            diagnostic.severity)) {
                        outError = "compiled MaterialProgram contains an unknown diagnostic severity";
                        return false;
                    }
                    diagnostic.code = item.value("code", std::string{});
                    diagnostic.message = item.value("message", std::string{});
                    diagnostic.subjectId = item.value("subjectId", std::string{});
                    result.diagnostics.push_back(std::move(diagnostic));
                }
            }
            outResult = std::move(result);
            outError.clear();
            return true;
        } catch (const std::exception& exception) {
            outError = "compiled MaterialProgram schema error: " +
                std::string(exception.what());
            return false;
        }
    }

} // namespace HIKARI::MATERIAL_SERIALIZATION
