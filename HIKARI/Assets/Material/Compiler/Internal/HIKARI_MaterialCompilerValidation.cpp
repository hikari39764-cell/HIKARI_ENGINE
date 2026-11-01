#include "HIKARI_MaterialCompilerValidation.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "RenderContracts/Material/HIKARI_MaterialRampRasterizer.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"

namespace HIKARI::MATERIAL_COMPILER_INTERNAL {

namespace {

void AddDiagnostic(MaterialCompileResult& result, MaterialCompileDiagnosticSeverity severity, std::string code,
                   std::string message, std::string subjectId = {}) {
    result.diagnostics.push_back(
        MaterialCompileDiagnostic{severity, std::move(code), std::move(message), std::move(subjectId)});
}

bool ValidateStableValueList(const std::vector<MaterialGraphParameterData>& values, std::string_view listName,
                             MaterialCompileResult& result) {
    bool valid = true;
    std::unordered_set<std::string> ids{};
    for (const MaterialGraphParameterData& value : values) {
        if (!IsValidMaterialStableId(value.id) || !ids.emplace(value.id).second) {
            AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error, "Material.InvalidFunctionPort",
                          std::string(listName) + " IDs must be stable and unique", value.id);
            valid = false;
        }
    }
    return valid;
}

bool AppendGraphDiagnostics(const MaterialGraphData& graph, bool requireDomainOutput, MaterialDomain domain,
                            MaterialCompileResult& result) {
    std::vector<MaterialGraphDiagnostic> graphDiagnostics{};
    const bool graphValid =
        ValidateMaterialGraph(graph, MaterialGraphValidationOptions{requireDomainOutput, domain}, graphDiagnostics);
    for (const MaterialGraphDiagnostic& diagnostic : graphDiagnostics) {
        MaterialCompileDiagnosticSeverity severity = MaterialCompileDiagnosticSeverity::Error;
        if (diagnostic.severity == MaterialGraphDiagnosticSeverity::Info) {
            severity = MaterialCompileDiagnosticSeverity::Info;
        } else if (diagnostic.severity == MaterialGraphDiagnosticSeverity::Warning) {
            severity = MaterialCompileDiagnosticSeverity::Warning;
        }
        AddDiagnostic(result, severity, diagnostic.code, diagnostic.message, diagnostic.subjectId);
    }
    return graphValid;
}

struct RuntimeGraphView {
    std::unordered_map<std::string, const MaterialGraphNodeData*> nodes{};
    std::unordered_map<std::string, const MaterialGraphEdgeData*> inputs{};
    std::unordered_set<std::string> reachable{};
};

std::string InputKey(std::string_view nodeId, std::string_view pin) {
    return std::string{nodeId} + ":" + std::string{pin};
}

RuntimeGraphView BuildRuntimeGraphView(const MaterialGraphData& graph) {
    RuntimeGraphView view{};
    for (const MaterialGraphNodeData& node : graph.nodes) {
        view.nodes.emplace(node.id, &node);
    }
    for (const MaterialGraphEdgeData& edge : graph.edges) {
        view.inputs.emplace(InputKey(edge.targetNodeId, edge.targetPin), &edge);
    }
    std::vector<std::string> pending{};
    if (!graph.outputNodeId.empty()) {
        pending.push_back(graph.outputNodeId);
    }
    while (!pending.empty()) {
        std::string nodeId = std::move(pending.back());
        pending.pop_back();
        if (!view.reachable.emplace(nodeId).second) {
            continue;
        }
        for (const MaterialGraphEdgeData& edge : graph.edges) {
            if (edge.targetNodeId == nodeId) {
                pending.push_back(edge.sourceNodeId);
            }
        }
    }
    return view;
}

const MaterialGraphNodeData* FindRuntimeNode(const RuntimeGraphView& view, std::string_view nodeId) noexcept {
    const auto found = view.nodes.find(std::string{nodeId});
    return found == view.nodes.end() ? nullptr : found->second;
}

const MaterialGraphEdgeData* FindRuntimeInput(const RuntimeGraphView& view, std::string_view nodeId,
                                              std::string_view pin) noexcept {
    const auto found = view.inputs.find(InputKey(nodeId, pin));
    return found == view.inputs.end() ? nullptr : found->second;
}

const MaterialGraphResourceData* FindRuntimeResource(const MaterialGraphData& graph,
                                                     std::string_view resourceId) noexcept {
    const auto found =
        std::find_if(graph.resources.begin(), graph.resources.end(),
                     [resourceId](const MaterialGraphResourceData& resource) { return resource.id == resourceId; });
    return found == graph.resources.end() ? nullptr : &*found;
}

bool HasValidAuthoredResource(const MaterialGraphData& graph, std::string_view resourceId) noexcept {
    const MaterialGraphResourceData* resource = FindRuntimeResource(graph, resourceId);
    return resource != nullptr && resource->enabled && resource->assetGuid.IsValid();
}

std::string_view ResolveRequiredResourceId(const RuntimeGraphView& view, const MaterialGraphNodeData& node) {
    if (node.kind == MaterialNodeKind::TextureSample) {
        return node.referenceId;
    }
    const std::string_view requiredInput = node.kind == MaterialNodeKind::FaceSdf      ? "SDF"
                                           : node.kind == MaterialNodeKind::HairShadow ? "ShadowMask"
                                           : node.kind == MaterialNodeKind::MatCap     ? "Image"
                                                                                       : std::string_view{};
    if (!requiredInput.empty()) {
        if (const MaterialGraphEdgeData* sdf = FindRuntimeInput(view, node.id, requiredInput)) {
            if (const MaterialGraphNodeData* sample = FindRuntimeNode(view, sdf->sourceNodeId);
                sample != nullptr && sample->kind == MaterialNodeKind::TextureSample) {
                return sample->referenceId;
            }
        }
    }
    return {};
}

bool ValidateRequiredResources(const MaterialAssetData& source, const RuntimeGraphView& view,
                               MaterialCompileResult& result) {
    bool valid = true;
    for (const std::string& nodeId : view.reachable) {
        const MaterialGraphNodeData* node = FindRuntimeNode(view, nodeId);
        if (node == nullptr) {
            continue;
        }
        const auto& contract = ASSETS::MATERIAL::GetMaterialNodeSemanticContract(node->kind);
        const bool required = node->kind == MaterialNodeKind::TextureSample ||
                              contract.resourcePolicy == ASSETS::MATERIAL::MaterialNodeResourcePolicy::AuthoredRequired;
        if (!required) {
            continue;
        }
        const std::string_view resourceId = ResolveRequiredResourceId(view, *node);
        if (!resourceId.empty() && HasValidAuthoredResource(source.graph, resourceId)) {
            continue;
        }
        std::string message = contract.failureBehavior.empty() ? "A reachable node requires a valid authored resource."
                                                               : std::string{contract.failureBehavior};
        if (!resourceId.empty()) {
            message += " Required binding: ";
            message += resourceId;
            message += '.';
        }
        AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error, "Material.RequiredResourceMissing",
                      std::move(message), node->id);
        valid = false;
    }
    return valid;
}

bool IsStaticRuntimeExpression(const RuntimeGraphView& view, std::string_view nodeId,
                               std::unordered_set<std::string>& evaluating) {
    const MaterialGraphNodeData* node = FindRuntimeNode(view, nodeId);
    if (node == nullptr || !evaluating.emplace(node->id).second) {
        return false;
    }
    const auto inputIsStatic = [&](std::string_view pin) {
        const MaterialGraphEdgeData* edge = FindRuntimeInput(view, node->id, pin);
        return edge != nullptr && IsStaticRuntimeExpression(view, edge->sourceNodeId, evaluating);
    };

    bool supported = false;
    MaterialNodeKind builtinKind{};
    if (!TryGetBuiltinMaterialNodeKind(node->kind, builtinKind)) {
        evaluating.erase(node->id);
        return false;
    }
    switch (builtinKind) {
    case MaterialNodeKind::Constant:
    case MaterialNodeKind::Parameter:
    case MaterialNodeKind::Variable: supported = true; break;
    case MaterialNodeKind::Add:
    case MaterialNodeKind::Multiply:
    case MaterialNodeKind::Dot: supported = inputIsStatic("A") && inputIsStatic("B"); break;
    case MaterialNodeKind::Lerp: supported = inputIsStatic("A") && inputIsStatic("B") && inputIsStatic("Alpha"); break;
    case MaterialNodeKind::Clamp:
        supported = inputIsStatic("Value") && inputIsStatic("Min") && inputIsStatic("Max");
        break;
    case MaterialNodeKind::Saturate:
    case MaterialNodeKind::OneMinus: supported = inputIsStatic("Value"); break;
    case MaterialNodeKind::Power: supported = inputIsStatic("Base") && inputIsStatic("Exponent"); break;
    default: break;
    }
    evaluating.erase(node->id);
    return supported;
}

bool IsStaticRuntimeExpression(const RuntimeGraphView& view, std::string_view nodeId) {
    std::unordered_set<std::string> evaluating{};
    return IsStaticRuntimeExpression(view, nodeId, evaluating);
}

bool IsTextureRuntimeExpression(const RuntimeGraphView& view, std::string_view nodeId, bool allowNormalUnpack) {
    const MaterialGraphNodeData* node = FindRuntimeNode(view, nodeId);
    if (node == nullptr) {
        return false;
    }
    if (node->kind == MaterialNodeKind::TextureSample) {
        return true;
    }
    if (!allowNormalUnpack || node->kind != MaterialNodeKind::NormalUnpack) {
        return false;
    }
    const MaterialGraphEdgeData* packed = FindRuntimeInput(view, node->id, "PackedNormal");
    const MaterialGraphNodeData* source = packed != nullptr ? FindRuntimeNode(view, packed->sourceNodeId) : nullptr;
    return source != nullptr && source->kind == MaterialNodeKind::TextureSample;
}

bool IsAdditiveRuntimeExpression(const RuntimeGraphView& view, std::string_view nodeId,
                                 std::unordered_set<std::string>& evaluating) {
    const MaterialGraphNodeData* node = FindRuntimeNode(view, nodeId);
    if (node == nullptr || !evaluating.emplace(node->id).second) {
        return false;
    }
    bool supported = node->kind == MaterialNodeKind::FresnelRim || node->kind == MaterialNodeKind::MatCap ||
                     node->kind == MaterialNodeKind::FabricSheen;
    if (node->kind == MaterialNodeKind::Add) {
        const MaterialGraphEdgeData* a = FindRuntimeInput(view, node->id, "A");
        const MaterialGraphEdgeData* b = FindRuntimeInput(view, node->id, "B");
        supported = (a == nullptr || IsAdditiveRuntimeExpression(view, a->sourceNodeId, evaluating)) &&
                    (b == nullptr || IsAdditiveRuntimeExpression(view, b->sourceNodeId, evaluating));
    }
    evaluating.erase(node->id);
    return supported;
}

bool IsAdditiveRuntimeExpression(const RuntimeGraphView& view, std::string_view nodeId) {
    std::unordered_set<std::string> evaluating{};
    return IsAdditiveRuntimeExpression(view, nodeId, evaluating);
}

bool IsAllowedResponseParameterPin(const MaterialNodeTypeId& typeId, std::string_view pin) noexcept {
    MaterialNodeKind kind{};
    if (!TryGetBuiltinMaterialNodeKind(typeId, kind)) {
        return false;
    }
    switch (kind) {
    case MaterialNodeKind::RampDiffuse:
        return pin == "ShadowThreshold" || pin == "ShadowSoftness" || pin == "MidThreshold" || pin == "ShadowTint" ||
               pin == "MidTint" || pin == "LitTint" || pin == "MaskInfluence" || pin == "Intensity" ||
               pin == "DirectGain";
    case MaterialNodeKind::MultiRampDiffuse:
        return pin == "ShadowThreshold" || pin == "ShadowSoftness" || pin == "MidThreshold" || pin == "ShadowTint" ||
               pin == "MidTint" || pin == "LitTint" || pin == "RampRow" || pin == "MaskInfluence" ||
               pin == "RampBlend" || pin == "LayerWeights" || pin == "Intensity" || pin == "DirectGain";
    case MaterialNodeKind::SpecularBand:
        return pin == "Width" || pin == "Softness" || pin == "Intensity" || pin == "Power" || pin == "Tint";
    case MaterialNodeKind::FresnelRim:
        return pin == "Power" || pin == "Width" || pin == "Color" || pin == "Intensity" || pin == "LightAlignment";
    case MaterialNodeKind::AnisotropicHair:
        return pin == "Shift" || pin == "Width" || pin == "PrimaryPower" || pin == "SecondaryPower" ||
               pin == "SecondaryIntensity" || pin == "FlowStrength";
    case MaterialNodeKind::FaceSdf: return pin == "Softness" || pin == "DirectGain";
    case MaterialNodeKind::SkinLighting: return pin == "Wrap" || pin == "Subsurface" || pin == "Tint";
    case MaterialNodeKind::FabricSheen:
        return pin == "Sheen" || pin == "FiberRoughness" || pin == "Power" || pin == "Intensity";
    case MaterialNodeKind::HybridLighting:
        return pin == "DirectDiffuseBlend" || pin == "DirectSpecularBlend" || pin == "IndirectBlend";
    case MaterialNodeKind::Outline: return pin == "Color" || pin == "Thickness";
    case MaterialNodeKind::HairShadow: return pin == "Strength";
    default: return false;
    }
}

bool ValidateRuntimeGraphContract(const MaterialAssetData& source, MaterialCompileResult& result) {
    if (source.kind != MaterialAssetKind::Material) {
        return true;
    }
    bool valid = true;

    const RuntimeGraphView view = BuildRuntimeGraphView(source.graph);
    valid = ValidateRequiredResources(source, view, result) && valid;
    for (const std::string& nodeId : view.reachable) {
        const MaterialGraphNodeData* node = FindRuntimeNode(view, nodeId);
        if (node == nullptr) {
            continue;
        }
        if (node->kind == MaterialNodeKind::FunctionCall) {
            AddDiagnostic(
                result, MaterialCompileDiagnosticSeverity::Error, "Material.RuntimeFunctionUnsupported",
                "Material Function calls are not available in the production Runtime yet; the node cannot be compiled as a silent no-op.",
                node->id);
            valid = false;
            continue;
        }

        if (node->kind == MaterialNodeKind::PbrSurface) {
            for (const MaterialGraphEdgeData& edge : source.graph.edges) {
                if (edge.targetNodeId != node->id) {
                    continue;
                }
                bool supported = false;
                if (edge.targetPin == "BaseColor" || edge.targetPin == "Opacity" || edge.targetPin == "Normal" ||
                    edge.targetPin == "Metallic" || edge.targetPin == "Roughness" || edge.targetPin == "Occlusion" ||
                    edge.targetPin == "Emissive" || edge.targetPin == "Specular" || edge.targetPin == "SpecularColor" ||
                    edge.targetPin == "AlphaClipThreshold") {
                    // The graph catalogue is the source language for
                    // the bounded pixel expression evaluator. Static
                    // projection remains a compatibility fast path.
                    supported = true;
                } else if (edge.targetPin == "DiffuseResponse") {
                    const MaterialGraphNodeData* response = FindRuntimeNode(view, edge.sourceNodeId);
                    supported = response != nullptr && (response->kind == MaterialNodeKind::RampDiffuse ||
                                                        response->kind == MaterialNodeKind::MultiRampDiffuse ||
                                                        response->kind == MaterialNodeKind::FaceSdf ||
                                                        response->kind == MaterialNodeKind::SkinLighting ||
                                                        response->kind == MaterialNodeKind::HybridLighting ||
                                                        response->kind == MaterialNodeKind::HairShadow);
                } else if (edge.targetPin == "SpecularResponse") {
                    const MaterialGraphNodeData* response = FindRuntimeNode(view, edge.sourceNodeId);
                    supported = response != nullptr && (response->kind == MaterialNodeKind::SpecularBand ||
                                                        response->kind == MaterialNodeKind::AnisotropicHair);
                } else if (edge.targetPin == "Eye") {
                    const MaterialGraphNodeData* response = FindRuntimeNode(view, edge.sourceNodeId);
                    supported = response != nullptr && (response->kind == MaterialNodeKind::EyeLighting ||
                                                        response->kind == MaterialNodeKind::CorneaHighlight);
                } else if (edge.targetPin == "Additive") {
                    supported = IsAdditiveRuntimeExpression(view, edge.sourceNodeId);
                } else if (edge.targetPin == "Shell") {
                    const MaterialGraphNodeData* configuration = FindRuntimeNode(view, edge.sourceNodeId);
                    supported = configuration != nullptr && configuration->kind == MaterialNodeKind::Shell;
                } else if (edge.targetPin == "Outline") {
                    const MaterialGraphNodeData* response = FindRuntimeNode(view, edge.sourceNodeId);
                    supported = response != nullptr && response->kind == MaterialNodeKind::Outline;
                }
                if (!supported) {
                    AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error, "Material.RuntimeGraphUnsupported",
                                  "This connection has no equivalent in the bounded production material evaluator.",
                                  edge.id);
                    valid = false;
                }
            }
            continue;
        }

        const bool responseNode =
            node->kind == MaterialNodeKind::RampDiffuse || node->kind == MaterialNodeKind::MultiRampDiffuse ||
            node->kind == MaterialNodeKind::SpecularBand || node->kind == MaterialNodeKind::FresnelRim ||
            node->kind == MaterialNodeKind::MatCap || node->kind == MaterialNodeKind::AnisotropicHair ||
            node->kind == MaterialNodeKind::FaceSdf || node->kind == MaterialNodeKind::EyeLighting ||
            node->kind == MaterialNodeKind::SkinLighting || node->kind == MaterialNodeKind::FabricSheen ||
            node->kind == MaterialNodeKind::HybridLighting || node->kind == MaterialNodeKind::Outline ||
            node->kind == MaterialNodeKind::HairShadow || node->kind == MaterialNodeKind::CorneaHighlight;
        if (!responseNode) {
            continue;
        }
        if (node->kind == MaterialNodeKind::CorneaHighlight && source.surface.alphaMode != MaterialAlphaMode::Blend) {
            AddDiagnostic(
                result, MaterialCompileDiagnosticSeverity::Error, "Material.CorneaHighlightRequiresBlend",
                "Cornea Highlight operates on a transparent authored highlight layer; set Surface Output to Blend.",
                node->id);
            valid = false;
        }
        if ((node->kind == MaterialNodeKind::SkinLighting || node->kind == MaterialNodeKind::HybridLighting ||
             node->kind == MaterialNodeKind::HairShadow) &&
            FindRuntimeInput(view, node->id, "DiffuseResponse") == nullptr) {
            AddDiagnostic(
                result, MaterialCompileDiagnosticSeverity::Error, "Material.RequiredResponseMissing",
                node->kind == MaterialNodeKind::HybridLighting
                    ? "PBR + NPR Hybrid requires an upstream NPR Diffuse Response before its blend controls have a defined meaning."
                : node->kind == MaterialNodeKind::HairShadow ? "Hair Shadow Mask requires an upstream Diffuse Response."
                                                             : "Skin Lighting requires an upstream Diffuse Response.",
                node->id);
            valid = false;
        }
        for (const MaterialGraphEdgeData& edge : source.graph.edges) {
            if (edge.targetNodeId != node->id) {
                continue;
            }
            const MaterialGraphNodeData* inputNode = FindRuntimeNode(view, edge.sourceNodeId);
            const bool responseInput =
                edge.targetPin == "DiffuseResponse" && inputNode != nullptr &&
                (inputNode->kind == MaterialNodeKind::RampDiffuse ||
                 inputNode->kind == MaterialNodeKind::MultiRampDiffuse ||
                 inputNode->kind == MaterialNodeKind::FaceSdf || inputNode->kind == MaterialNodeKind::HairShadow ||
                 inputNode->kind == MaterialNodeKind::SkinLighting ||
                 inputNode->kind == MaterialNodeKind::HybridLighting);
            const auto expectedMaskChannel = [node]() -> std::string_view {
                MaterialNodeKind builtinKind{};
                if (!TryGetBuiltinMaterialNodeKind(node->kind, builtinKind)) {
                    return {};
                }
                switch (builtinKind) {
                case MaterialNodeKind::RampDiffuse:
                case MaterialNodeKind::MultiRampDiffuse:
                case MaterialNodeKind::AnisotropicHair: return "R";
                case MaterialNodeKind::SkinLighting: return "G";
                case MaterialNodeKind::FabricSheen: return "B";
                case MaterialNodeKind::MatCap: return "A";
                case MaterialNodeKind::HairShadow: return "R";
                default: return {};
                }
            }();
            const bool maskInput = edge.targetPin == "Mask" && !expectedMaskChannel.empty() &&
                                   edge.sourcePin == expectedMaskChannel &&
                                   IsTextureRuntimeExpression(view, edge.sourceNodeId, false);
            const bool hairShadowInput = node->kind == MaterialNodeKind::HairShadow && edge.targetPin == "ShadowMask" &&
                                         edge.sourcePin == "R" &&
                                         IsTextureRuntimeExpression(view, edge.sourceNodeId, false);
            const bool faceSdfInput = node->kind == MaterialNodeKind::FaceSdf && edge.targetPin == "SDF" &&
                                      edge.sourcePin == "R" &&
                                      IsTextureRuntimeExpression(view, edge.sourceNodeId, false);
            const bool matCapImageInput = node->kind == MaterialNodeKind::MatCap && edge.targetPin == "Image" &&
                                          edge.sourcePin == "Color" &&
                                          IsTextureRuntimeExpression(view, edge.sourceNodeId, false);
            const bool eyeControlInput = node->kind == MaterialNodeKind::EyeLighting && edge.targetPin == "Control" &&
                                         edge.sourcePin == "Color" &&
                                         IsTextureRuntimeExpression(view, edge.sourceNodeId, false);
            const bool parameterInput = IsAllowedResponseParameterPin(node->kind, edge.targetPin) &&
                                        IsStaticRuntimeExpression(view, edge.sourceNodeId);
            if (!responseInput && !maskInput && !hairShadowInput && !faceSdfInput && !matCapImageInput &&
                !eyeControlInput && !parameterInput) {
                AddDiagnostic(
                    result, MaterialCompileDiagnosticSeverity::Error, "Material.RuntimeResponseInputUnsupported",
                    "This lighting-response input is supplied by the Runtime shading context or is not implemented as a graph input.",
                    edge.id);
                valid = false;
            }
        }
    }
    return valid;
}

} // namespace

bool ValidateMaterialSource(const MaterialAssetData& source, MaterialCompileResult& result) {
    bool valid = true;
    if (source.version != kMaterialAssetSourceVersion) {
        AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error, "Material.UnsupportedVersion",
                      "compiler accepts only canonical Material source data");
        valid = false;
    }
    if (source.materialName.empty()) {
        AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Warning, "Material.EmptyName",
                      "material has no display name");
    }
    if (!std::isfinite(source.surface.alphaCutoff) || source.surface.alphaCutoff < 0.0f ||
        source.surface.alphaCutoff > 1.0f) {
        AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error, "Material.InvalidAlphaCutoff",
                      "alpha cutoff must be finite and between zero and one");
        valid = false;
    }

    switch (source.kind) {
    case MaterialAssetKind::Material:
        valid = AppendGraphDiagnostics(source.graph, true, source.domain, result) && valid;
        if (valid) {
            valid = ValidateRuntimeGraphContract(source, result) && valid;
        }
        break;
    case MaterialAssetKind::Function:
        valid = ValidateStableValueList(source.function.inputs, "function input", result) && valid;
        valid = ValidateStableValueList(source.function.outputs, "function output", result) && valid;
        if (source.function.outputs.empty()) {
            AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error, "Material.FunctionOutputArity",
                          "Material Function requires at least one declared output");
            valid = false;
        }
        valid = AppendGraphDiagnostics(source.function.graph, false, source.domain, result) && valid;
        if (source.function.graph.outputNodeId.empty()) {
            AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error, "Material.FunctionOutputMissing",
                          "Material Function graph requires an outputNodeId");
            valid = false;
        } else {
            const auto outputNode = std::find_if(
                source.function.graph.nodes.begin(), source.function.graph.nodes.end(),
                [&source](const MaterialGraphNodeData& node) { return node.id == source.function.graph.outputNodeId; });
            if (outputNode != source.function.graph.nodes.end()) {
                const std::vector<std::string_view> outputPins = GetMaterialGraphNodeOutputPins(*outputNode);
                const bool outputsMatch =
                    !source.function.outputs.empty() &&
                    std::all_of(source.function.outputs.begin(), source.function.outputs.end(),
                                [&outputPins, &source](const auto& output) {
                                    return std::find(outputPins.begin(), outputPins.end(), output.id) !=
                                               outputPins.end() ||
                                           (source.function.outputs.size() == 1u && outputPins.size() == 1u);
                                });
                if (outputPins.empty() || !outputsMatch) {
                    AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error,
                                  "Material.FunctionOutputPinAmbiguous",
                                  "each Function output ID must name a pin when the function exposes multiple outputs",
                                  outputNode->id);
                    valid = false;
                }
            }
        }
        break;
    case MaterialAssetKind::Instance: {
        if (!source.instance.parentMaterialGuid.IsValid()) {
            AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error, "Material.MissingParent",
                          "material instance requires a parent Material GUID");
            valid = false;
        }
        std::unordered_set<std::string> parameterIds{};
        for (const auto& value : source.instance.parameterOverrides) {
            if (!IsValidMaterialStableId(value.parameterId) || !parameterIds.emplace(value.parameterId).second) {
                AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error, "Material.InvalidInstanceParameter",
                              "instance parameter overrides must have stable unique IDs", value.parameterId);
                valid = false;
            }
        }
        std::unordered_set<std::string> resourceIds{};
        for (const auto& value : source.instance.resourceOverrides) {
            if (!IsValidMaterialStableId(value.resourceId) || !resourceIds.emplace(value.resourceId).second) {
                AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error, "Material.InvalidInstanceResource",
                              "instance resource overrides must have stable unique IDs", value.resourceId);
                valid = false;
            }
        }
        break;
    }
    case MaterialAssetKind::Profile:
        if (source.profile.functionAssetGuids.empty() && source.profile.rampAssetGuids.empty() &&
            source.profile.featureRequests.empty()) {
            AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Warning, "Material.EmptyProfile",
                          "material profile contains no reusable functions, ramps, or features");
        }
        break;
    case MaterialAssetKind::Ramp: {
        const bool oneDimensional = source.ramp.dimension == MaterialRampDimension::OneD;
        const uint32_t maximumResolution = oneDimensional ? ASSETS::MATERIAL::kMaximumMaterialRamp1DResolution
                                                          : ASSETS::MATERIAL::kMaximumMaterialRamp2DResolution;
        if (source.ramp.width < 2u || source.ramp.width > maximumResolution || source.ramp.height < 1u ||
            source.ramp.height > maximumResolution ||
            (source.ramp.dimension == MaterialRampDimension::OneD && source.ramp.height != 1u) ||
            (source.ramp.dimension == MaterialRampDimension::TwoD && source.ramp.height < 2u) ||
            source.ramp.points.size() < 2u || source.ramp.points.size() > 256u) {
            AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error, "Material.InvalidRampShape",
                          "ramp dimensions and point count are outside the bounded v1 contract");
            valid = false;
        }
        std::unordered_set<std::string> pointIds{};
        for (const MaterialRampPointData& point : source.ramp.points) {
            if (!IsValidMaterialStableId(point.id) || !pointIds.emplace(point.id).second ||
                !std::isfinite(point.position.x) || !std::isfinite(point.position.y) || point.position.x < 0.0f ||
                point.position.x > 1.0f || point.position.y < 0.0f || point.position.y > 1.0f) {
                AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error, "Material.InvalidRampPoint",
                              "ramp point IDs and normalized positions must be valid", point.id);
                valid = false;
            }
        }
        break;
    }
    }
    return valid;
}

} // namespace HIKARI::MATERIAL_COMPILER_INTERNAL
