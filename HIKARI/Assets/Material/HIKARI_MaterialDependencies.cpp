#include "HIKARI_MaterialDependencies.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace HIKARI {

    namespace {

        void AddDependency(
            std::vector<MaterialAssetDependency>& dependencies,
            std::set<std::pair<MaterialDependencyKind, std::string>>& seen,
            const AssetGuid& guid,
            MaterialDependencyKind kind,
            std::string role) {

            if (!guid.IsValid() || !seen.emplace(kind, guid.value).second) {
                return;
            }
            dependencies.push_back(MaterialAssetDependency{
                guid,
                kind,
                std::move(role)
            });
        }

        void AddTextureSlot(
            std::vector<MaterialAssetDependency>& dependencies,
            std::set<std::pair<MaterialDependencyKind, std::string>>& seen,
            const MaterialTextureSlotData& slot,
            std::string role) {

            if (slot.useTexture) {
                AddDependency(dependencies, seen, slot.textureAssetGuid,
                    MaterialDependencyKind::Texture, std::move(role));
            }
        }

        void AddGraphDependencies(
            const MaterialGraphData& graph,
            std::vector<MaterialAssetDependency>& dependencies,
            std::set<std::pair<MaterialDependencyKind, std::string>>& seen) {

            for (const MaterialGraphResourceData& resource : graph.resources) {
                if (resource.enabled) {
                    AddDependency(dependencies, seen, resource.assetGuid,
                        resource.type == MaterialResourceType::Ramp1D ||
                            resource.type == MaterialResourceType::Ramp2D
                            ? MaterialDependencyKind::Ramp
                            : MaterialDependencyKind::Texture,
                        "Graph Resource " + resource.id);
                }
            }
            for (const MaterialGraphNodeData& node : graph.nodes) {
                if (node.kind == MaterialNodeKind::FunctionCall) {
                    AddDependency(dependencies, seen, node.assetReference,
                        MaterialDependencyKind::Function,
                        "Function Call " + node.id);
                }
            }
        }

    } // namespace

    std::vector<MaterialAssetDependency> CollectMaterialAssetDependencies(
        const MaterialAssetData& data) {

        std::vector<MaterialAssetDependency> dependencies{};
        std::set<std::pair<MaterialDependencyKind, std::string>> seen{};

        AddTextureSlot(dependencies, seen, data.pbr.baseColorTexture, "BaseColor");
        AddTextureSlot(dependencies, seen, data.pbr.normalTexture, "Normal");
        AddTextureSlot(dependencies, seen, data.pbr.metallicRoughnessTexture,
            "MetallicRoughness");
        AddTextureSlot(dependencies, seen, data.pbr.occlusionTexture, "Occlusion");
        AddTextureSlot(dependencies, seen, data.pbr.emissiveTexture, "Emissive");
        AddTextureSlot(dependencies, seen, data.pbr.specularTexture, "Specular");
        AddTextureSlot(dependencies, seen, data.pbr.specularColorTexture,
            "SpecularColor");
        AddDependency(dependencies, seen, data.profileAssetGuid,
            MaterialDependencyKind::Profile, "Material Profile");

        switch (data.kind) {
        case MaterialAssetKind::Material:
            AddGraphDependencies(data.graph, dependencies, seen);
            break;
        case MaterialAssetKind::Function:
            AddGraphDependencies(data.function.graph, dependencies, seen);
            break;
        case MaterialAssetKind::Instance:
            AddDependency(dependencies, seen, data.instance.parentMaterialGuid,
                MaterialDependencyKind::ParentMaterial, "Parent Material");
            for (const auto& resource : data.instance.resourceOverrides) {
                AddDependency(dependencies, seen, resource.assetGuid,
                    MaterialDependencyKind::Texture,
                    "Instance Resource " + resource.resourceId);
            }
            break;
        case MaterialAssetKind::Profile:
            for (const AssetGuid& function : data.profile.functionAssetGuids) {
                AddDependency(dependencies, seen, function,
                    MaterialDependencyKind::Function, "Profile Function");
            }
            for (const AssetGuid& ramp : data.profile.rampAssetGuids) {
                AddDependency(dependencies, seen, ramp,
                    MaterialDependencyKind::Ramp, "Profile Ramp");
            }
            break;
        case MaterialAssetKind::Ramp:
            break;
        }

        std::sort(dependencies.begin(), dependencies.end(), [](const auto& left, const auto& right) {
            return std::tie(left.kind, left.assetGuid.value, left.role) <
                std::tie(right.kind, right.assetGuid.value, right.role);
        });
        return dependencies;
    }

} // namespace HIKARI
