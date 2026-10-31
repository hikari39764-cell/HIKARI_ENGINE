#include "Assets/Material/Authoring/HIKARI_MaterialGraphInspection.h"
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialOperations.h"
#include "Assets/Material/Authoring/HIKARI_MaterialGraphReuse.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphQuery.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"
#include "Assets/Material/HIKARI_MaterialBuiltInAssets.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceDocument.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace HIKARI;
using namespace HIKARI::EDITOR;
using namespace HIKARI::ASSETS::MATERIAL;
namespace UI = HIKARI::EDITOR::MATERIAL_GRAPH_UI;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(#x);                                                                              \
    } while (false)
// Exercise the production controller without starting an editor or GPU device.
template <class Tag, typename Tag::Type Member>
struct TestAccess {
    friend typename Tag::Type Access(Tag) { return Member; }
};
using Controller = ModelMaterialWorkspaceController;
struct DocumentTag {
    using Type = ModelMaterialWorkspaceDocument Controller::*;
    friend Type Access(DocumentTag);
};
template struct TestAccess<DocumentTag, &Controller::authoringDocument_>;
struct SelectTag {
    using Type = void (Controller::*)(const std::string&, bool);
    friend Type Access(SelectTag);
};
template struct TestAccess<SelectTag, &Controller::SetGraphNodeSelection>;
struct DrawTag {
    using Type = void (Controller::*)(DocumentSceneBase&);
    friend Type Access(DrawTag);
};
template struct TestAccess<DrawTag, &Controller::DrawGraphWindow>;
struct EditTag {
    using Type = MaterialAssetData& (ModelMaterialWorkspaceDocument::*)() noexcept;
    friend Type Access(EditTag);
};
template struct TestAccess<EditTag, &ModelMaterialWorkspaceDocument::EditMaterial>;
namespace {
std::string Json(const MaterialAssetData& source, bool compileOnly = false) {
    std::string text, error;
    CHECK(compileOnly ? MATERIAL_SERIALIZATION::SerializeMaterialCompileInputJson(source, text, error)
                      : MATERIAL_SERIALIZATION::SerializeMaterialSourceJson(source, text, error));
    return text;
}
void RequireCompile(const MaterialCompileResult& compile) {
    for (const auto& d : compile.diagnostics)
        if (!compile.success)
            std::cerr << d.code << ": " << d.message << '\n';
    CHECK(compile.success);
}
void PainterlyTemplateContracts() {
    auto source = CreateMaterialFromTemplate(MaterialTemplateId::PainterlyShell);
    auto compile = CompileMaterialAsset(source);
    RequireCompile(compile);
    // 筆触の殻は固定経路。画素ごとの式は持たず、Shell ノードの uniform と 2 枚の筆触テクスチャだけを運ぶ。
    CHECK(!RENDER3D::MATERIAL::MaterialProgramHasRuntimeExpression(compile.program));
    CHECK((compile.program.capabilities & MaterialCapabilityBit(MaterialProgramCapability::Shell)) != 0u);
    CHECK(compile.program.shell.maxLayers == 4u);
    CHECK(compile.program.shell == [] {
        MaterialShellSettings expected{};
        expected.maxLayers = 4u;
        return expected;
    }());
    CHECK(source.surface.alphaMode == MaterialAlphaMode::Mask);
    CHECK(FindMaterialGraphResource(source.graph, "resource.strokeMask")->assetGuid.value ==
          kBuiltInPainterlyStrokeMaskTextureId);
    CHECK(FindMaterialGraphResource(source.graph, "resource.strokeNormal")->assetGuid.value ==
          kBuiltInPainterlyStrokeNormalTextureId);
    const auto hasStrokeResources = [](const MaterialProgramIR& program) {
        return std::any_of(
                   program.resources.begin(), program.resources.end(),
                   [](const auto& resource) { return resource.semantic == MaterialResourceSemantic::StrokeMask; }) &&
               std::any_of(program.resources.begin(), program.resources.end(), [](const auto& resource) {
                   return resource.semantic == MaterialResourceSemantic::StrokeNormal;
               });
    };
    CHECK(hasStrokeResources(compile.program));
    CHECK(std::none_of(source.graph.nodes.begin(), source.graph.nodes.end(),
                       [](const auto& node) { return node.kind == MaterialNodeKind::TextureSample; }));
    PbrMaterialAssetData imported{};
    imported.baseColorTexture.useTexture = true;
    imported.baseColorTexture.textureAssetGuid = {"test.imported.albedo"};
    imported.normalTexture.useTexture = true;
    imported.normalTexture.textureAssetGuid = {"test.imported.normal"};
    imported.normalScale = 0.35f;
    auto seeded = CreateMaterialFromTemplateWithPbr(MaterialTemplateId::PainterlyShell, imported);
    const auto seededCompile = CompileMaterialAsset(seeded);
    RequireCompile(seededCompile);
    CHECK(hasStrokeResources(seededCompile.program));
    const auto reachable = CollectMaterialGraphUpstreamNodes(seeded.graph, seeded.graph.outputNodeId);
    CHECK(reachable.contains("node.texture.baseColor") && reachable.contains("node.normalUnpack"));
    CHECK(FindMaterialGraphParameter(seeded.graph, "parameter.normalScale")->defaultValue.numeric.x ==
          imported.normalScale);
    MaterialValueData tiling{};
    tiling.numeric.x = 3.0f;
    std::string tilingError;
    CHECK(SetMaterialParameterValue(seeded, "parameter.strokeTiling", tiling, tilingError));
    CHECK(CompileMaterialAsset(seeded).program.shell.strokeTiling == 3.0f);
    ModelMaterialWorkspaceDocument document;
    document.OpenMaterial({"painterly"}, {}, source, compile, true);
    document.ConsumeMaterialPreviewChange();
    auto changed = document.GetMaterial();
    MaterialValueData value{};
    value.numeric.x = 2.0f;
    std::string error;
    CHECK(SetMaterialParameterValue(changed, "parameter.shellMaxLayers", value, error));
    CHECK(document.ApplyMaterial(changed, "Reduce shell layers"));
    CHECK(document.GetCompileResult().program.shell.maxLayers == 2u && document.ConsumeMaterialPreviewChange());
    ModelMaterialWorkspaceDocument::ChangedDomain domain;
    CHECK(document.Undo(domain, error) && !document.IsMaterialDirty());
    CHECK(document.GetCompileResult().program.shell.maxLayers == 4u);
    CHECK(document.Redo(domain, error) && document.GetCompileResult().program.shell.maxLayers == 2u);
    MaterialAssetData restored{};
    CHECK(MATERIAL_SERIALIZATION::ParseMaterialSourceJson(Json(document.GetMaterial()), restored, error));
    CHECK(Json(restored) == Json(document.GetMaterial()));
    MaterialAssetData instance{};
    instance.kind = MaterialAssetKind::Instance;
    instance.instance.parentMaterialGuid = {"painterly"};
    value.numeric.x = 0.0f;
    CHECK(SetMaterialParameterValue(instance, "parameter.shellMaxLayers", value, error, &source));
    const auto resolved =
        ResolveMaterialInstance(instance, [&](const AssetGuid& guid, MaterialAssetData& parent, std::string&) {
            CHECK(guid.value == "painterly");
            parent = source;
            return true;
        });
    CHECK(resolved.success && resolved.compile.program.shell.maxLayers == 0u);
    CHECK(std::any_of(
        resolved.compile.program.resources.begin(), resolved.compile.program.resources.end(),
        [](const auto& resource) { return resource.assetGuid.value == kBuiltInPainterlyStrokeNormalTextureId; }));
}
void LayoutContracts() {
    auto material = CreateMaterialFromTemplate(MaterialTemplateId::StandardPbr);
    auto& graph = material.graph;
    // Different heights, groups, isolated nodes and a shared dependency.
    auto fragment = CreateMaterialGraphModule("radial_mask");
    MaterialGraphGroupData group;
    group.id = "radial";
    for (auto& n : fragment.nodes)
        group.nodeIds.push_back(n.id);
    graph.nodes.insert(graph.nodes.end(), fragment.nodes.begin(), fragment.nodes.end());
    graph.edges.insert(graph.edges.end(), fragment.edges.begin(), fragment.edges.end());
    graph.parameters.insert(graph.parameters.end(), fragment.parameters.begin(), fragment.parameters.end());
    graph.groups.push_back(group);
    const auto semanticBefore = Json(material, true);
    CHECK(UI::AutoLayoutMaterialGraph(graph));
    CHECK(Json(material, true) == semanticBefore);
    for (size_t i = 0; i < graph.nodes.size(); ++i) {
        const auto& a = graph.nodes[i];
        const auto as = UI::NodeLayoutSize(graph, a);
        CHECK(std::isfinite(a.position.x) && std::isfinite(a.position.y));
        for (size_t j = i + 1; j < graph.nodes.size(); ++j) {
            const auto& b = graph.nodes[j];
            const auto bs = UI::NodeLayoutSize(graph, b);
            CHECK(a.position.x + as.x <= b.position.x || b.position.x + bs.x <= a.position.x ||
                  a.position.y + as.y <= b.position.y || b.position.y + bs.y <= a.position.y);
        }
    }
    const auto arranged = Json(material);
    CHECK(!UI::AutoLayoutMaterialGraph(graph));
    CHECK(Json(material) == arranged);
    auto reversed = graph;
    std::reverse(reversed.nodes.begin(), reversed.nodes.end());
    std::reverse(reversed.edges.begin(), reversed.edges.end());
    UI::AutoLayoutMaterialGraph(reversed);
    for (const auto& node : graph.nodes) {
        const auto* other = FindMaterialGraphNode(reversed, node.id);
        CHECK(other->position.x == node.position.x && other->position.y == node.position.y);
    }
    auto& selected = graph.nodes.back();
    const auto selectedId = selected.id;
    selected.position = graph.nodes.front().position;
    const auto outside = graph.nodes.front().position;
    CHECK(UI::AutoLayoutMaterialGraph(graph, {selectedId}));
    CHECK(graph.nodes.front().position.x == outside.x && graph.nodes.front().position.y == outside.y);
    CHECK(!UI::AutoLayoutMaterialGraph(graph, {selectedId}));
    // Missing edge references and cycles never hang or create nonfinite positions.
    graph.edges.push_back({"bad", "missing", "Value", selectedId, "A"});
    graph.edges.push_back({"cycleA", graph.nodes[0].id, "Value", graph.nodes[1].id, "A"});
    graph.edges.push_back({"cycleB", graph.nodes[1].id, "Value", graph.nodes[0].id, "A"});
    UI::AutoLayoutMaterialGraph(graph);
    for (const auto& n : graph.nodes)
        CHECK(std::isfinite(n.position.x) && std::isfinite(n.position.y));
}
void DocumentContracts() {
    auto source = CreateMaterialFromTemplate(MaterialTemplateId::StandardPbr);
    MaterialAssetData parsed;
    std::string error;
    CHECK(MATERIAL_SERIALIZATION::ParseMaterialSourceJson(Json(source), parsed, error));
    ModelMaterialWorkspaceDocument document;
    auto compile = CompileMaterialAsset(source);
    RequireCompile(compile);
    document.OpenMaterial({"test"}, {}, source, compile, true);
    CHECK(document.ConsumeMaterialPreviewChange());
    const auto originalProgram = compile.program.programId;
    auto before = document.GetMaterial();
    before.graph.nodes.front().position = {37, 81};
    CHECK(document.ApplyMaterial(before, "Move"));
    CHECK(document.IsMaterialDirty());
    CHECK(document.IsCompileCurrent());
    CHECK(!document.ConsumeMaterialPreviewChange());
    CHECK(document.GetCompileResult().program.programId == originalProgram);
    CHECK(CompileMaterialAsset(document.GetMaterial()).program.programId == originalProgram);
    auto laidOut = document.GetMaterial();
    CHECK(!document.ApplyMaterial(laidOut, "No-op"));
    ModelMaterialWorkspaceDocument::ChangedDomain domain;
    std::string label;
    CHECK(document.Undo(domain, label));
    CHECK(domain == ModelMaterialWorkspaceDocument::ChangedDomain::MaterialLayout);
    CHECK(!document.IsMaterialDirty());
    CHECK(!document.ConsumeMaterialPreviewChange());
    CHECK(document.Redo(domain, label));
    CHECK(Json(document.GetMaterial()) == Json(laidOut));
    before = document.GetMaterial();
    auto& parameter = before.graph.parameters.front();
    parameter.group = "Main";
    parameter.useUiRange = true;
    parameter.uiMinimum = -1;
    parameter.uiMaximum = 3;
    CHECK(document.ApplyMaterial(before, "Range"));
    CHECK(!document.ConsumeMaterialPreviewChange());
    CHECK(MATERIAL_SERIALIZATION::ParseMaterialSourceJson(Json(document.GetMaterial()), parsed, error));
    CHECK(Json(parsed) == Json(document.GetMaterial()));
    before = document.GetMaterial();
    before.graph.parameters.front().defaultValue.numeric.x += 0.15f;
    CHECK(document.ApplyMaterial(before, "Value"));
    CHECK(document.ConsumeMaterialPreviewChange());
    CHECK(document.Undo(domain, label));
    CHECK(domain == ModelMaterialWorkspaceDocument::ChangedDomain::Material);
    CHECK(document.ConsumeMaterialPreviewChange());
    before = document.GetMaterial();
    before.graph.edges.clear();
    CHECK(document.ApplyMaterial(before, "Break graph"));
    CHECK(!document.GetCompileResult().success);
    before = document.GetMaterial();
    before.graph.nodes.front().position.x += 20;
    CHECK(document.ApplyMaterial(before, "Move invalid graph"));
    CHECK(!document.GetCompileResult().success);
    CHECK(document.ConsumeMaterialPreviewChange()); // The later layout edit must not lose the invalidation.
    CHECK(!document.ConsumeMaterialPreviewChange());
    CHECK(document.IsCompileCurrent());

    document.OpenMaterial({"test"}, {}, source, compile, true);
    before = document.GetMaterial();
    document.BeginMaterialGesture(before, "Drag");
    CHECK(!document.ApplyMaterial(laidOut, "Concurrent command"));
    (document.*Access(EditTag{}))().graph.nodes.front().position = {19, 23};
    CHECK(document.EndMaterialGesture(true));
    CHECK(Json(document.GetMaterial()) == Json(before) && !document.CanUndo());
    document.BeginMaterialGesture(before, "Drag");
    (document.*Access(EditTag{}))().graph.nodes.front().position = {19, 23};
    CHECK(document.EndMaterialGesture(false));
    CHECK(document.Undo(domain, label) && label == "Drag");
    CHECK(!document.IsMaterialDirty());
    CHECK(document.BeginAbPreview(laidOut, error));
    CHECK(!document.CanUndo() && !document.CanRedo());
    CHECK(document.DiscardAbPreview(error) && Json(document.GetMaterial()) == Json(before));
    CHECK(document.BeginAbPreview(laidOut, error));
    CHECK(document.KeepAbPreview(error) && document.IsMaterialDirty());
    CHECK(document.Undo(domain, label) && !document.IsMaterialDirty());

    document.OpenMaterial({"items"}, {}, source, compile, true);
    CHECK(document.ConsumeMaterialPreviewChange());
    before = document.GetMaterial();
    auto& live = (document.*Access(EditTag{}))();
    live.graph.parameters.front().defaultValue.numeric.x += 0.2f;
    document.UpdateMaterialItem(before, "First control", 1, true, true);
    CHECK(document.HasMaterialGesture() && !document.CanUndo());
    CHECK(document.ConsumeMaterialPreviewChange() && document.IsCompileCurrent());
    CHECK(document.GetCompileResult().program.programId == CompileMaterialAsset(live).program.programId);
    const auto first = document.GetMaterial();
    // A second control can be drawn before the previous control reports deactivation.
    live.graph.parameters.front().defaultValue.numeric.x += 0.3f;
    document.UpdateMaterialItem(first, "Second control", 2, true, true);
    CHECK(document.EndMaterialGesture(true));
    CHECK(Json(document.GetMaterial()) == Json(first));
    CHECK(document.Undo(domain, label) && label == "First control");
    CHECK(Json(document.GetMaterial()) == Json(before));
    document.UpdateMaterialItem(before, "Click without change", 3, true, false);
    document.FinishMaterialItems(0); // A hidden/collapsed control cannot strand its gesture.
    CHECK(!document.HasMaterialGesture() && !document.CanUndo());
    auto grouped = document.GetMaterial();
    MaterialGraphGroupData group;
    group.id = "temporary-group";
    group.nodeIds = {grouped.graph.nodes.front().id};
    grouped.graph.groups.push_back(group);
    CHECK(document.ApplyMaterial(grouped, "Group"));
    grouped.graph.nodes.erase(grouped.graph.nodes.begin());
    CHECK(document.ApplyMaterial(grouped, "Remove node externally"));
    CHECK(document.GetMaterial().graph.groups.empty());
    CHECK(document.Undo(domain, label) && document.GetMaterial().graph.groups.size() == 1);
    before = document.GetMaterial();
    (document.*Access(EditTag{}))().graph.parameters.front().defaultValue.numeric.x += 0.1f;
    document.UpdateMaterialItem(before, "Release", 4, true, true);
    const auto liveRevision = document.GetMaterialRevision();
    CHECK(document.ConsumeMaterialPreviewChange());
    CHECK(document.EndMaterialGesture(false));
    CHECK(document.GetMaterialRevision() == liveRevision && !document.ConsumeMaterialPreviewChange());
}
void ReuseAndInspectionContracts() {
    for (const auto& info : GetMaterialGraphModules()) {
        MaterialAssetData source;
        source.graph = CreateMaterialGraphModule(info.id);
        const auto original = Json(source);
        const auto* output = FindMaterialGraphNode(source.graph, source.graph.outputNodeId);
        CHECK(output);
        for (const auto pin : GetMaterialGraphNodeOutputPins(*output)) {
            MaterialAssetData preview;
            std::string error;
            CHECK(BuildMaterialNodePreview(source, output->id, pin, -1, preview, error));
            RequireCompile(CompileMaterialAsset(preview));
            CHECK(Json(source) == original);
        }
    }
    MaterialAssetData source;
    source.graph = CreateMaterialGraphModule("uv_flow");
    MaterialAssetData preview;
    std::string error;
    for (int channel = -1; channel <= 3; ++channel) {
        CHECK(BuildMaterialNodePreview(source, source.graph.outputNodeId, "Result", channel, preview, error));
        RequireCompile(CompileMaterialAsset(preview));
    }
    source = preview;
    auto graph = source.graph;
    std::vector<std::string> selected;
    for (const auto& node : graph.nodes)
        if (node.id == "Offset" || node.id == "Flow_UV")
            selected.push_back(node.id);
    const auto original = Json(source);
    MaterialAssetData function, replaced;
    CHECK(ExtractMaterialGraphFunction(source, selected, {"function-test"}, "Flow", "call", function, replaced, error));
    MaterialCompileOptions options;
    int resolutions = 0;
    options.resolveAsset = [&](const AssetGuid& guid, MaterialAssetData& out, std::string&) {
        ++resolutions;
        if (guid.value != "function-test")
            return false;
        out = function;
        return true;
    };
    auto compiled = CompileMaterialAsset(replaced, options);
    RequireCompile(compiled);
    CHECK(resolutions > 0);
    CHECK(Json(source) == original);
    CHECK(replaced.graph.nodes.size() < source.graph.nodes.size());
    CHECK(FindMaterialGraphNode(replaced.graph, "call"));
    ModelMaterialWorkspaceDocument document;
    document.SetMaterialCompileOptions(options);
    document.OpenMaterial({"replaced"}, {}, replaced, compiled, true);
    resolutions = 0;
    auto before = document.GetMaterial();
    UI::AutoLayoutMaterialGraph(before.graph);
    CHECK(document.ApplyMaterial(before, "Layout function graph"));
    CHECK(resolutions == 0);
    ModelMaterialWorkspaceDocument::ChangedDomain domain;
    std::string label;
    CHECK(document.Undo(domain, label));
    CHECK(document.Redo(domain, label));
    CHECK(resolutions == 0);
    CHECK(!ExtractMaterialGraphFunction(source, {"Speed", "Time"}, {"x"}, "bad", "call", function, replaced, error));
    source.domain = MaterialDomain::Particle;
    CHECK(!BuildMaterialNodePreview(source, "Time", "Time", -1, preview, error));
}
class ContractScene final : public DocumentSceneBase {
public:
    ContractScene() : DocumentSceneBase("material-contracts") {}
    const char* GetSceneName() const override { return "Material contracts"; }
};
void ControllerContracts() {
    std::cout << "  controller fixture\n" << std::flush;
    auto controller = std::make_unique<Controller>();
    auto scene = std::make_unique<ContractScene>();
    auto& document = *controller.*Access(DocumentTag{});
    // An intentionally incomplete graph keeps preview projection independent of GPU setup.
    auto source = CreateMaterialFromTemplate(MaterialTemplateId::StandardPbr);
    source.graph.edges.clear();
    MaterialGraphParameterData tint;
    tint.id = "parameter.contractTint";
    tint.defaultValue.type = MaterialValueType::Float3;
    source.graph.parameters.push_back(tint);
    MaterialGraphParameterData enabled;
    enabled.id = "parameter.contractEnabled";
    enabled.defaultValue.type = MaterialValueType::Bool;
    source.graph.parameters.push_back(enabled);
    document.OpenMaterial({}, {}, source, CompileMaterialAsset(source), true);
    EditorOperationRegistry operations;
    RegisterModelMaterialOperations(operations, *controller, [&] { return scene.get(); });
    auto result = operations.Execute("material.edit_parameter", {{"parameter", tint.id}, {"value", {0.2, 0.4, 0.6}}});
    CHECK(result.ok);
    CHECK(FindMaterialGraphParameter(document.GetMaterial().graph, tint.id)->defaultValue.numeric.z == 0.6f);
    const auto parameterRevision = document.GetMaterialRevision();
    CHECK(operations.Execute("material.edit_parameter", {{"parameter", tint.id}, {"value", {0.2, 0.4, 0.6}}}).ok);
    CHECK(document.GetMaterialRevision() == parameterRevision);
    CHECK(!operations.Execute("material.edit_parameter", {{"parameter", tint.id}, {"value", 0.2}}).ok);
    CHECK(!operations.Execute("material.edit_parameter", {{"parameter", tint.id}, {"value", {0.2, "bad", 0.6}}}).ok);
    CHECK(document.GetMaterialRevision() == parameterRevision);
    CHECK(operations.Execute("material.edit_parameter", {{"parameter", enabled.id}, {"value", true}}).ok);
    CHECK(FindMaterialGraphParameter(document.GetMaterial().graph, enabled.id)->defaultValue.boolean);
    CHECK(operations.Execute("material.undo").ok);
    CHECK(!FindMaterialGraphParameter(document.GetMaterial().graph, enabled.id)->defaultValue.boolean);
    CHECK(operations.Execute("material.redo").ok);
    CHECK(FindMaterialGraphParameter(document.GetMaterial().graph, enabled.id)->defaultValue.boolean);
    MaterialAssetData instance;
    instance.kind = MaterialAssetKind::Instance;
    std::string parameterError;
    auto value = FindMaterialGraphParameter(document.GetMaterial().graph, tint.id)->defaultValue;
    CHECK(SetMaterialParameterValue(instance, tint.id, value, parameterError, &source));
    CHECK(instance.instance.parameterOverrides.size() == 1);
    CHECK(instance.instance.parameterOverrides.front().value.numeric.z == 0.6f);
    value.type = MaterialValueType::Float1;
    CHECK(!SetMaterialParameterValue(instance, tint.id, value, parameterError, &source));
    CHECK(instance.instance.parameterOverrides.front().value.type == MaterialValueType::Float3);
    auto parent = CreateMaterialFromTemplate(MaterialTemplateId::StandardPbr);
    instance.instance.parameterOverrides.clear();
    instance.instance.parentMaterialGuid = {"contract.parent"};
    value = FindMaterialGraphParameter(parent.graph, "parameter.baseColor")->defaultValue;
    value.numeric = {0.1f, 0.2f, 0.3f, 0.4f};
    CHECK(SetMaterialParameterValue(instance, "parameter.baseColor", value, parameterError, &parent));
    parent.pbr.metallicFactor = 0.91f; // The graph owns defaults, even if the compatibility block is stale.
    const auto resolved =
        ResolveMaterialInstance(instance, [&](const AssetGuid& guid, MaterialAssetData& loaded, std::string&) {
            loaded = parent;
            return guid == instance.instance.parentMaterialGuid;
        });
    CHECK(resolved.success && resolved.resolvedMaterial.pbr.baseColorFactor.w == 0.4f);
    CHECK(resolved.resolvedMaterial.pbr.metallicFactor ==
          FindMaterialGraphParameter(parent.graph, "parameter.metallic")->defaultValue.numeric.x);
    MaterialGraphNodeCreateRequest request;
    request.nodeType = "Time";
    std::string message, code;
    std::vector<std::string> details;
    CHECK(controller->AddMaterialGraphNode(*scene, request, message, details, code));
    const auto timeId = document.GetMaterial().graph.nodes.back().id;
    const auto beforeLink = Json(document.GetMaterial());
    request = {};
    request.nodeType = "Multiply";
    request.connectSourceNodeId = timeId;
    request.connectSourcePin = "Time";
    CHECK(controller->AddMaterialGraphNode(*scene, request, message, details, code));
    CHECK(document.GetMaterial().graph.edges.size() == 1);
    const auto linked = Json(document.GetMaterial());
    ModelMaterialWorkspaceDocument::ChangedDomain domain;
    CHECK(document.Undo(domain, message));
    CHECK(Json(document.GetMaterial()) == beforeLink);
    CHECK(document.Redo(domain, message));
    CHECK(Json(document.GetMaterial()) == linked);
    const auto connection = document.GetMaterial().graph.edges.front();
    const auto connectionRevision = document.GetMaterialRevision();
    CHECK(document.ConnectMaterialNodes(connection, message));
    CHECK(document.GetMaterialRevision() == connectionRevision);
    auto invalidConnection = connection;
    invalidConnection.sourceNodeId = "missing";
    CHECK(!document.ConnectMaterialNodes(invalidConnection, message));
    CHECK(Json(document.GetMaterial()) == linked);
    request.nodeType = "Constant"; // Has no compatible input: source and history remain intact.
    CHECK(!controller->AddMaterialGraphNode(*scene, request, message, details, code));
    CHECK(Json(document.GetMaterial()) == linked);
    CHECK(document.Undo(domain, message));
    CHECK(Json(document.GetMaterial()) == beforeLink);
    CHECK(document.Redo(domain, message));

    std::cout << "  clipboard and groups\n" << std::flush;
    // Copying a whole graph never creates a second terminal node; shared parameters stay shared.
    auto fragment = CreateMaterialGraphModule("radial_mask");
    fragment.nodes.push_back(*FindMaterialGraphNode(source.graph, source.graph.outputNodeId));
    auto parameterNode = *std::find_if(fragment.nodes.begin(), fragment.nodes.end(),
                                       [](const auto& node) { return node.kind == MaterialNodeKind::Parameter; });
    parameterNode.id = "SharedParameter";
    fragment.nodes.push_back(parameterNode);
    const auto parameterCount = document.GetMaterial().graph.parameters.size();
    auto& edited = (document.*Access(EditTag{}))();
    CHECK(controller->PasteMaterialGraphFragment(edited, edited.graph, fragment, {40, 40}, false, "Paste"));
    CHECK(std::count_if(edited.graph.nodes.begin(), edited.graph.nodes.end(),
                        [](const auto& node) { return node.kind == MaterialNodeKind::SurfaceOutput; }) == 1);
    CHECK(edited.graph.parameters.size() == parameterCount + fragment.parameters.size());
    CHECK(operations.Execute("material.group_selected").ok);
    CHECK(!edited.graph.groups.empty());
    CHECK(operations.Execute("material.delete_selected").ok);
    CHECK(edited.graph.groups.empty());

    std::cout << "  ImGui frames\n" << std::flush;
    // Render actual ImGui graph frames in memory, including expanded node controls.
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = {1400, 900};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({1400, 900});
        ((*controller).*Access(DrawTag{}))(*scene);
        ImGui::Render();
        CHECK(ImGui::GetDrawData()->TotalVtxCount > 0);
        const auto* ui = ImGui::GetCurrentContext();
        CHECK(ui->DisabledStackSize == 0 && ui->ColorStack.empty() && ui->StyleVarStack.empty());
        CHECK(ui->BeginPopupStack.empty() && ui->CurrentWindowStack.empty());
    }
    ImGui::DestroyContext();
}
} // namespace
int main() {
    try {
        std::cout << "Testing layout\n" << std::flush;
        LayoutContracts();
        std::cout << "Testing document/history\n" << std::flush;
        DocumentContracts();
        std::cout << "Testing painterly template and instance controls\n" << std::flush;
        PainterlyTemplateContracts();
        std::cout << "Testing modules/inspection/functions\n" << std::flush;
        ReuseAndInspectionContracts();
        std::cout << "Testing controller and ImGui\n" << std::flush;
        ControllerContracts();
        std::cout << "PASS: material layout, dirty/history, program identity, metadata, modules, "
                     "inspection, function extraction, controller edits and ImGui stacks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
