#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <string_view>

#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
#include "RenderContracts/Material/HIKARI_MaterialNodeDefinition.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Assets/Material/Authoring/HIKARI_MaterialGraphReuse.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Runtime/Binding/Adapters/HIKARI_RuntimeMaterialValueAdapter.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::DrawMaterialGraphNodePalette(DocumentSceneBase& scene, MaterialAssetData& source,
                                                                    MaterialGraphData& graph, MATH::Vec2 position) {
    constexpr std::array<std::string_view, 4u> categories{"Ready-made Shading", "Surface", "Advanced / Inputs",
                                                          "Advanced / Math & Masks"};
    constexpr std::string_view otherCategory = "Other";
    constexpr std::array<MaterialNodeKind, 9u> commonKinds{
        MaterialNodeKind::Constant, MaterialNodeKind::Parameter,     MaterialNodeKind::Variable,
        MaterialNodeKind::Add,      MaterialNodeKind::Multiply,      MaterialNodeKind::Lerp,
        MaterialNodeKind::Clamp,    MaterialNodeKind::TextureSample, MaterialNodeKind::TextureCoordinate,
    };

    SearchField("##MaterialGraphNodeSearch", TEXT::Localize("material.palette.search").c_str(), graphSearch_.data(),
                graphSearch_.size());
    const std::string searchText = graphSearch_.data();
    const bool searching = !searchText.empty();
    const auto matchesSearch = [&searchText](std::string_view text) {
        if (searchText.empty()) {
            return true;
        }
        std::string folded(text);
        std::string query = searchText;
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        for (char& c : folded) {
            c = lower(c);
        }
        for (char& c : query) {
            c = lower(c);
        }
        return folded.find(query) != std::string::npos;
    };

    const auto acceptsDraggedInput = [&](const MaterialNodeDefinition& definition) {
        if (!graphLinkCreatePending_)
            return true;
        const auto* sourceNode = FindMaterialGraphNode(graph, edgeSourceNodeId_);
        const auto sourceType =
            sourceNode ? ResolveMaterialGraphPinType(graph, *sourceNode, edgeSourcePin_, true) : std::nullopt;
        if (!sourceType)
            return false;
        return std::any_of(definition.inputs.begin(), definition.inputs.end(), [&](const auto& pin) {
            return !pin.runtimeGenerated &&
                   (pin.fixedType == *sourceType ||
                    (IsMaterialNumericType(*sourceType) && IsMaterialNumericType(pin.fixedType) &&
                     (pin.acceptsDynamicNumeric || *sourceType == MaterialValueType::Float1)));
        });
    };
    const auto addNodeByKind = [&](MaterialNodeKind kind, std::string_view label,
                                   MaterialValueType valueType = MaterialValueType::Float1, bool typed = false,
                                   std::string_view referenceId = {}) {
        const MaterialNodeDefinition* definition = FindMaterialNodeDefinition(kind);
        if (definition == nullptr) {
            return;
        }
        MaterialGraphNodeCreateRequest request{};
        request.nodeType = definition->stableName;
        request.displayName = definition->stableName;
        if (!referenceId.empty())
            request.displayName = std::string(label);
        request.position = position;
        request.manualPosition = true;
        if (graphLinkCreatePending_) {
            request.connectSourceNodeId = edgeSourceNodeId_;
            request.connectSourcePin = edgeSourcePin_;
        }
        request.parameterIdPrefix = "parameter.ui_";
        request.parameterDisplayName = "Parameter";
        request.referenceId = referenceId;
        if (typed) {
            request.valueSet = true;
            request.valueX = 1.0f;
            request.valueY = 1.0f;
            request.valueZ = 1.0f;
            request.valueW = 1.0f;
            switch (valueType) {
            case MaterialValueType::Float1: request.valueType = "Float1"; break;
            case MaterialValueType::Float2: request.valueType = "Float2"; break;
            case MaterialValueType::Float3: request.valueType = "Float3"; break;
            case MaterialValueType::Float4: request.valueType = "Float4"; break;
            }
        }
        std::string message;
        std::string code;
        std::vector<std::string> details;
        const bool added = AddMaterialGraphNode(scene, request, message, details, code);
        statusMessage_ = message;
        if (added) {
            graphLinkCreatePending_ = false;
            std::erase(recentGraphNodes_, std::string(definition->stableName));
            recentGraphNodes_.insert(recentGraphNodes_.begin(), std::string(definition->stableName));
            if (recentGraphNodes_.size() > 8)
                recentGraphNodes_.resize(8);
        }
    };

    const auto drawVariableMenu = [&](std::string_view label) {
        if (!ImGui::BeginMenu(label.data())) {
            return;
        }
        size_t compatibleCount = 0u;
        for (const BINDING::VariableDefinition& variable : scene.GetRuntimeBindingStore().GetCatalog().variables) {
            if (!BINDING::MATERIAL_ADAPTER::ToMaterialValueType(variable.defaultValue.type).has_value()) {
                continue;
            }
            const std::string itemLabel = variable.name + "  [" +
                                          std::string(BINDING::ToString(variable.defaultValue.type)) + "]##" +
                                          variable.id;
            if (ImGui::MenuItem(itemLabel.c_str())) {
                addNodeByKind(MaterialNodeKind::Variable, variable.name, MaterialValueType::Float1, false, variable.id);
            }
            ++compatibleCount;
        }
        if (compatibleCount == 0u) {
            ImGui::TextDisabled(TEXT::Localize("editor.ui.no.material.compatible.variables.6ef34e").c_str());
        }
        ImGui::EndMenu();
    };

    const auto drawFunctions = [&]() {
        if (graphLinkCreatePending_)
            return false;
        bool foundFunction = false;
        for (const AssetChoice& choice : materialChoices_) {
            if (!choice.ready || choice.materialKind != MaterialAssetKind::Function || choice.functionOutputs.empty()) {
                continue;
            }
            if (!matchesSearch(choice.label))
                continue;
            foundFunction = true;
            if (!ImGui::MenuItem(choice.label.c_str())) {
                continue;
            }
            MaterialGraphNodeCreateRequest request{};
            request.nodeType = "FunctionCall";
            request.displayName = choice.label;
            request.position = position;
            request.manualPosition = true;
            request.allowFunctionCall = true;
            request.functionAssetGuid = choice.guid;
            request.functionInputs = choice.functionInputs;
            request.functionOutputs = choice.functionOutputs;
            std::string message;
            std::string code;
            std::vector<std::string> details;
            (void)AddMaterialGraphNode(scene, request, message, details, code);
            statusMessage_ = message;
        }
        if (!foundFunction && !searching) {
            ImGui::BeginDisabled();
            (void)ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.no.imported.material.functions.c9a466",
                                                       "No imported Material Functions")
                                      .c_str());
            ImGui::EndDisabled();
        }
        return foundFunction;
    };

    const auto drawModules = [&]() {
        if (graphLinkCreatePending_)
            return false;
        bool found = false;
        for (const auto& module : ASSETS::MATERIAL::GetMaterialGraphModules()) {
            const auto label = TEXT::Localize(module.labelKey);
            if (!matchesSearch(label) && !matchesSearch(module.searchTerms))
                continue;
            found = true;
            if (ImGui::MenuItem(label.c_str()) && operationRegistry_) {
                statusMessage_ = operationRegistry_
                                     ->Execute("material.insert_module",
                                               {{"module", module.id}, {"x", position.x}, {"y", position.y}})
                                     .message;
            }
        }
        return found;
    };
    if (searching) {
        ImGui::SeparatorText(TEXT::Localize("editor.searchResults").c_str());
        size_t shown = 0u;
        for (const MaterialNodeDefinition& definition : GetMaterialNodeDefinitions()) {
            if (!definition.paletteVisible || !acceptsDraggedInput(definition) ||
                !IsMaterialNodeAvailableForDomain(definition.kind, source.domain)) {
                continue;
            }
            const std::string label = MATERIAL_GRAPH_UI::NodeDisplayName(definition.kind);
            if (!matchesSearch(label) && !matchesSearch(definition.stableName)) {
                continue;
            }
            const bool duplicateSingleton = ASSETS::MATERIAL::IsSingletonReadyMadeMaterialNode(definition.kind) &&
                                            std::any_of(graph.nodes.begin(), graph.nodes.end(),
                                                        [&](const auto& node) { return node.kind == definition.kind; });
            ImGui::BeginDisabled(duplicateSingleton);
            if (definition.kind == MaterialNodeKind::Variable) {
                drawVariableMenu(label);
            } else if (ImGui::MenuItem(label.data())) {
                addNodeByKind(definition.kind, label);
            }
            ImGui::EndDisabled();
            ++shown;
        }
        const bool foundFunction = drawFunctions();
        const bool foundModule = drawModules();
        if (shown == 0u && !foundFunction && !foundModule) {
            ImGui::TextDisabled(TEXT::Localize("editor.noMatchingNodes").c_str());
        }
        return;
    }

    if (ImGui::BeginMenu(TEXT::Localize("material.module.menu").c_str())) {
        drawModules();
        ImGui::EndMenu();
    }
    if (!recentGraphNodes_.empty() && ImGui::BeginMenu(TEXT::Localize("material.palette.recent").c_str())) {
        // 挿入後に履歴が更新されても走査中の参照を無効にしない。
        const auto recent = recentGraphNodes_;
        for (const auto& name : recent) {
            const auto* definition = FindMaterialNodeDefinition(std::string_view(name));
            if (definition == nullptr || !acceptsDraggedInput(*definition) ||
                !IsMaterialNodeAvailableForDomain(definition->kind, source.domain))
                continue;
            const auto label = MATERIAL_GRAPH_UI::NodeDisplayName(definition->kind);
            if (definition->kind == MaterialNodeKind::Variable)
                drawVariableMenu(label);
            else if (ImGui::MenuItem(label.c_str()))
                addNodeByKind(definition->kind, label);
        }
        ImGui::EndMenu();
    }
    if (!searching && ImGui::BeginMenu(TEXT::LocalizeWindow("editor.ui.common.7de90a", "Common").c_str())) {
        for (const MaterialNodeKind kind : commonKinds) {
            const MaterialNodeDefinition* definition = FindMaterialNodeDefinition(kind);
            if (definition == nullptr || !definition->paletteVisible || !acceptsDraggedInput(*definition) ||
                !IsMaterialNodeAvailableForDomain(definition->kind, source.domain)) {
                continue;
            }
            const bool duplicateSingleton = ASSETS::MATERIAL::IsSingletonReadyMadeMaterialNode(kind) &&
                                            std::any_of(graph.nodes.begin(), graph.nodes.end(),
                                                        [kind](const auto& node) { return node.kind == kind; });
            if (kind == MaterialNodeKind::Variable) {
                drawVariableMenu(definition->stableName);
                continue;
            }
            if (kind == MaterialNodeKind::Constant || kind == MaterialNodeKind::Parameter) {
                if (ImGui::BeginMenu(MATERIAL_GRAPH_UI::NodeDisplayName(kind).c_str())) {
                    constexpr std::array<MaterialValueType, 4u> types{
                        MaterialValueType::Float1,
                        MaterialValueType::Float2,
                        MaterialValueType::Float3,
                        MaterialValueType::Float4,
                    };
                    constexpr std::array<const char*, 4u> typeNames{
                        "Float1 (1)",
                        "Float2 (1,1)",
                        "Float3 (1,1,1)",
                        "Float4 (1,1,1,1)",
                    };
                    for (size_t index = 0u; index < types.size(); ++index) {
                        if (ImGui::MenuItem(typeNames[index])) {
                            addNodeByKind(kind, std::string(definition->stableName) + " (" + typeNames[index] + ")",
                                          types[index], true);
                        }
                    }
                    ImGui::EndMenu();
                }
                continue;
            }
            ImGui::BeginDisabled(duplicateSingleton);
            if (ImGui::MenuItem(MATERIAL_GRAPH_UI::NodeDisplayName(kind).c_str())) {
                addNodeByKind(kind, definition->stableName);
            }
            ImGui::EndDisabled();
        }
        ImGui::EndMenu();
    }

    const auto drawNodeItem = [&](const MaterialNodeDefinition& definition) {
        const MaterialNodeKind kind = definition.kind;
        if (!acceptsDraggedInput(definition))
            return;
        const bool duplicateSingleton =
            ASSETS::MATERIAL::IsSingletonReadyMadeMaterialNode(kind) &&
            std::any_of(graph.nodes.begin(), graph.nodes.end(), [kind](const auto& node) { return node.kind == kind; });
        ImGui::BeginDisabled(duplicateSingleton);
        const std::string label = MATERIAL_GRAPH_UI::NodeDisplayName(kind);
        if (searching && !matchesSearch(label) && !matchesSearch(definition.stableName)) {
            ImGui::EndDisabled();
            return;
        }
        if (kind == MaterialNodeKind::Variable) {
            drawVariableMenu(label);
        } else if (ImGui::MenuItem(label.data())) {
            addNodeByKind(kind, label);
        }
        ImGui::EndDisabled();
    };
    const auto drawCategory = [&](const std::string_view category) {
        const char* key = category == "Ready-made Shading"        ? "editor.material.category.shading"
                          : category == "Surface"                 ? "editor.material.category.surface"
                          : category == "Advanced / Inputs"       ? "editor.material.category.inputs"
                          : category == "Advanced / Math & Masks" ? "editor.material.category.math"
                                                                  : "editor.material.category.other";
        if (!ImGui::BeginMenu(TEXT::LocalizeWindow(key, category).c_str())) {
            return;
        }
        if (category == "Advanced / Math & Masks") {
            std::map<std::string_view, std::vector<const MaterialNodeDefinition*>> groups;
            for (const MaterialNodeDefinition& definition : GetMaterialNodeDefinitions()) {
                if (!definition.paletteVisible || !IsMaterialNodeAvailableForDomain(definition.kind, source.domain) ||
                    MATERIAL_GRAPH_UI::NodeCategory(definition.kind) != category) {
                    continue;
                }
                groups[MATERIAL_GRAPH_UI::NodeMathSubgroup(definition.kind)].push_back(&definition);
            }
            for (const auto& [group, definitions] : groups) {
                const bool nested = !group.empty() && definitions.size() > 1u;
                if (nested && !ImGui::BeginMenu(group.data())) {
                    continue;
                }
                for (const MaterialNodeDefinition* definition : definitions) {
                    drawNodeItem(*definition);
                }
                if (nested) {
                    ImGui::EndMenu();
                }
            }
        } else {
            for (const MaterialNodeDefinition& definition : GetMaterialNodeDefinitions()) {
                if (!definition.paletteVisible || !IsMaterialNodeAvailableForDomain(definition.kind, source.domain) ||
                    MATERIAL_GRAPH_UI::NodeCategory(definition.kind) != category) {
                    continue;
                }
                drawNodeItem(definition);
            }
        }
        ImGui::EndMenu();
    };
    for (const std::string_view category : categories) {
        drawCategory(category);
    }
    drawCategory(otherCategory);

    if (ImGui::BeginMenu(TEXT::Localize("editor.ui.material.functions.c794e3").c_str())) {
        drawFunctions();
        ImGui::EndMenu();
    }
}

} // namespace HIKARI::EDITOR
