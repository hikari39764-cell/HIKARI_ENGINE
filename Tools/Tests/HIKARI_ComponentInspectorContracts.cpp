#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

#include "Assets/HIKARI_AssetRegistryBuilder.h"
#include "Assets/Sprite/HIKARI_SpriteAssetStore.h"
#include "Assets/Sprite/HIKARI_SpriteMaterialStore.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Editor/HIKARI_ComponentDocumentEditor.h"
#include "Editor/History/HIKARI_SceneObjectsHistoryCommand.h"
#include "Editor/Inspectors/HIKARI_ImGuiInspectorBuilder.h"
#include "Editor/HIKARI_HierarchyPanel.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/Commands/HIKARI_SceneObjectCommandService.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Scene/Inspector/HIKARI_SceneInspectorPanel.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpriteComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#include "Render3D/Models/HIKARI_ModelManager.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace {
using namespace HIKARI;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)

class ProjectFogInspectorFixture final : public IGameComponent {
public:
    float density = 0.1f;
    float edge = 0.2f;
    void DescribeProperties(PropertyFields& fields) override {
        fields.Float("density", "Fog density", density, 0, 10, .01f);
        fields.Float("edge", "Fog edge", edge, 0, 1, .01f);
        if (fields.Button("Reset project fog")) {
            density = .1f;
            edge = .2f;
        }
    }
};

// 実際の Inspector widget を描画し、入力座標と表示済みフィールドだけを観測する。
class InspectorProbe final : public IInspectorBuilder {
public:
    ImGuiInspectorBuilder widgets{};
    std::unordered_map<std::string, ImRect> items{};
    std::vector<std::string> messages{};
    size_t parameters = 0u;
    size_t clips = 0u;
    void SetContext(const InspectorContext& context) override { widgets.SetContext(context); }
    const InspectorContext& GetContext() const noexcept override { return widgets.GetContext(); }
    void Text(std::string_view text) override {
        messages.emplace_back(text);
        widgets.Text(text);
    }
    bool Section(std::string_view label, bool open) override {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        return widgets.Section(label, open);
    }
    bool Button(std::string_view label) override {
        const bool changed = widgets.Button(label);
        Record(label);
        return changed;
    }
    bool Bool(const AuthoringFieldDescriptor& field, bool& value) override {
        const bool changed = widgets.Bool(field, value);
        Record(field.id);
        return changed;
    }
    bool Int(const AuthoringFieldDescriptor& field, int& value) override { return widgets.Int(field, value); }
    bool Float(const AuthoringFieldDescriptor& field, float& value) override { return widgets.Float(field, value); }
    bool FloatRange(const AuthoringFieldDescriptor& field, float& value, float min, float max, float speed) override {
        const bool changed = widgets.FloatRange(field, value, min, max, speed);
        Record(field.id);
        return changed;
    }
    bool TypedValue(const AuthoringFieldDescriptor& field, BINDING::Value& value, bool range, float min, float max,
                    float step) override {
        ++parameters;
        const bool changed = widgets.TypedValue(field, value, range, min, max, step);
        Record(field.id);
        return changed;
    }
    bool Color3(const AuthoringFieldDescriptor& field, float& r, float& g, float& b) override {
        return widgets.Color3(field, r, g, b);
    }
    bool Vec2(const AuthoringFieldDescriptor& field, float& x, float& y) override { return widgets.Vec2(field, x, y); }
    bool Vec3(const AuthoringFieldDescriptor& field, float& x, float& y, float& z) override {
        return widgets.Vec3(field, x, y, z);
    }
    bool String(const AuthoringFieldDescriptor& field, std::string& value) override {
        return widgets.String(field, value);
    }
    bool InputActionIdPicker(const AuthoringFieldDescriptor& field, INPUT::InputActionValueType type,
                             std::string& value) override {
        return widgets.InputActionIdPicker(field, type, value);
    }
    bool RuntimeVariableIdPicker(const AuthoringFieldDescriptor& field, std::span<const BINDING::ValueType> types,
                                 std::string& value) override {
        return widgets.RuntimeVariableIdPicker(field, types, value);
    }
    bool RuntimeEventIdPicker(const AuthoringFieldDescriptor& field, bool payload, BINDING::ValueType type,
                              std::string& value) override {
        return widgets.RuntimeEventIdPicker(field, payload, type, value);
    }
    bool RuntimeEventIdPicker(const AuthoringFieldDescriptor& field, AuthoringEventPayloadPolicy policy,
                              BINDING::ValueType type, std::string& value) override {
        return widgets.RuntimeEventIdPicker(field, policy, type, value);
    }
    bool Choice(const AuthoringFieldDescriptor& field, int& index, std::span<const AuthoringChoice> choices) override {
        if (field.id == "animationClip")
            clips = choices.size();
        return widgets.Choice(field, index, choices);
    }
    bool AssetIdPicker(const AuthoringFieldDescriptor& field, AssetType type, std::string& value) override {
        return widgets.AssetIdPicker(field, type, value);
    }
    bool SceneIdPicker(const AuthoringFieldDescriptor& field, std::string& value) override {
        return widgets.SceneIdPicker(field, value);
    }
    bool SceneObjectIdPicker(const AuthoringFieldDescriptor& field, SceneObjectId& value) override {
        return widgets.SceneObjectIdPicker(field, value);
    }

private:
    void Record(std::string_view id) { items[std::string(id)] = GImGui->LastItemData.Rect; }
};

struct ImGuiSession {
    ImGuiSession() {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = {1200, 3000};
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~ImGuiSession() { ImGui::DestroyContext(); }
};
} // namespace

void VerifySceneObjectMenuLifetime() {
    using namespace HIKARI;
    using namespace HIKARI::EDITOR;
    ImGuiSession gui;
    class MenuScene final : public DocumentSceneBase {
    public:
        MenuScene() : DocumentSceneBase("object-menu-contracts") {}
        const char* GetSceneName() const override { return "Object menu contracts"; }
    } scene;
    EditorContext context;
    SelectionSyncService selection;
    SceneObjectCommandService commands;
    SceneInspectorPanel inspector;
    HierarchyPanel hierarchy;
    EditorOperationRegistry operations;
    for (int surface = 0; surface < 3; ++surface) {
        for (const auto command : {SceneObjectCommandId::Duplicate, SceneObjectCommandId::Delete}) {
            auto& document = scene.GetSceneDocument();
            document.objects.clear();
            for (uint64_t id = 1; id <= 4; ++id) {
                SceneObjectData object;
                object.id = {id};
                object.name = "Object " + std::to_string(id);
                if (surface == 1)
                    object.editorFolder = "Group";
                if (id == 2)
                    object.parent = SceneObjectId{1};
                document.objects.push_back(std::move(object));
            }
            CHECK(selection.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId));
            context.selection.SelectObjectIds(scene.GetWorld(), {{1}});
            context.sceneDirty = false;
            auto* originalObject = context.selection.selectedObject;
            CHECK(originalObject);
            ImGuiID commandItem = 0;
            const auto frame = [&](bool open) {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0});
                ImGui::SetNextWindowSize({800, 1200});
                ImGui::Begin("Object menu contracts", nullptr, ImGuiWindowFlags_NoSavedSettings);
                if (surface == 1) {
                    // フォルダーは既定で閉じている。利用者が展開した状態を再現する。
                    ImGui::PushID("Group");
                    ImGui::GetStateStorage()->SetInt(ImGui::GetID("folder"), 1);
                    ImGui::PopID();
                }
                if (open) {
                    if (surface == 2) {
                        ImGui::PushID(1);
                        ImGui::OpenPopup("ObjectActions");
                    } else {
                        if (surface == 1) {
                            ImGui::PushID("Group");
                            ImGui::PushID("folder");
                        }
                        ImGui::PushID(originalObject);
                        ImGui::OpenPopup("ObjectContextMenu");
                    }
                    ImGui::PopID();
                    if (surface == 1) {
                        ImGui::PopID();
                        ImGui::PopID();
                    }
                }
                if (surface == 2) {
                    inspector.DrawContents(scene, context, selection, commands);
                } else {
                    hierarchy.DrawContents(
                        scene.GetWorld(), context.selection,
                        [&](GameObject& object) {
                            inspector.DrawObjectContextMenu(scene, context, selection, commands, object);
                        },
                        {}, surface == 1 ? &document : nullptr, surface == 1 ? &operations : nullptr);
                }
                if (!GImGui->OpenPopupStack.empty()) {
                    auto* popup = GImGui->OpenPopupStack.back().Window;
                    if (popup)
                        commandItem = popup->GetID(GetSceneObjectCommandPresentation(command).label);
                }
                // メニューの後も、同フレームの描画が旧ポインターを安全に使用できる。
                CHECK(document.objects.size() == 4);
                CHECK(context.selection.selectedObject == originalObject && originalObject->GetName() == "Object 1");
                ImGui::End();
                inspector.ExecutePendingObjectCommand(scene, context, selection, commands);
                ImGui::Render();
            };
            frame(true);
            frame(false);
            if (commandItem == 0)
                throw std::runtime_error("Object menu did not open on surface " + std::to_string(surface));
            ImGui::ActivateItemByID(commandItem);
            frame(false);
            const size_t expected = command == SceneObjectCommandId::Duplicate ? 6 : 2;
            CHECK(document.objects.size() == expected && scene.GetWorld().GetObjects().size() == expected);
            CHECK(context.sceneDirty);
            if (command == SceneObjectCommandId::Duplicate)
                CHECK(context.selection.selectedObject && context.selection.GetActiveObjectId().value > 4);
            else
                CHECK(context.selection.selectedObject == nullptr);
            auto history = commands.ConsumeHistoryRequest();
            CHECK(history && history->beforeObjects.size() == 4 && history->afterObjects.size() == expected);
            auto undo = MakeSceneObjectsHistoryCommand(history->label, history->beforeObjects, history->afterObjects,
                                                       history->beforeCamera, history->afterCamera);
            undo->Undo(document);
            CHECK(selection.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId));
            CHECK(document.objects.size() == 4 &&
                  scene.GetWorld().FindObject(SceneObjectId{2})->GetParent()->GetDocumentId().value == 1);
            undo->Redo(document);
            CHECK(selection.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId));
            CHECK(document.objects.size() == expected && scene.GetWorld().GetObjects().size() == expected);
        }
    }
    std::cout
        << "Scene menus: flat/folder hierarchy + Inspector duplicate/delete, deferred rebuild, selection, Undo/Redo: PASS\n";
}

// 既に import 済みの任意プロジェクトを引数に指定して、GPU や可視ウィンドウを使わず検証する。
void VerifyComponentInspectorProject(const std::filesystem::path& root) {
    using namespace HIKARI;
    ImGuiSession gui;
    CHECK(CONTENT::GetEngineContentContext().Open(HIKARI_CONTRACT_ENGINE_CONTENT_ROOT));
    AssetDatabase database;
    CHECK(database.Initialize(root));
    CHECK(database.ScanAssets(false));
    AssetRegistry assets;
    CHECK(AssetRegistryBuilder{}.RebuildRegistry(database, assets));
    SPRITE::SpriteAssetStore sprites;
    sprites.Configure(&database);
    SPRITE::SpriteMaterialStore materials;
    materials.Configure(&database, &assets);
    World world;
    CHECK(world.Services().Register(sprites));
    CHECK(world.Services().Register(materials));
    ComponentRegistry registry;
    ComponentTypeInfo type;
    type.typeName = "WorldSpriteComponent";
    type.factory = [] { return std::make_unique<WorldSpriteComponent>(); };
    CHECK(registry.Register(std::move(type)));
    ComponentDocumentEditor editor;
    InspectorProbe probe;
    InspectorContext context{&assets, &database, nullptr, &world.Services()};
    CHECK(!database.CollectByType(AssetType::SpriteMaterial).empty());
    CHECK(!database.CollectByType(AssetType::Sprite).empty());
    const AssetRecord* record = nullptr;
    std::string error;
    std::shared_ptr<const SPRITE::SpriteMaterialBinding> binding;
    for (const auto* candidate : database.CollectByType(AssetType::SpriteMaterial)) {
        auto resolved = materials.Resolve(candidate->guid, error);
        CHECK(resolved);
        if (!binding || resolved->shaderArtifact->parameters.size() > binding->shaderArtifact->parameters.size()) {
            record = candidate;
            binding = std::move(resolved);
        }
    }
    CHECK(record != nullptr && binding);
    const auto* spriteRecord = database.CollectByType(AssetType::Sprite).front();
    SceneDocument document;
    SceneObjectData object;
    object.id = {1};
    object.name = "Inspector fixture";
    WorldSpriteComponent source;
    SceneComponentData data;
    data.type = "WorldSpriteComponent";
    data.id = {1};
    source.Serialize(data.properties);
    data.properties["spriteAssetGuid"] = spriteRecord->guid.value;
    data.properties["spriteMaterial"] = {
        {"enabled", true}, {"assetGuid", record->guid.value}, {"parameters", nlohmann::json::object()}};
    data.properties["roughness"] = 0.73;
    data.properties["alphaCutoff"] = 0.27;
    data.properties["pivot"] = {0.123, 0.877};
    object.components.push_back(data);
    document.objects.push_back(object);
    context.sceneDocument = &document;
    auto& properties = document.objects[0].components[0].properties;
    const auto original = properties;
    auto frame = [&](ImVec2 mouse = ImVec2(-100, -100), bool down = false) {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(0, down);
        probe.parameters = 0;
        probe.clips = 0;
        probe.messages.clear();
        probe.items.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({1100, 2900});
        ImGui::Begin("Component inspector contracts", nullptr,
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        const bool changed = editor.DrawComponent(registry, document.objects[0].components[0], probe, context);
        ImGui::End();
        ImGui::Render();
        return changed;
    };
    for (int index = 0; index < 4; ++index)
        CHECK(!frame());
    CHECK(properties == original);
    CHECK(probe.parameters == binding->shaderArtifact->parameters.size());
    CHECK(probe.clips > 0);
    for (const auto& message : probe.messages)
        CHECK(message.find("unavailable") == std::string::npos);
    const auto parameter = std::find_if(
        binding->shaderArtifact->parameters.begin(), binding->shaderArtifact->parameters.end(), [](const auto& p) {
            return p.defaultValue.type == BINDING::ValueType::Float && p.hasRange && p.minimum < p.maximum;
        });
    CHECK(parameter != binding->shaderArtifact->parameters.end());
    const std::string field = "spriteMaterial.parameters." + parameter->key;
    CHECK(probe.items.contains(field));
    auto point = [](const ImRect& rect) { return ImVec2(rect.Max.x - 80.0f, (rect.Min.y + rect.Max.y) * 0.5f); };
    ImVec2 cursor = point(probe.items.at(field));
    frame(cursor);
    frame(cursor, true);
    cursor.x += 25;
    CHECK(frame(cursor, true));
    frame(cursor, false);
    CHECK(properties["spriteMaterial"]["parameters"].contains(parameter->key));
    CHECK(properties["roughness"] == original["roughness"] && properties["pivot"] == original["pivot"] &&
          properties["alphaCutoff"] == original["alphaCutoff"]);
    const auto edited = properties;
    auto command = EDITOR::MakeSceneObjectsHistoryCommand("Inspector drag", {object}, document.objects, document.camera,
                                                          document.camera);
    command->Undo(document);
    CHECK(document.objects[0].components[0].properties == original);
    CHECK(!frame());
    command->Redo(document);
    CHECK(document.objects[0].components[0].properties == edited);
    CHECK(!frame());
    const auto savePath = std::filesystem::path(HIKARI_CONTRACT_OUTPUT_ROOT) / "component-inspector-reopen.hscene";
    CHECK(SceneSerializer{}.SaveToFile(TEXT::PathToUtf8(savePath), document));
    CHECK(SceneSerializer{}.LoadFromFile(TEXT::PathToUtf8(savePath), document));
    CHECK(!frame());
    CHECK(document.objects[0].components[0].properties == edited);
    auto* runtime = world.CreateObject("Preview");
    auto* component = runtime->AddComponent<WorldSpriteComponent>();
    ModelManager models;
    component->Deserialize(document.objects[0].components[0].properties);
    CHECK(SceneRuntimeBuilder{}.InitializeRuntimeComponentAssets(*runtime, *component, assets, models, error));
    CHECK(component->GetMaterialParameterCount() == binding->shaderArtifact->parameters.size());
    CHECK(component->GetAnimationState().bound);
    BINDING::Value actual;
    CHECK(component->TryGetMaterialParameter(parameter->key, actual));
    CHECK(actual.numeric.x == edited["spriteMaterial"]["parameters"][parameter->key]["value"].get<float>());
    const std::string reset = "Reset override: " + parameter->label + "###spriteMaterial.reset." + parameter->key;
    CHECK(probe.items.contains(reset));
    cursor = point(probe.items.at(reset));
    frame(cursor);
    frame(cursor, true);
    CHECK(frame(cursor, false));
    CHECK(document.objects[0].components[0].properties["spriteMaterial"]["parameters"].empty());
    CHECK(!frame());
    ComponentTypeInfo fogType;
    fogType.typeName = "Contract.ProjectFog";
    fogType.factory = [] {
        return std::make_unique<GameProjectComponentAdapter>("Contract.ProjectFog", new ProjectFogInspectorFixture,
                                                             [](IGameComponent* value) noexcept { delete value; });
    };
    CHECK(registry.Register(std::move(fogType)));
    auto& fogData = document.objects[0].components[0];
    fogData.type = "Contract.ProjectFog";
    fogData.properties = {{"density", 0.73}, {"edge", 0.61}, {"retained", "untouched"}};
    const auto fogBefore = document.objects;
    CHECK(!frame());
    CHECK(!frame());
    CHECK(probe.items.contains("density") && probe.items.contains("edge"));
    cursor = point(probe.items.at("Reset project fog"));
    frame(cursor);
    frame(cursor, true);
    CHECK(frame(cursor, false));
    CHECK(document.objects[0].components[0].properties["density"].get<float>() == .1f);
    CHECK(document.objects[0].components[0].properties["edge"].get<float>() == .2f);
    CHECK(document.objects[0].components[0].properties["retained"] == "untouched");
    auto resetCommand = EDITOR::MakeSceneObjectsHistoryCommand("Project fog reset", fogBefore, document.objects,
                                                               document.camera, document.camera);
    resetCommand->Undo(document);
    CHECK(document.objects[0].components[0].properties == fogBefore[0].components[0].properties);
    resetCommand->Redo(document);
    CHECK(document.objects[0].components[0].properties["density"].get<float>() == .1f);
    CHECK(SceneSerializer{}.SaveToFile(TEXT::PathToUtf8(savePath), document));
    CHECK(SceneSerializer{}.LoadFromFile(TEXT::PathToUtf8(savePath), document));
    CHECK(!frame());
    std::cout
        << "Inspector: idle exact preservation / metadata / ImGui drag / reset / Undo-Redo / save-reopen / runtime rebind: PASS\n";
}
