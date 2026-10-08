#include "Editor/RenderStyle/HIKARI_RenderStyleOperations.h"

#include <memory>
#include <string>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Post/HIKARI_PostAssetStore.h"
#include "Assets/RenderStyle/HIKARI_RenderStyleAssetStore.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/RenderStyle/HIKARI_RenderStyleEditor.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::EDITOR {
namespace {
using ParameterType = EditorOperationParameterType;
using SceneProvider = std::function<DocumentSceneBase*()>;
using RENDERSTYLE::RenderStyleAsset;

DocumentSceneBase* ResolveScene(const SceneProvider& provider, EditorOperationResult& failure) {
    DocumentSceneBase* scene = provider ? provider() : nullptr;
    if (scene == nullptr) {
        failure = OperationFailure("NO_SCENE", "No scene is open.");
    }
    return scene;
}

// 開いている文書の様式なら未保存の内容を、それ以外は資産の内容を使う。
std::shared_ptr<const RenderStyleAsset> LoadStyle(RenderStyleEditor& editor, DocumentSceneBase& scene,
                                                  const std::string& id, std::string& error) {
    const RenderStyleEditorDocument& document = editor.Document();
    if (document.IsOpen() && document.Guid().value == id) {
        return std::make_shared<const RenderStyleAsset>(document.Profile());
    }
    return scene.GetRenderStyleAssets().Load(id, error);
}

EditorOperationResult Describe(RenderStyleEditor& editor, const SceneProvider& provider,
                               const nlohmann::json& parameters) {
    EditorOperationResult failure;
    DocumentSceneBase* scene = ResolveScene(provider, failure);
    if (!scene) {
        return failure;
    }
    nlohmann::json styles = nlohmann::json::array();
    for (const RENDERSTYLE::BuiltinRenderStyleContent& content : RENDERSTYLE::GetBuiltinRenderStyles()) {
        styles.push_back({{"id", content.id}, {"name", content.displayName}, {"builtin", true}});
    }
    for (const AssetRecord* record : scene->GetAssetDatabase().CollectByType(AssetType::RenderStyle)) {
        styles.push_back(
            {{"id", record->guid.value}, {"name", GetAssetRecordDisplayName(*record)}, {"builtin", false}});
    }
    const std::string sceneStyle = scene->GetEnvironmentRuntime().GetRenderStyleId();
    const std::string id = parameters.value("styleId", sceneStyle);
    std::string error;
    const auto style = LoadStyle(editor, *scene, id, error);
    if (!style) {
        return OperationFailure("STYLE_UNAVAILABLE", error);
    }
    const nlohmann::json content = RENDERSTYLE::WriteRenderStyleAsset(*style);
    const RenderStyleEditorDocument& document = editor.Document();
    return OperationOk(
        "Render style. post layers stack in order; each layer's values live in its post profile "
        "(post.profile.describe).",
        {{"sceneStyleId", sceneStyle},
         {"styleId", id},
         {"editable", !id.starts_with(RENDERSTYLE::kBuiltinRenderStylePrefix)},
         {"dirty", document.IsOpen() && document.Guid().value == id && document.IsDirty()},
         {"style", {{"name", content.at("name")}, {"shading", content.at("shading")}, {"post", content.at("post")}}},
         {"styles", std::move(styles)}});
}

EditorOperationResult Create(RenderStyleEditor& editor, const SceneProvider& provider,
                             const nlohmann::json& parameters) {
    EditorOperationResult failure;
    DocumentSceneBase* scene = ResolveScene(provider, failure);
    if (!scene) {
        return failure;
    }
    std::string error;
    const auto source = LoadStyle(editor, *scene, parameters.value("sourceStyleId", std::string{}), error);
    if (!source) {
        return OperationFailure("STYLE_UNAVAILABLE", error);
    }
    RenderStyleAsset copy = *source;
    if (const std::string name = parameters.value("name", std::string{}); !name.empty()) {
        copy.name = name;
    }
    std::string message;
    if (!editor.CreateStyleCopy(std::move(copy), scene->GetAssetDatabase(), message)) {
        return OperationFailure("CREATE_FAILED", message);
    }
    return OperationOk("Project render style created; select it with environment.edit renderStyle.",
                       {{"styleId", editor.Document().Guid().value}});
}

EditorOperationResult Edit(RenderStyleEditor& editor, const SceneProvider& provider, const nlohmann::json& parameters) {
    EditorOperationResult failure;
    DocumentSceneBase* scene = ResolveScene(provider, failure);
    if (!scene) {
        return failure;
    }
    const std::string id = parameters.value("styleId", std::string{});
    if (id.starts_with(RENDERSTYLE::kBuiltinRenderStylePrefix)) {
        return OperationFailure("BUILTIN_READ_ONLY", "Engine styles are read-only; use renderstyle.create first.");
    }
    RenderStyleEditorDocument& document = editor.Document();
    std::string message;
    if (!(document.IsOpen() && document.Guid().value == id) &&
        !document.Open(scene->GetAssetDatabase(), AssetGuid{id}, message)) {
        return OperationFailure("STYLE_BUSY", message);
    }
    // 部分的な内容を今の内容へ重ね、資産と同じ規則で読み直して検証する。
    nlohmann::json merged = RENDERSTYLE::WriteRenderStyleAsset(document.Profile());
    if (parameters.contains("name")) {
        merged["name"] = parameters.at("name");
    }
    if (parameters.contains("shading")) {
        if (!parameters.at("shading").is_object()) {
            return OperationFailure("INVALID_STYLE", "shading must be an object.");
        }
        merged["shading"].merge_patch(parameters.at("shading"));
    }
    if (parameters.contains("post")) {
        merged["post"] = parameters.at("post");
    }
    RenderStyleAsset edited;
    if (!RENDERSTYLE::ReadRenderStyleAsset(merged, edited, message)) {
        return OperationFailure("INVALID_STYLE", message);
    }
    const auto* posts = scene->GetWorld().Services().Find<POST::PostAssetStore>();
    for (const RENDERSTYLE::RenderStylePostLayer& layer : edited.post) {
        if (posts == nullptr || !posts->LoadProfile(layer.profileId, message)) {
            return OperationFailure("UNKNOWN_PROFILE", message);
        }
    }
    if (!document.Apply(std::move(edited), message)) {
        return OperationFailure("EDIT_REJECTED", message);
    }
    editor.PublishDrafts(*scene);
    return OperationOk("Render style edited; save it with renderstyle.save.",
                       {{"styleId", id}, {"dirty", document.IsDirty()}});
}

EditorOperationResult Save(RenderStyleEditor& editor, const SceneProvider& provider, const nlohmann::json& parameters) {
    EditorOperationResult failure;
    DocumentSceneBase* scene = ResolveScene(provider, failure);
    if (!scene) {
        return failure;
    }
    RenderStyleEditorDocument& document = editor.Document();
    if (!document.IsOpen() || document.Guid().value != parameters.value("styleId", std::string{})) {
        return OperationFailure("STYLE_NOT_OPEN", "Edit the style with renderstyle.edit before saving it.");
    }
    std::string message;
    if (!document.Save(scene->GetAssetDatabase(), message)) {
        return OperationFailure("SAVE_FAILED", message);
    }
    editor.PublishDrafts(*scene);
    return OperationOk("Render style saved.");
}
} // namespace

void RegisterRenderStyleOperations(EditorOperationRegistry& registry, RenderStyleEditor& editor, SceneProvider scene) {
    EditorOperationDefinition describe{
        "renderstyle.describe",
        "List render styles (whole-screen look such as anime, manga, painterly, sketch, pixel or retro CRT) and "
        "describe one: standard-material shading and ordered post layers. Defaults to the scene's style.",
        "scene",
        {{"styleId", ParameterType::String, false, "Style id; defaults to the scene's render style"}},
        [&editor, scene](const nlohmann::json& p) { return Describe(editor, scene, p); }};
    describe.entryPoint = true;
    describe.searchTerms = {"render style", "style", "look", "stylized", "npr", "post", "风格", "渲染风格", "画風"};
    registry.Register(std::move(describe), EditorOperationKind::Query, false);
    EditorOperationDefinition create{"renderstyle.create",
                                     "Save a new project render style copied from an engine or project style.",
                                     "scene",
                                     {{"sourceStyleId", ParameterType::String, true, "Style to copy"},
                                      {"name", ParameterType::String, false, "Display name of the copy"}},
                                     [&editor, scene](const nlohmann::json& p) { return Create(editor, scene, p); }};
    create.permission = EditorOperationPermission::ModifyProjectFiles;
    registry.Register(std::move(create), EditorOperationKind::Mutation, false);
    registry.Register(
        {"renderstyle.edit",
         "Edit a project render style as an unsaved draft shown live in the scene. shading is merged (model Physical "
         "or Cel, bands 1-4, threshold, softness, shadowTint [r,g,b], specular, rim; applies to standard materials). "
         "post replaces the whole ordered layer list: [{\"profile\":\"<post profile id>\",\"enabled\":true}].",
         "scene",
         {{"styleId", ParameterType::String, true, "Project style id"},
          {"name", ParameterType::String, false, "Display name"},
          {"shading", ParameterType::Json, false, "Partial standard-material shading"},
          {"post", ParameterType::Json, false, "Ordered post layers"}},
         [&editor, scene](const nlohmann::json& p) { return Edit(editor, scene, p); }},
        EditorOperationKind::Mutation, false);
    EditorOperationDefinition save{"renderstyle.save",
                                   "Save the edited project render style.",
                                   "scene",
                                   {{"styleId", ParameterType::String, true, "Project style id"}},
                                   [&editor, scene](const nlohmann::json& p) { return Save(editor, scene, p); }};
    save.permission = EditorOperationPermission::ModifyProjectFiles;
    registry.Register(std::move(save), EditorOperationKind::Mutation, false);
}

} // namespace HIKARI::EDITOR
