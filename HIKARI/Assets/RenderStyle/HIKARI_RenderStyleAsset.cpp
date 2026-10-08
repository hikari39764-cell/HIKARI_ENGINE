#include "Assets/RenderStyle/HIKARI_RenderStyleAsset.h"

#include <algorithm>
#include <utility>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Post/HIKARI_PostAsset.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"

namespace HIKARI::RENDERSTYLE {
namespace {
constexpr BuiltinRenderStyleContent kBuiltinRenderStyles[]{
    {"engine.style.realistic", "Realistic", "Realistic"},
    {"engine.style.anime", "Anime", "Anime"},
    {"engine.style.manga", "Manga", "Manga"},
    {"engine.style.painterly", "Painterly", "Painterly"},
    {"engine.style.sketch", "Sketch", "Sketch"},
    {"engine.style.pixel", "Pixel", "Pixel"},
    {"engine.style.retro_crt", "Retro CRT", "RetroCRT"},
    {"engine.style.cyber", "Cyber", "Cyber"},
};

bool ReadPostLayer(const nlohmann::json& node, RenderStylePostLayer& layer, std::string& error,
                   const std::string& path) {
    if (!node.is_object()) {
        return JSONSTRICT::Fail(error, path + " must be an object");
    }
    if (!JSONSTRICT::ReadString(node, "profile", {}, layer.profileId, error, path) ||
        !JSONSTRICT::ReadBool(node, "enabled", true, layer.enabled, error, path)) {
        return false;
    }
    if (layer.profileId.starts_with(POST::kBuiltinPostProfilePrefix)) {
        return true;
    }
    if (!IsValidAssetGuid(layer.profileId)) {
        return JSONSTRICT::Fail(error, path + ".profile must be an engine post profile ID or a project asset GUID");
    }
    return true;
}
} // namespace

bool ReadRenderStyleAsset(const nlohmann::json& root, RenderStyleAsset& out, std::string& error) {
    if (!JSONSTRICT::RequireFormatVersion(root, kRenderStyleFormatVersion, error)) {
        return false;
    }
    RenderStyleAsset style;
    if (!JSONSTRICT::ReadString(root, "name", style.name, style.name, error, "root")) {
        return false;
    }
    if (const auto shading = root.find("shading"); shading != root.end()) {
        if (!shading->is_object()) {
            return JSONSTRICT::Fail(error, "root.shading must be an object");
        }
        JSONFIELDS::ReadLenient(*shading, style.shading, kRenderStyleShadingFields);
        style.shading = JSONFIELDS::Sanitize(style.shading, kRenderStyleShadingFields);
    }
    if (const auto post = root.find("post"); post != root.end()) {
        if (!post->is_array() || post->size() > kMaxRenderStylePostLayers) {
            return JSONSTRICT::Fail(error, "root.post must be an array of at most " +
                                               std::to_string(kMaxRenderStylePostLayers) + " layers");
        }
        for (size_t index = 0; index < post->size(); ++index) {
            RenderStylePostLayer layer;
            if (!ReadPostLayer((*post)[index], layer, error, "root.post[" + std::to_string(index) + "]")) {
                return false;
            }
            style.post.push_back(std::move(layer));
        }
    }
    out = std::move(style);
    return true;
}

nlohmann::json WriteRenderStyleAsset(const RenderStyleAsset& style) {
    nlohmann::json post = nlohmann::json::array();
    for (const RenderStylePostLayer& layer : style.post) {
        post.push_back({{"profile", layer.profileId}, {"enabled", layer.enabled}});
    }
    nlohmann::json shading = nlohmann::json::object();
    JSONFIELDS::Write(shading, style.shading, kRenderStyleShadingFields);
    return {
        {"formatVersion", kRenderStyleFormatVersion},
        {"name", style.name},
        {"shading", std::move(shading)},
        {"post", std::move(post)},
    };
}

bool LoadRenderStyleAsset(const std::filesystem::path& path, RenderStyleAsset& out, std::string& error) {
    nlohmann::json root;
    return SERIALIZATION::JSON::ReadJsonFile(path, root, &error) && ReadRenderStyleAsset(root, out, error);
}

bool SaveRenderStyleAsset(const std::filesystem::path& path, const RenderStyleAsset& style, std::string& error) {
    return SERIALIZATION::JSON::WriteJsonFile(path, WriteRenderStyleAsset(style), &error);
}

std::vector<AssetDependencyDesc> CollectRenderStyleDependencies(const RenderStyleAsset& style) {
    std::vector<AssetDependencyDesc> dependencies;
    for (const RenderStylePostLayer& layer : style.post) {
        const AssetGuid guid{layer.profileId};
        if (!layer.profileId.starts_with(POST::kBuiltinPostProfilePrefix) &&
            std::none_of(dependencies.begin(), dependencies.end(),
                         [&](const AssetDependencyDesc& dependency) { return dependency.guid == guid; })) {
            dependencies.push_back({guid, {}, "RenderStyle.PostProfile"});
        }
    }
    return dependencies;
}

std::span<const BuiltinRenderStyleContent> GetBuiltinRenderStyles() noexcept { return kBuiltinRenderStyles; }

std::filesystem::path ResolveBuiltinRenderStylePath(std::string_view id) {
    for (const BuiltinRenderStyleContent& content : kBuiltinRenderStyles) {
        if (content.id == id) {
            return CONTENT::GetEngineContentContext().Resolve(std::filesystem::path("RenderStyles") /
                                                              (std::string(content.fileStem) + ".hrenderstyle"));
        }
    }
    return {};
}

} // namespace HIKARI::RENDERSTYLE
