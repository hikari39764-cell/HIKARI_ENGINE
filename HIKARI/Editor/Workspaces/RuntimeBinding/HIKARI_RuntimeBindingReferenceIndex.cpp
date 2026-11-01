#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingReferenceIndex.h"

#include <string>

#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {

void ScanJsonReferences(const nlohmann::json& value, std::string_view bindingId, std::string path,
                        const RuntimeBindingReference& base, std::vector<RuntimeBindingReference>& outReferences) {
    if (value.is_string()) {
        if (value.get_ref<const std::string&>() == bindingId) {
            RuntimeBindingReference reference = base;
            reference.slotId = path;
            reference.slotLabel = path;
            outReferences.push_back(std::move(reference));
        }
        return;
    }
    if (value.is_object()) {
        for (auto found = value.begin(); found != value.end(); ++found) {
            const std::string childPath = path.empty() ? found.key() : path + "." + found.key();
            ScanJsonReferences(found.value(), bindingId, childPath, base, outReferences);
        }
        return;
    }
    if (!value.is_array()) {
        return;
    }
    for (size_t index = 0u; index < value.size(); ++index) {
        ScanJsonReferences(value[index], bindingId, path + "[" + std::to_string(index) + "]", base, outReferences);
    }
}

void CollectSceneReferences(const SceneDocument& document, std::string_view bindingId,
                            std::vector<RuntimeBindingReference>& outReferences) {
    for (const SceneObjectData& object : document.objects) {
        for (const SceneComponentData& component : object.components) {
            RuntimeBindingReference base{};
            base.targetKind = "scene.component";
            base.targetId = std::to_string(object.id.value);
            base.targetLabel = object.name.empty() ? "Object " + std::to_string(object.id.value) : object.name;
            base.access = "component binding";
            ScanJsonReferences(component.properties, bindingId, component.type, base, outReferences);
        }
    }
    for (const SceneSystemData& system : document.systems) {
        RuntimeBindingReference base{};
        base.targetKind = "scene.system";
        base.targetId = system.systemId;
        base.targetLabel = system.systemId;
        base.access = "system binding";
        ScanJsonReferences(system.settings, bindingId, system.systemId, base, outReferences);
    }
}

void CollectMaterialReferences(const AssetRegistry& registry, std::string_view bindingId,
                               std::vector<RuntimeBindingReference>& outReferences) {
    for (const AssetDescriptor* base : registry.CollectByType(AssetType::Material)) {
        const auto* material = dynamic_cast<const MaterialAssetDescriptor*>(base);
        if (material == nullptr) {
            continue;
        }
        for (const MaterialGraphNodeData& node : material->sourceData.graph.nodes) {
            if (node.kind != MaterialNodeKind::Variable || node.referenceId != bindingId) {
                continue;
            }
            RuntimeBindingReference reference{};
            reference.targetKind = "material.asset";
            reference.targetId = material->id.value;
            reference.targetLabel =
                material->sourceData.materialName.empty() ? material->sourcePath : material->sourceData.materialName;
            reference.slotId = node.id;
            reference.slotLabel = node.displayName.empty() ? node.id : node.displayName;
            reference.access = "material read";
            outReferences.push_back(std::move(reference));
        }
    }
}

} // namespace

std::vector<RuntimeBindingReference> CollectRuntimeBindingReferences(const DocumentSceneBase& scene,
                                                                     std::string_view bindingId) {
    std::vector<RuntimeBindingReference> references{};
    if (bindingId.empty()) {
        return references;
    }
    CollectSceneReferences(scene.GetSceneDocument(), bindingId, references);
    CollectMaterialReferences(scene.GetAssetRegistry(), bindingId, references);
    return references;
}

} // namespace HIKARI::EDITOR
