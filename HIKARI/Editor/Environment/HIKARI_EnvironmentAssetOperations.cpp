#include "Editor/Environment/HIKARI_EnvironmentAssetOperations.h"

#include <string>
#include <string_view>
#include <utility>

#include "Assets/Environment/HIKARI_EnvironmentAssetStore.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Environment/HIKARI_EnvironmentAssetEditor.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {
using ParameterType = EditorOperationParameterType;
using SceneProvider = std::function<DocumentSceneBase*()>;

// 種類ごとの違い（内蔵 ID の接頭辞、資産の読み込み、文書、複製）。
struct ProfileKind {
    using Traits = EnvironmentProfileDocumentTraits;
    static constexpr std::string_view prefix = ENVIRONMENT::kBuiltinEnvironmentPrefix;
    static auto Load(DocumentSceneBase& scene, std::string_view id, std::string& error) {
        return scene.GetEnvironmentAssets().LoadProfile(id, error);
    }
    static EnvironmentProfileEditorDocument& Document(EnvironmentAssetEditor& editor) { return editor.Profile(); }
    static bool Read(const nlohmann::json& json, Traits::Asset& asset, std::string& error) {
        return ENVIRONMENT::ReadEnvironmentProfileAsset(json, asset, error);
    }
};

struct WeatherKind {
    using Traits = WeatherPresetDocumentTraits;
    static constexpr std::string_view prefix = ENVIRONMENT::kBuiltinWeatherPrefix;
    static auto Load(DocumentSceneBase& scene, std::string_view id, std::string& error) {
        return scene.GetEnvironmentAssets().LoadWeather(id, error);
    }
    static WeatherPresetEditorDocument& Document(EnvironmentAssetEditor& editor) { return editor.Weather(); }
    static bool Read(const nlohmann::json& json, Traits::Asset& asset, std::string& error) {
        return ENVIRONMENT::ReadWeatherPresetAsset(json, asset, error);
    }
};

// 資産の JSON から形式の版を除いた、編集できる項目。
nlohmann::json EditableJson(nlohmann::json json) {
    json.erase("formatVersion");
    return json;
}

template <class Kind>
EditorOperationResult Describe(EnvironmentAssetEditor& editor, DocumentSceneBase& scene, const std::string& id) {
    std::string error;
    const auto asset = Kind::Load(scene, id, error);
    if (!asset) {
        return OperationFailure("ASSET_UNAVAILABLE", error);
    }
    const auto& document = Kind::Document(editor);
    return OperationOk("Environment asset.",
                       {{"id", id},
                        {"editable", !id.starts_with(Kind::prefix)},
                        {"dirty", document.IsOpen() && document.Guid().value == id && document.IsDirty()},
                        {"value", EditableJson(Kind::Traits::Write(*asset))}});
}

template <class Kind>
EditorOperationResult Create(EnvironmentAssetEditor& editor, DocumentSceneBase& scene,
                             const nlohmann::json& parameters) {
    std::string error;
    const auto source = Kind::Load(scene, parameters.value("sourceId", std::string{}), error);
    if (!source) {
        return OperationFailure("ASSET_UNAVAILABLE", error);
    }
    typename Kind::Traits::Asset copy = *source;
    if (const std::string name = parameters.value("name", std::string{}); !name.empty()) {
        copy.name = name;
    }
    std::string message;
    if (!Kind::Document(editor).SaveAsNew(std::move(copy), scene.GetAssetDatabase(), message)) {
        return OperationFailure("CREATE_FAILED", message);
    }
    editor.PublishDrafts(scene);
    return OperationOk("Project environment asset created; select it with environment.edit.",
                       {{"id", Kind::Document(editor).Guid().value}});
}

template <class Kind>
EditorOperationResult Edit(EnvironmentAssetEditor& editor, DocumentSceneBase& scene, const nlohmann::json& parameters) {
    const std::string id = parameters.value("id", std::string{});
    if (id.starts_with(Kind::prefix)) {
        return OperationFailure("BUILTIN_READ_ONLY",
                                "Engine content is read-only; copy it with environment.asset.create first.");
    }
    const nlohmann::json& patch = parameters.at("patch");
    if (!patch.is_object() || patch.empty()) {
        return OperationFailure("INVALID_PATCH", "patch must be a non-empty object.");
    }
    auto& document = Kind::Document(editor);
    std::string message;
    if (!(document.IsOpen() && document.Guid().value == id) &&
        !document.Open(scene.GetAssetDatabase(), AssetGuid{id}, message)) {
        return OperationFailure("ASSET_BUSY", message);
    }
    const nlohmann::json current = Kind::Traits::Write(document.Profile());
    if (const std::string unknown = FindUnknownPatchKey(patch, EditableJson(current)); !unknown.empty()) {
        return OperationFailure("UNKNOWN_FIELD", "Unknown field: " + unknown);
    }
    nlohmann::json merged = current;
    merged.merge_patch(patch);
    typename Kind::Traits::Asset edited{};
    if (!Kind::Read(merged, edited, message)) {
        return OperationFailure("INVALID_VALUE", message);
    }
    if (!document.Apply(std::move(edited), message)) {
        return OperationFailure("EDIT_REJECTED", message);
    }
    editor.PublishDrafts(scene);
    const nlohmann::json stored = Kind::Traits::Write(document.Profile());
    return OperationOk("Environment asset edited; save it with environment.asset.save.",
                       {{"id", id},
                        {"dirty", document.IsDirty()},
                        {"appliedPatch", ProjectAppliedPatch(patch, stored)},
                        {"adjusted", CollectAdjustedPatchValues(patch, stored)}});
}

template <class Kind>
EditorOperationResult Save(EnvironmentAssetEditor& editor, DocumentSceneBase& scene, const std::string& id) {
    auto& document = Kind::Document(editor);
    if (!document.IsOpen() || document.Guid().value != id) {
        return OperationFailure("ASSET_NOT_OPEN", "Edit the asset with environment.asset.edit before saving it.");
    }
    std::string message;
    if (!document.Save(scene.GetAssetDatabase(), message)) {
        return OperationFailure("SAVE_FAILED", message);
    }
    editor.PublishDrafts(scene);
    return OperationOk("Environment asset saved.");
}

// kind で種類を選び、同じ手順を実行する。
template <class Run>
EditorOperationResult Dispatch(const SceneProvider& provider, const nlohmann::json& parameters, Run run) {
    DocumentSceneBase* scene = provider ? provider() : nullptr;
    if (scene == nullptr) {
        return OperationFailure("NO_SCENE", "No scene is open.");
    }
    const std::string kind = parameters.value("kind", std::string{});
    if (kind == "profile") {
        return run(ProfileKind{}, *scene);
    }
    if (kind == "weather") {
        return run(WeatherKind{}, *scene);
    }
    return OperationFailure("UNKNOWN_KIND", "kind must be profile or weather.");
}

const EditorOperationParameter kKindParameter{"kind", ParameterType::String, true, "profile or weather"};
} // namespace

void RegisterEnvironmentAssetOperations(EditorOperationRegistry& registry, EnvironmentAssetEditor& editor,
                                        SceneProvider scene) {
    registry.Register(
        {"environment.asset.describe",
         "Return an environment profile (sky, atmosphere, sun, moon, clouds, fog, exposure, post...) or weather "
         "preset in its saved JSON form, including unsaved edits.",
         "scene",
         {kKindParameter, {"id", ParameterType::String, true, "Id from environment.describe"}},
         [&editor, scene](const nlohmann::json& p) {
             return Dispatch(scene, p, [&](auto kind, DocumentSceneBase& target) {
                 return Describe<decltype(kind)>(editor, target, p.value("id", std::string{}));
             });
         }},
        EditorOperationKind::Query, false);

    EditorOperationDefinition create{
        "environment.asset.create",
        "Save a new project environment profile or weather preset copied from an engine or project asset.",
        "scene",
        {kKindParameter,
         {"sourceId", ParameterType::String, true, "Asset to copy"},
         {"name", ParameterType::String, false, "Display name of the copy"}},
        [&editor, scene](const nlohmann::json& p) {
            return Dispatch(scene, p, [&](auto kind, DocumentSceneBase& target) {
                return Create<decltype(kind)>(editor, target, p);
            });
        }};
    create.permission = EditorOperationPermission::ModifyProjectFiles;
    registry.Register(std::move(create), EditorOperationKind::Mutation, false);

    registry.Register(
        {"environment.asset.edit",
         "Merge a partial JSON object into a project environment profile or weather preset (same keys as "
         "environment.asset.describe value). The unsaved draft shows in the scene at once; reports normalized values.",
         "scene",
         {kKindParameter,
          {"id", ParameterType::String, true, "Project asset id"},
          {"patch", ParameterType::Json, true, "Partial asset, for example {\"fog\":{\"density\":0.01}}"}},
         [&editor, scene](const nlohmann::json& p) {
             return Dispatch(scene, p, [&](auto kind, DocumentSceneBase& target) {
                 return Edit<decltype(kind)>(editor, target, p);
             });
         }},
        EditorOperationKind::Mutation, false);

    EditorOperationDefinition save{"environment.asset.save",
                                   "Save the edited project environment profile or weather preset.",
                                   "scene",
                                   {kKindParameter, {"id", ParameterType::String, true, "Project asset id"}},
                                   [&editor, scene](const nlohmann::json& p) {
                                       return Dispatch(scene, p, [&](auto kind, DocumentSceneBase& target) {
                                           return Save<decltype(kind)>(editor, target, p.value("id", std::string{}));
                                       });
                                   }};
    save.permission = EditorOperationPermission::ModifyProjectFiles;
    registry.Register(std::move(save), EditorOperationKind::Mutation, false);
}

} // namespace HIKARI::EDITOR
