#include "Editor/Quality/HIKARI_GlobalPostOperations.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Post/HIKARI_PostAssetStore.h"
#include "Assets/RenderStyle/HIKARI_RenderStyleAssetStore.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Quality/HIKARI_GlobalPostEditor.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_World.h"
#include "Vfx/Parameters/HIKARI_VfxParameterValues.h"

namespace HIKARI::EDITOR {
namespace {
using ParameterType = EditorOperationParameterType;
using SceneProvider = std::function<DocumentSceneBase*()>;

struct PostContext {
    DocumentSceneBase* scene = nullptr;
    POST::PostAssetStore* store = nullptr;
};

bool ResolveContext(const SceneProvider& provider, PostContext& context, EditorOperationResult& failure) {
    context.scene = provider ? provider() : nullptr;
    context.store = context.scene ? context.scene->GetWorld().Services().Find<POST::PostAssetStore>() : nullptr;
    if (context.store == nullptr) {
        failure = OperationFailure("NO_SCENE", "No scene with global post support is open.");
        return false;
    }
    return true;
}

bool IsBuiltinProfile(std::string_view id) { return id.starts_with(POST::kBuiltinPostProfilePrefix); }

nlohmann::json Components(const float* values, size_t count) {
    nlohmann::json out = nlohmann::json::array();
    for (size_t index = 0; index < count; ++index) {
        out.push_back(values[index]);
    }
    return out;
}

// 開いている文書のプロファイルなら未保存の内容を、それ以外は資産の内容を使う。
std::shared_ptr<const POST::PostProfileAsset> LoadProfile(GlobalPostEditor& editor, const POST::PostAssetStore& store,
                                                          const std::string& id, std::string& error) {
    const PostProfileEditorDocument& document = editor.Document();
    if (document.IsOpen() && document.Guid().value == id) {
        return std::make_shared<const POST::PostProfileAsset>(document.Profile());
    }
    return store.LoadProfile(id, error);
}

nlohmann::json DescribeParameters(const POST::PostEffectDefinition& effect, const POST::PostProfileAsset& profile) {
    nlohmann::json parameters = nlohmann::json::array();
    for (const VFX::ParamDesc& param : effect.params) {
        const size_t count = VFX::GetParameterComponentCount(param.type);
        const POST::PostParameterValue value = POST::GetPostParameterValue(param, &profile);
        parameters.push_back({{"key", param.key},
                              {"label", param.label},
                              {"components", count},
                              {"default", Components(param.defaultValues, count)},
                              {"min", Components(param.minValues, count)},
                              {"max", Components(param.maxValues, count)},
                              {"value", Components(value.data(), count)}});
    }
    return parameters;
}

// 数値 1 つか、成分数と同じ長さの配列を受け付け、宣言された範囲へ収める。
bool ReadParameterValue(const VFX::ParamDesc& param, const nlohmann::json& input, POST::PostParameterValue& value,
                        bool& adjusted) {
    const size_t count = VFX::GetParameterComponentCount(param.type);
    value = POST::GetPostParameterValue(param, nullptr);
    const auto read = [&](size_t index, const nlohmann::json& item) {
        if (!item.is_number() || !std::isfinite(item.get<float>())) {
            return false;
        }
        const float requested = item.get<float>();
        float stored = requested;
        if (param.minValues[index] < param.maxValues[index]) {
            stored = std::clamp(requested, param.minValues[index], param.maxValues[index]);
        }
        adjusted = adjusted || stored != requested;
        value[index] = stored;
        return true;
    };
    if (count == 1u && input.is_number()) {
        return read(0u, input);
    }
    if (!input.is_array() || input.size() != count) {
        return false;
    }
    for (size_t index = 0; index < count; ++index) {
        if (!read(index, input[index])) {
            return false;
        }
    }
    return true;
}

EditorOperationResult QueryPost(GlobalPostEditor& editor, const SceneProvider& provider) {
    PostContext context;
    EditorOperationResult failure;
    if (!ResolveContext(provider, context, failure)) {
        return failure;
    }
    const std::string& styleId = context.scene->GetEnvironmentRuntime().GetRenderStyleId();
    nlohmann::json layers = nlohmann::json::array();
    std::string styleError;
    if (const auto style = context.scene->GetRenderStyleAssets().Load(styleId, styleError)) {
        for (const RENDERSTYLE::RenderStylePostLayer& layer : style->post) {
            layers.push_back({{"profileId", layer.profileId}, {"enabled", layer.enabled}});
        }
    }
    nlohmann::json profiles = nlohmann::json::array();
    nlohmann::json effects = nlohmann::json::array();
    for (const POST::BuiltinPostContent& content : POST::GetBuiltinPostContent()) {
        profiles.push_back({{"id", content.profileId},
                            {"name", content.displayName},
                            {"effectId", content.effectId},
                            {"builtin", true}});
        effects.push_back({{"id", content.effectId}, {"name", content.displayName}, {"builtin", true}});
    }
    AssetDatabase& database = context.scene->GetAssetDatabase();
    for (const AssetRecord* record : database.CollectByType(AssetType::PostProfile)) {
        std::string error;
        const auto profile = LoadProfile(editor, *context.store, record->guid.value, error);
        profiles.push_back({{"id", record->guid.value},
                            {"name", profile ? profile->name : GetAssetRecordDisplayName(*record)},
                            {"effectId", profile ? profile->effectId : std::string{}},
                            {"builtin", false}});
    }
    for (const AssetRecord* record : database.CollectByType(AssetType::PostEffect)) {
        effects.push_back(
            {{"id", record->guid.value}, {"name", GetAssetRecordDisplayName(*record)}, {"builtin", false}});
    }
    const PostProfileEditorDocument& document = editor.Document();
    return OperationOk("Global post state. The scene's render style stacks post profiles in order: pick the style "
                       "with environment.edit {\"patch\":{\"renderStyle\":\"<id>\"}} and edit a project style's "
                       "layers with renderstyle.edit.",
                       {{"scene", {{"renderStyleId", styleId}, {"post", std::move(layers)}, {"error", styleError}}},
                        {"profiles", std::move(profiles)},
                        {"effects", std::move(effects)},
                        {"editing",
                         {{"profileId", document.IsOpen() ? document.Guid().value : std::string{}},
                          {"dirty", document.IsOpen() && document.IsDirty()}}}});
}

EditorOperationResult DescribeProfile(GlobalPostEditor& editor, const SceneProvider& provider,
                                      const nlohmann::json& parameters) {
    PostContext context;
    EditorOperationResult failure;
    if (!ResolveContext(provider, context, failure)) {
        return failure;
    }
    const std::string id = parameters.value("profileId", std::string{});
    std::string error;
    const auto profile = LoadProfile(editor, *context.store, id, error);
    const auto effect = profile ? context.store->LoadEffect(profile->effectId, error) : nullptr;
    if (!effect) {
        return OperationFailure("PROFILE_UNAVAILABLE", error);
    }
    const PostProfileEditorDocument& document = editor.Document();
    return OperationOk("Global post profile.",
                       {{"profileId", id},
                        {"name", profile->name},
                        {"effectId", profile->effectId},
                        {"effectName", effect->name},
                        {"editable", !IsBuiltinProfile(id)},
                        {"dirty", document.IsOpen() && document.Guid().value == id && document.IsDirty()},
                        {"parameters", DescribeParameters(*effect, *profile)}});
}

EditorOperationResult CreateProfile(GlobalPostEditor& editor, const SceneProvider& provider,
                                    const nlohmann::json& parameters) {
    PostContext context;
    EditorOperationResult failure;
    if (!ResolveContext(provider, context, failure)) {
        return failure;
    }
    std::string error;
    const auto source = LoadProfile(editor, *context.store, parameters.value("sourceProfileId", std::string{}), error);
    if (!source) {
        return OperationFailure("PROFILE_UNAVAILABLE", error);
    }
    POST::PostProfileAsset copy = *source;
    if (const std::string name = parameters.value("name", std::string{}); !name.empty()) {
        copy.name = name;
    }
    std::string message;
    if (!editor.Document().SaveAsNew(std::move(copy), context.scene->GetAssetDatabase(), message)) {
        return OperationFailure("CREATE_FAILED", message);
    }
    return OperationOk("Project post profile created.", {{"profileId", editor.Document().Guid().value}});
}

EditorOperationResult EditProfile(GlobalPostEditor& editor, const SceneProvider& provider,
                                  const nlohmann::json& parameters) {
    PostContext context;
    EditorOperationResult failure;
    if (!ResolveContext(provider, context, failure)) {
        return failure;
    }
    const std::string id = parameters.value("profileId", std::string{});
    if (IsBuiltinProfile(id)) {
        return OperationFailure("BUILTIN_READ_ONLY", "Engine profiles are read-only; use post.profile.create first.");
    }
    PostProfileEditorDocument& document = editor.Document();
    std::string message;
    if (!(document.IsOpen() && document.Guid().value == id) &&
        !document.Open(context.scene->GetAssetDatabase(), AssetGuid{id}, message)) {
        return OperationFailure("PROFILE_BUSY", message);
    }
    POST::PostProfileAsset edited = document.Profile();
    if (const std::string effectId = parameters.value("effectId", std::string{}); !effectId.empty()) {
        // パラメーターは効果ごとに違うので、効果を替えたら既定値から始める。
        edited.effectId = effectId;
        edited.values.clear();
    }
    if (parameters.value("resetDefaults", false)) {
        edited.values.clear();
    }
    std::string error;
    const auto effect = context.store->LoadEffect(edited.effectId, error);
    if (!effect) {
        return OperationFailure("EFFECT_UNAVAILABLE", error);
    }
    nlohmann::json adjusted = nlohmann::json::array();
    const nlohmann::json values = parameters.value("values", nlohmann::json::object());
    if (!values.is_object()) {
        return OperationFailure("INVALID_VALUES", "values must map parameter keys to numbers or arrays.");
    }
    for (auto it = values.begin(); it != values.end(); ++it) {
        const auto param = std::find_if(effect->params.begin(), effect->params.end(),
                                        [&](const VFX::ParamDesc& candidate) { return candidate.key == it.key(); });
        if (param == effect->params.end()) {
            return OperationFailure("UNKNOWN_PARAMETER", "Unknown parameter: " + it.key());
        }
        POST::PostParameterValue value{};
        bool clamped = false;
        if (!ReadParameterValue(*param, *it, value, clamped)) {
            return OperationFailure("INVALID_VALUE",
                                    "Parameter " + it.key() + " expects a number or an array matching components.");
        }
        if (clamped) {
            adjusted.push_back(it.key());
        }
        POST::SetPostParameterValue(*param, edited, value);
    }
    if (!document.Apply(std::move(edited), message)) {
        return OperationFailure("EDIT_REJECTED", message);
    }
    editor.PublishDraft(*context.store);
    return OperationOk("Post profile edited; save it with post.profile.save.",
                       {{"profileId", id}, {"dirty", document.IsDirty()}, {"clamped", std::move(adjusted)}});
}

EditorOperationResult SaveProfile(GlobalPostEditor& editor, const SceneProvider& provider,
                                  const nlohmann::json& parameters) {
    PostContext context;
    EditorOperationResult failure;
    if (!ResolveContext(provider, context, failure)) {
        return failure;
    }
    PostProfileEditorDocument& document = editor.Document();
    if (!document.IsOpen() || document.Guid().value != parameters.value("profileId", std::string{})) {
        return OperationFailure("PROFILE_NOT_OPEN", "Edit the profile with post.profile.edit before saving it.");
    }
    std::string message;
    if (!document.Save(context.scene->GetAssetDatabase(), message)) {
        return OperationFailure("SAVE_FAILED", message);
    }
    editor.PublishDraft(*context.store);
    return OperationOk("Post profile saved.");
}
} // namespace

void RegisterGlobalPostOperations(EditorOperationRegistry& registry, GlobalPostEditor& editor, SceneProvider scene) {
    EditorOperationDefinition query{
        "post.query",
        "List global post profiles and effects, the scene's current selection and the profile being edited.",
        "scene",
        {},
        [&editor, scene](const nlohmann::json&) { return QueryPost(editor, scene); }};
    query.entryPoint = true;
    query.searchTerms = {"post", "postprocess", "profile", "后处理", "後処理"};
    registry.Register(std::move(query), EditorOperationKind::Query, false);
    registry.Register(
        {"post.profile.describe",
         "Describe a post profile: its effect and each parameter's components, range, default and current value.",
         "scene",
         {{"profileId", ParameterType::String, true, "Profile id from post.query"}},
         [&editor, scene](const nlohmann::json& p) { return DescribeProfile(editor, scene, p); }},
        EditorOperationKind::Query, false);
    EditorOperationDefinition create{
        "post.profile.create",
        "Save a new project post profile copied from an engine or project profile.",
        "scene",
        {{"sourceProfileId", ParameterType::String, true, "Profile to copy"},
         {"name", ParameterType::String, false, "Display name of the copy"}},
        [&editor, scene](const nlohmann::json& p) { return CreateProfile(editor, scene, p); }};
    create.permission = EditorOperationPermission::ModifyProjectFiles;
    registry.Register(std::move(create), EditorOperationKind::Mutation, false);
    registry.Register(
        {"post.profile.edit",
         "Edit a project post profile as an unsaved draft shown live in the scene. Values outside the declared range are clamped.",
         "scene",
         {{"profileId", ParameterType::String, true, "Project profile id"},
          {"values", ParameterType::Json, false, "Map of parameter key to a number or component array"},
          {"effectId", ParameterType::String, false, "Switch the effect; values restart from defaults"},
          {"resetDefaults", ParameterType::Bool, false, "Clear all overrides first"}},
         [&editor, scene](const nlohmann::json& p) { return EditProfile(editor, scene, p); }},
        EditorOperationKind::Mutation, false);
    EditorOperationDefinition save{"post.profile.save",
                                   "Save the edited project post profile.",
                                   "scene",
                                   {{"profileId", ParameterType::String, true, "Project profile id"}},
                                   [&editor, scene](const nlohmann::json& p) { return SaveProfile(editor, scene, p); }};
    save.permission = EditorOperationPermission::ModifyProjectFiles;
    registry.Register(std::move(save), EditorOperationKind::Mutation, false);
}

} // namespace HIKARI::EDITOR
