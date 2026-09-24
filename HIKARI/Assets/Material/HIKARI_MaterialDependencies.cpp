#include "HIKARI_MaterialDependencies.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace HIKARI {
namespace {
template <class Data, class Visitor>
void VisitReferences(Data& data, Visitor&& visit, bool includeInactive) {
    using Kind = MaterialDependencyKind;
    const auto texture = [&](auto& slot, const char* role) {
        if (includeInactive || slot.useTexture)
            visit(slot.textureAssetGuid, Kind::Texture, role);
    };
    const auto graph = [&](auto& source) {
        for (auto& resource : source.resources) {
            if (includeInactive || resource.enabled)
                visit(resource.assetGuid,
                      resource.type == MaterialResourceType::Ramp1D || resource.type == MaterialResourceType::Ramp2D
                          ? Kind::Ramp
                          : Kind::Texture,
                      "Graph Resource " + resource.id);
        }
        for (auto& node : source.nodes) {
            if (includeInactive || node.kind == MaterialNodeKind::FunctionCall)
                visit(node.assetReference, Kind::Function, "Function Call " + node.id);
        }
    };
    texture(data.pbr.baseColorTexture, "BaseColor");
    texture(data.pbr.normalTexture, "Normal");
    texture(data.pbr.metallicRoughnessTexture, "MetallicRoughness");
    texture(data.pbr.occlusionTexture, "Occlusion");
    texture(data.pbr.emissiveTexture, "Emissive");
    texture(data.pbr.specularTexture, "Specular");
    texture(data.pbr.specularColorTexture, "SpecularColor");
    visit(data.profileAssetGuid, Kind::Profile, "Material Profile");
    if (includeInactive || data.kind == MaterialAssetKind::Material)
        graph(data.graph);
    if (includeInactive || data.kind == MaterialAssetKind::Function)
        graph(data.function.graph);
    if (includeInactive || data.kind == MaterialAssetKind::Instance) {
        visit(data.instance.parentMaterialGuid, Kind::ParentMaterial, "Parent Material");
        for (auto& resource : data.instance.resourceOverrides)
            visit(resource.assetGuid, Kind::Texture, "Instance Resource " + resource.resourceId);
    }
    if (includeInactive || data.kind == MaterialAssetKind::Profile) {
        for (auto& function : data.profile.functionAssetGuids)
            visit(function, Kind::Function, "Profile Function");
        for (auto& ramp : data.profile.rampAssetGuids)
            visit(ramp, Kind::Ramp, "Profile Ramp");
    }
}
} // namespace

void VisitMaterialAssetReferences(MaterialAssetData& data,
                                  const std::function<void(AssetGuid&, MaterialDependencyKind, std::string)>& visitor,
                                  bool includeInactive) {
    VisitReferences(data, visitor, includeInactive);
}

std::vector<MaterialAssetDependency> CollectMaterialAssetDependencies(const MaterialAssetData& data) {
    std::vector<MaterialAssetDependency> dependencies;
    std::set<std::pair<MaterialDependencyKind, std::string>> seen;
    VisitReferences(
        data,
        [&](const AssetGuid& guid, MaterialDependencyKind kind, std::string role) {
            if (guid.IsValid() && seen.emplace(kind, guid.value).second)
                dependencies.push_back({guid, kind, std::move(role)});
        },
        false);
    std::sort(dependencies.begin(), dependencies.end(), [](const auto& left, const auto& right) {
        return std::tie(left.kind, left.assetGuid.value, left.role) <
               std::tie(right.kind, right.assetGuid.value, right.role);
    });
    return dependencies;
}
} // namespace HIKARI
