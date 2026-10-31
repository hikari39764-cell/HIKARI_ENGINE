#include "Assets/Post/HIKARI_PostAsset.h"

#include <algorithm>
#include <utility>

#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"
#include "Vfx/Parameters/HIKARI_VfxParameterValues.h"
#include "Vfx/Serialization/HIKARI_VfxParameterJson.h"

namespace HIKARI::POST {
namespace {
constexpr BuiltinPostContent kBuiltinPostContent[]{
    {"engine.post.effect.ani", "engine.post.profile.ani", "Ani", "Ani"},
    {"engine.post.effect.crtdy", "engine.post.profile.crtdy", "CRTDY", "CRTDY"},
    {"engine.post.effect.cyber", "engine.post.profile.cyber", "CYBER", "CYBER"},
    {"engine.post.effect.manga", "engine.post.profile.manga", "Manga", "Manga"},
    {"engine.post.effect.pixel", "engine.post.profile.pixel", "Pixel", "Pixel"},
    {"engine.post.effect.painterly", "engine.post.profile.painterly", "Painterly", "Painterly"},
    {"engine.post.effect.sketch", "engine.post.profile.sketch", "Sketch", "Sketch"},
    {"engine.post.effect.outline", "engine.post.profile.outline", "Outline", "Outline"},
    {"engine.post.effect.lens", "engine.post.profile.lens", "Lens", "Lens"},
};

bool ReadPass(const nlohmann::json& node, PostEffectPhase defaultPhase, PostEffectPass& pass, std::string& error,
              const std::string& path) {
    if (!node.is_object()) {
        return JSONSTRICT::Fail(error, path + " must be an object");
    }
    if (!JSONSTRICT::ReadString(node, "name", {}, pass.name, error, path) ||
        !JSONSTRICT::ReadString(node, "programId", {}, pass.programId, error, path) ||
        !JSONSTRICT::ReadString(node, "shaderAssetGuid", {}, pass.shaderAssetGuid.value, error, path) ||
        !JSONSTRICT::ReadString(node, "entryId", "Main", pass.entryId, error, path)) {
        return false;
    }
    if (pass.name.empty() || pass.entryId.empty() || pass.programId.empty() == pass.shaderAssetGuid.value.empty()) {
        return JSONSTRICT::Fail(error, path + " requires name, entryId and exactly one of programId/shaderAssetGuid");
    }
    if (!pass.shaderAssetGuid.value.empty() && !IsValidAssetGuid(pass.shaderAssetGuid.value)) {
        return JSONSTRICT::Fail(error, path + ".shaderAssetGuid must be a project asset GUID");
    }
    std::string phase;
    if (!JSONSTRICT::ReadString(node, "phase", TEXT::EnumToName(kPostEffectPhaseNames, defaultPhase), phase, error,
                                path) ||
        !TEXT::TryParseEnum(kPostEffectPhaseNames, phase, pass.phase)) {
        return JSONSTRICT::Fail(error, path + ".phase must be PreTemporal, Scene or Display");
    }
    return true;
}

const VFX::ParamDesc* FindParameter(const PostEffectDefinition& effect, std::string_view key) {
    const auto found = std::find_if(effect.params.begin(), effect.params.end(),
                                    [key](const VFX::ParamDesc& param) { return param.key == key; });
    return found != effect.params.end() ? &*found : nullptr;
}

bool ReadScreenWarp(const nlohmann::json& node, PostEffectDefinition& effect, std::string& error) {
    PostScreenWarpBinding binding;
    if (!node.is_object()) {
        return JSONSTRICT::Fail(error, "screenWarp must be an object");
    }
    if (!JSONSTRICT::ReadString(node, "strength", {}, binding.strengthParam, error, "screenWarp") ||
        !JSONSTRICT::ReadString(node, "curvature", {}, binding.curvatureParam, error, "screenWarp")) {
        return false;
    }
    if (!FindParameter(effect, binding.strengthParam) || !FindParameter(effect, binding.curvatureParam)) {
        return JSONSTRICT::Fail(error, "screenWarp must reference declared parameters");
    }
    effect.screenWarp = std::move(binding);
    return true;
}
} // namespace

bool ReadPostEffectDefinition(const nlohmann::json& root, PostEffectDefinition& out, std::string& error) {
    if (!JSONSTRICT::RequireFormatVersion(root, kPostEffectFormatVersion, error)) {
        return false;
    }
    PostEffectDefinition effect;
    if (const auto inputs = root.find("inputs"); inputs != root.end()) {
        if (!inputs->is_array())
            return JSONSTRICT::Fail(error, "inputs must be an array");
        for (const auto& input : *inputs) {
            PostEffectInput kind{};
            if (!input.is_string() || !TEXT::TryParseEnum(kPostEffectInputNames, input.get<std::string>(), kind))
                return JSONSTRICT::Fail(error, "inputs supports SceneDepth and SurfaceFeatureMask");
            bool& required =
                kind == PostEffectInput::SceneDepth ? effect.requiresSceneDepth : effect.requiresSurfaceFeatureMask;
            if (required)
                return JSONSTRICT::Fail(error, "inputs lists " + input.get<std::string>() + " more than once");
            required = true;
        }
    }
    std::string phase;
    if (!JSONSTRICT::ReadString(root, "name", {}, effect.name, error, "root") ||
        !JSONSTRICT::ReadString(root, "phase", "Scene", phase, error, "root")) {
        return false;
    }
    PostEffectPhase defaultPhase{};
    if (!TEXT::TryParseEnum(kPostEffectPhaseNames, phase, defaultPhase)) {
        return JSONSTRICT::Fail(error, "root.phase must be PreTemporal, Scene or Display");
    }
    const auto passes = root.find("passes");
    if (passes == root.end() || !passes->is_array() || passes->empty()) {
        return JSONSTRICT::Fail(error, "passes must be a non-empty array");
    }
    for (size_t index = 0; index < passes->size(); ++index) {
        PostEffectPass pass;
        if (!ReadPass((*passes)[index], defaultPhase, pass, error, "passes[" + std::to_string(index) + "]")) {
            return false;
        }
        effect.passes.push_back(std::move(pass));
    }
    if (!VFX::SERIALIZATION::ReadParameterDescriptorsFromJson(root.value("params", nlohmann::json::array()),
                                                              kPostParameterSlotCount, effect.params, error)) {
        return false;
    }
    if (const auto warp = root.find("screenWarp"); warp != root.end() && !ReadScreenWarp(*warp, effect, error)) {
        return false;
    }
    out = std::move(effect);
    return true;
}

bool LoadPostEffectDefinition(const std::filesystem::path& path, PostEffectDefinition& out, std::string& error) {
    nlohmann::json root;
    return SERIALIZATION::JSON::ReadJsonFile(path, root, &error) && ReadPostEffectDefinition(root, out, error);
}

bool ReadPostProfileAsset(const nlohmann::json& root, PostProfileAsset& out, std::string& error) {
    if (!JSONSTRICT::RequireFormatVersion(root, kPostProfileFormatVersion, error)) {
        return false;
    }
    PostProfileAsset profile;
    if (!JSONSTRICT::ReadString(root, "name", profile.name, profile.name, error, "root") ||
        !JSONSTRICT::ReadString(root, "effect", {}, profile.effectId, error, "root")) {
        return false;
    }
    if (profile.effectId.empty()) {
        return JSONSTRICT::Fail(error, "root.effect is required");
    }
    const auto values = root.find("values");
    if (values != root.end()) {
        if (!values->is_object()) {
            return JSONSTRICT::Fail(error, "values must be an object");
        }
        for (const auto& [key, node] : values->items()) {
            PostParameterValue value{};
            if (!node.is_array() || node.empty() || node.size() > 4u) {
                return JSONSTRICT::Fail(error, "values." + key + " must be an array of one to four numbers");
            }
            for (size_t index = 0; index < node.size(); ++index) {
                if (!node[index].is_number() || !JSONSTRICT::IsFloatRange(node[index].get<double>())) {
                    return JSONSTRICT::Fail(error, "values." + key + " must contain finite numbers");
                }
                value[index] = node[index].get<float>();
            }
            profile.values.emplace(key, value);
        }
    }
    out = std::move(profile);
    return true;
}

nlohmann::json WritePostProfileAsset(const PostProfileAsset& profile) {
    nlohmann::json values = nlohmann::json::object();
    for (const auto& [key, value] : profile.values) {
        values[key] = nlohmann::json::array({value[0], value[1], value[2], value[3]});
    }
    return {
        {"formatVersion", kPostProfileFormatVersion},
        {"name", profile.name},
        {"effect", profile.effectId},
        {"values", std::move(values)},
    };
}

bool LoadPostProfileAsset(const std::filesystem::path& path, PostProfileAsset& out, std::string& error) {
    nlohmann::json root;
    return SERIALIZATION::JSON::ReadJsonFile(path, root, &error) && ReadPostProfileAsset(root, out, error);
}

bool SavePostProfileAsset(const std::filesystem::path& path, const PostProfileAsset& profile, std::string& error) {
    return SERIALIZATION::JSON::WriteJsonFile(path, WritePostProfileAsset(profile), &error);
}

std::vector<AssetDependencyDesc> CollectPostProfileDependencies(const PostProfileAsset& profile) {
    if (profile.effectId.empty() || profile.effectId.starts_with(kBuiltinPostEffectPrefix)) {
        return {};
    }
    return {{AssetGuid{profile.effectId}, {}, "PostProfile.Effect"}};
}

std::vector<AssetDependencyDesc> CollectPostEffectDependencies(const PostEffectDefinition& effect) {
    std::vector<AssetDependencyDesc> dependencies;
    for (const auto& pass : effect.passes) {
        if (pass.shaderAssetGuid.IsValid() &&
            std::none_of(dependencies.begin(), dependencies.end(),
                         [&](const auto& dependency) { return dependency.guid == pass.shaderAssetGuid; }))
            dependencies.push_back({pass.shaderAssetGuid, {}, "PostEffect.Shader"});
    }
    return dependencies;
}

bool ValidatePostProfileValues(const PostProfileAsset& profile, const PostEffectDefinition& effect,
                               std::string& error) {
    for (const auto& [key, value] : profile.values) {
        if (!FindParameter(effect, key)) {
            return JSONSTRICT::Fail(error, "post profile value '" + key + "' is not a parameter of " + effect.name);
        }
    }
    return true;
}

PostParameterValue GetPostParameterValue(const VFX::ParamDesc& param, const PostProfileAsset* profile) {
    if (profile) {
        if (const auto found = profile->values.find(param.key); found != profile->values.end()) {
            return found->second;
        }
    }
    return {param.defaultValues[0], param.defaultValues[1], param.defaultValues[2], param.defaultValues[3]};
}

void SetPostParameterValue(const VFX::ParamDesc& param, PostProfileAsset& profile, const PostParameterValue& value) {
    profile.values.insert_or_assign(param.key, value);
}

PostParameterBlock ResolvePostParameters(const PostEffectDefinition& effect, const PostProfileAsset* profile) {
    PostParameterBlock block{};
    for (const VFX::ParamDesc& param : effect.params) {
        const PostParameterValue value = GetPostParameterValue(param, profile);
        VFX::WriteParameterValue(param, value.data(), block.data(), block.size());
    }
    return block;
}

std::optional<float> ReadPostParameter(const PostEffectDefinition& effect, std::span<const DirectX::XMFLOAT4> values,
                                       std::string_view key) {
    const VFX::ParamDesc* param = FindParameter(effect, key);
    if (!param || param->ref.slot >= values.size() || param->ref.channel >= 4u) {
        return std::nullopt;
    }
    const float* channels = &values[param->ref.slot].x;
    return channels[param->ref.channel];
}

std::span<const BuiltinPostContent> GetBuiltinPostContent() noexcept { return kBuiltinPostContent; }

std::filesystem::path ResolveBuiltinPostEffectPath(std::string_view effectId) {
    for (const BuiltinPostContent& content : kBuiltinPostContent) {
        if (content.effectId == effectId) {
            return CONTENT::GetEngineContentContext().Resolve(std::filesystem::path("Post") / "Effects" /
                                                              (std::string(content.fileStem) + ".hposteffect"));
        }
    }
    return {};
}

std::filesystem::path ResolveBuiltinPostProfilePath(std::string_view profileId) {
    for (const BuiltinPostContent& content : kBuiltinPostContent) {
        if (content.profileId == profileId) {
            return CONTENT::GetEngineContentContext().Resolve(std::filesystem::path("Post") / "Profiles" /
                                                              (std::string(content.fileStem) + ".hpostprofile"));
        }
    }
    return {};
}

} // namespace HIKARI::POST
