#include "Assets/Material/Authoring/HIKARI_MaterialInstanceResolver.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace HIKARI::ASSETS::MATERIAL {

    namespace {

        void AddError(
            MaterialInstanceResolveResult& result,
            std::string code,
            std::string message,
            std::string subjectId = {}) {

            result.diagnostics.push_back(MaterialCompileDiagnostic{
                MaterialCompileDiagnosticSeverity::Error,
                std::move(code), std::move(message), std::move(subjectId)
            });
        }

        bool IsFinite(const MaterialValueData& value) noexcept {
            if (value.type == MaterialValueType::Bool) {
                return true;
            }
            return IsMaterialNumericType(value.type) &&
                std::isfinite(value.numeric.x) &&
                std::isfinite(value.numeric.y) &&
                std::isfinite(value.numeric.z) &&
                std::isfinite(value.numeric.w);
        }

        MaterialGraphParameterData* FindParameter(
            MaterialAssetData& material,
            std::string_view id) noexcept {

            const auto found = std::find_if(
                material.graph.parameters.begin(),
                material.graph.parameters.end(),
                [id](const MaterialGraphParameterData& value) {
                    return value.id == id;
                });
            return found == material.graph.parameters.end()
                ? nullptr : &*found;
        }

        const MaterialGraphParameterData* FindParameter(
            const MaterialAssetData& material,
            std::string_view id) noexcept {

            const auto found = std::find_if(
                material.graph.parameters.begin(),
                material.graph.parameters.end(),
                [id](const MaterialGraphParameterData& value) {
                    return value.id == id;
                });
            return found == material.graph.parameters.end()
                ? nullptr : &*found;
        }

        MaterialGraphResourceData* FindResource(
            MaterialAssetData& material,
            std::string_view id) noexcept {

            const auto found = std::find_if(
                material.graph.resources.begin(),
                material.graph.resources.end(),
                [id](const MaterialGraphResourceData& value) {
                    return value.id == id;
                });
            return found == material.graph.resources.end()
                ? nullptr : &*found;
        }

        const MaterialGraphResourceData* FindResource(
            const MaterialAssetData& material,
            std::string_view id) noexcept {

            const auto found = std::find_if(
                material.graph.resources.begin(),
                material.graph.resources.end(),
                [id](const MaterialGraphResourceData& value) {
                    return value.id == id;
                });
            return found == material.graph.resources.end()
                ? nullptr : &*found;
        }

        void ApplyPbrParameter(
            MaterialAssetData& material,
            std::string_view id,
            const MaterialValueData& value) noexcept {

            if (id == "parameter.baseColor") {
                material.pbr.baseColorFactor = value.numeric;
            } else if (id == "parameter.metallic") {
                material.pbr.metallicFactor = value.numeric.x;
            } else if (id == "parameter.roughness") {
                material.pbr.roughnessFactor = value.numeric.x;
            } else if (id == "parameter.specular") {
                material.pbr.specularFactor = value.numeric.x;
            } else if (id == "parameter.specularColor") {
                material.pbr.specularColorFactor = {
                    value.numeric.x, value.numeric.y, value.numeric.z
                };
            } else if (id == "parameter.normalScale") {
                material.pbr.normalScale = value.numeric.x;
            } else if (id == "parameter.occlusionStrength") {
                material.pbr.occlusionStrength = value.numeric.x;
            } else if (id == "parameter.emissive") {
                material.pbr.emissiveFactor = {
                    value.numeric.x, value.numeric.y, value.numeric.z
                };
            } else if (id == "parameter.emissiveStrength") {
                material.pbr.emissiveStrength = value.numeric.x;
            }
        }

        MaterialTextureSlotData* SelectPbrTextureSlot(
            MaterialAssetData& material,
            MaterialResourceSemantic semantic) noexcept {

            switch (semantic) {
            case MaterialResourceSemantic::BaseColor:
                return &material.pbr.baseColorTexture;
            case MaterialResourceSemantic::Normal:
                return &material.pbr.normalTexture;
            case MaterialResourceSemantic::MetallicRoughness:
                return &material.pbr.metallicRoughnessTexture;
            case MaterialResourceSemantic::Occlusion:
                return &material.pbr.occlusionTexture;
            case MaterialResourceSemantic::Emissive:
                return &material.pbr.emissiveTexture;
            case MaterialResourceSemantic::Specular:
                return &material.pbr.specularTexture;
            case MaterialResourceSemantic::SpecularColor:
                return &material.pbr.specularColorTexture;
            default:
                return nullptr;
            }
        }

        bool ApplyOverrides(
            const MaterialAssetData& instance,
            MaterialAssetData& material,
            MaterialInstanceResolveResult& result) {

            bool valid = true;
            std::unordered_set<std::string> parameterIds{};
            for (const auto& overrideData :
                instance.instance.parameterOverrides) {
                if (!parameterIds.emplace(overrideData.parameterId).second) {
                    AddError(result, "Material.DuplicateInstanceParameter",
                        "material instance contains a duplicate parameter override",
                        overrideData.parameterId);
                    valid = false;
                    continue;
                }
                MaterialGraphParameterData* parameter =
                    FindParameter(material, overrideData.parameterId);
                if (parameter == nullptr) {
                    AddError(result, "Material.UnknownInstanceParameter",
                        "material instance parameter is not declared by its parent",
                        overrideData.parameterId);
                    valid = false;
                    continue;
                }
                if (parameter->defaultValue.type != overrideData.value.type ||
                    !IsFinite(overrideData.value)) {
                    AddError(result, "Material.InstanceParameterTypeMismatch",
                        "material instance parameter type or value is invalid",
                        overrideData.parameterId);
                    valid = false;
                    continue;
                }
                parameter->defaultValue = overrideData.value;
                ApplyPbrParameter(
                    material, overrideData.parameterId, overrideData.value);
            }

            std::unordered_set<std::string> resourceIds{};
            for (const auto& overrideData :
                instance.instance.resourceOverrides) {
                if (!resourceIds.emplace(overrideData.resourceId).second) {
                    AddError(result, "Material.DuplicateInstanceResource",
                        "material instance contains a duplicate resource override",
                        overrideData.resourceId);
                    valid = false;
                    continue;
                }
                MaterialGraphResourceData* resource =
                    FindResource(material, overrideData.resourceId);
                if (resource == nullptr) {
                    AddError(result, "Material.UnknownInstanceResource",
                        "material instance resource is not declared by its parent",
                        overrideData.resourceId);
                    valid = false;
                    continue;
                }
                if (!overrideData.assetGuid.IsValid()) {
                    AddError(result, "Material.InvalidInstanceResourceAsset",
                        "material instance resource override requires a valid asset GUID",
                        overrideData.resourceId);
                    valid = false;
                    continue;
                }
                resource->assetGuid = overrideData.assetGuid;
                resource->enabled = true;
                if (MaterialTextureSlotData* slot =
                    SelectPbrTextureSlot(material, resource->semantic)) {
                    slot->textureAssetGuid = overrideData.assetGuid;
                    slot->useTexture = true;
                    slot->texCoord = static_cast<int>(resource->texCoord);
                    slot->uvScale = resource->uvScale;
                    slot->uvOffset = resource->uvOffset;
                    slot->uvRotation = resource->uvRotation;
                }
            }
            return valid;
        }

        bool ResolveRecursive(
            const MaterialAssetData& source,
            const MaterialAssetLookup& lookup,
            uint32_t depth,
            uint32_t maxDepth,
            std::unordered_set<std::string>& visited,
            MaterialInstanceResolveResult& result,
            MaterialAssetData& outMaterial) {

            if (source.kind == MaterialAssetKind::Material) {
                outMaterial = source;
                return true;
            }
            if (source.kind != MaterialAssetKind::Instance) {
                AddError(result, "Material.InvalidInstanceParentKind",
                    "material instance parent must resolve to a Material or Instance");
                return false;
            }
            if (depth >= maxDepth) {
                AddError(result, "Material.InstanceDepthExceeded",
                    "material instance parent chain exceeds the bounded depth");
                return false;
            }
            const AssetGuid parentGuid = source.instance.parentMaterialGuid;
            if (!parentGuid.IsValid()) {
                AddError(result, "Material.MissingParent",
                    "material instance requires a valid parent Material GUID");
                return false;
            }
            if (!visited.emplace(parentGuid.value).second) {
                AddError(result, "Material.InstanceCycle",
                    "material instance parent chain contains a cycle",
                    parentGuid.value);
                return false;
            }
            MaterialAssetData parent{};
            std::string lookupError{};
            if (!lookup || !lookup(parentGuid, parent, lookupError)) {
                AddError(result, "Material.ParentLoadFailed",
                    lookupError.empty()
                        ? "material instance parent could not be loaded"
                        : std::move(lookupError),
                    parentGuid.value);
                return false;
            }
            if (!ResolveRecursive(
                    parent, lookup, depth + 1u, maxDepth,
                    visited, result, outMaterial)) {
                return false;
            }
            const bool valid = ApplyOverrides(source, outMaterial, result);
            outMaterial.materialName = source.materialName;
            outMaterial.pbr.materialName = source.materialName;
            return valid;
        }

    } // namespace

    MaterialInstanceResolveResult ResolveMaterialInstance(
        const MaterialAssetData& instanceSource,
        const MaterialAssetLookup& lookup,
        uint32_t maxParentDepth,
        const MaterialCompileOptions& compileOptions) {

        MaterialInstanceResolveResult result{};
        if (instanceSource.kind != MaterialAssetKind::Instance) {
            AddError(result, "Material.NotAnInstance",
                "ResolveMaterialInstance requires a Material Instance source");
            return result;
        }
        if (maxParentDepth == 0u) {
            AddError(result, "Material.InstanceDepthExceeded",
                "material instance maximum parent depth must be positive");
            return result;
        }
        std::unordered_set<std::string> visited{};
        result.success = ResolveRecursive(
            instanceSource, lookup, 0u, maxParentDepth,
            visited, result, result.resolvedMaterial);
        if (result.success) {
            result.compile = CompileMaterialAsset(
                result.resolvedMaterial, compileOptions);
            result.success = result.compile.success;
        }
        return result;
    }

    bool SetMaterialInstanceParameterOverride(
        MaterialAssetData& instanceSource,
        const MaterialAssetData& parentMaterial,
        std::string_view parameterId,
        const MaterialValueData& value,
        std::string& outError) {

        const MaterialGraphParameterData* parent =
            FindParameter(parentMaterial, parameterId);
        if (instanceSource.kind != MaterialAssetKind::Instance ||
            parentMaterial.kind != MaterialAssetKind::Material ||
            parent == nullptr || parent->defaultValue.type != value.type ||
            !IsFinite(value)) {
            outError = "parameter override is not declared by the parent with a compatible finite type";
            return false;
        }
        auto& values = instanceSource.instance.parameterOverrides;
        const auto found = std::find_if(values.begin(), values.end(),
            [parameterId](const auto& item) {
                return item.parameterId == parameterId;
            });
        if (found == values.end()) {
            values.push_back({ std::string(parameterId), value });
        } else {
            found->value = value;
        }
        std::sort(values.begin(), values.end(),
            [](const auto& left, const auto& right) {
                return left.parameterId < right.parameterId;
            });
        outError.clear();
        return true;
    }

    bool SetMaterialInstanceResourceOverride(
        MaterialAssetData& instanceSource,
        const MaterialAssetData& parentMaterial,
        std::string_view resourceId,
        const AssetGuid& assetGuid,
        std::string& outError) {

        if (instanceSource.kind != MaterialAssetKind::Instance ||
            parentMaterial.kind != MaterialAssetKind::Material ||
            FindResource(parentMaterial, resourceId) == nullptr ||
            !assetGuid.IsValid()) {
            outError = "resource override is not declared by the parent or has an invalid asset GUID";
            return false;
        }
        auto& values = instanceSource.instance.resourceOverrides;
        const auto found = std::find_if(values.begin(), values.end(),
            [resourceId](const auto& item) {
                return item.resourceId == resourceId;
            });
        if (found == values.end()) {
            values.push_back({ std::string(resourceId), assetGuid });
        } else {
            found->assetGuid = assetGuid;
        }
        std::sort(values.begin(), values.end(),
            [](const auto& left, const auto& right) {
                return left.resourceId < right.resourceId;
            });
        outError.clear();
        return true;
    }

    bool RestoreMaterialInstanceParameterDefault(
        MaterialAssetData& instanceSource,
        std::string_view parameterId) noexcept {

        auto& values = instanceSource.instance.parameterOverrides;
        const size_t oldSize = values.size();
        std::erase_if(values, [parameterId](const auto& item) {
            return item.parameterId == parameterId;
        });
        return values.size() != oldSize;
    }

    bool RestoreMaterialInstanceResourceDefault(
        MaterialAssetData& instanceSource,
        std::string_view resourceId) noexcept {

        auto& values = instanceSource.instance.resourceOverrides;
        const size_t oldSize = values.size();
        std::erase_if(values, [resourceId](const auto& item) {
            return item.resourceId == resourceId;
        });
        return values.size() != oldSize;
    }

} // namespace HIKARI::ASSETS::MATERIAL
