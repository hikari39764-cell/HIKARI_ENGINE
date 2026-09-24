#include "Runtime/Binding/Adapters/HIKARI_RuntimeMaterialProjection.h"

#include <string>
#include <unordered_set>

#include "Core/HIKARI_Logger.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/Program/HIKARI_MaterialParameterProjection.h"
#include "Runtime/Binding/Adapters/HIKARI_RuntimeMaterialValueAdapter.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::BINDING::MATERIAL_ADAPTER {

std::shared_ptr<const RENDER3D::MATERIAL::MaterialExternalValueSnapshot> BuildMaterialExternalValueSnapshot(
    const Snapshot& snapshot) {
    auto projected = std::make_shared<RENDER3D::MATERIAL::MaterialExternalValueSnapshot>();
    projected->sourceInstanceId = snapshot.storeInstanceId;
    projected->revision = snapshot.revision;
    projected->values.reserve(snapshot.values.size());
    for (const auto& [id, source] : snapshot.values) {
        const std::optional<MaterialValueType> targetType = ToMaterialValueType(source.type);
        if (!targetType.has_value()) {
            continue;
        }
        MaterialValueData converted{};
        if (TryConvertValue(source, *targetType, converted)) {
            projected->values.emplace(id, converted);
        }
    }
    return projected;
}

bool ApplyExternalValuesToRuntimeMaterial(Material& material,
                                          const RENDER3D::MATERIAL::MaterialExternalValueSnapshot& snapshot) {
    std::string error{};
    const bool changed = RENDER3D::MATERIAL::ApplyMaterialExternalValues(material, snapshot, error);
    if (!error.empty()) {
        HIKARI_LOG_ERROR("[RuntimeBinding][Material] parameter projection failed: " + error);
    }
    return changed;
}

uint32_t ApplyExternalValuesToRuntimeMaterials(World& world,
                                               const RENDER3D::MATERIAL::MaterialExternalValueSnapshot& snapshot) {
    std::unordered_set<Material*> visited{};
    uint32_t projectedCount = 0u;
    world.ForEachObjectWith<ModelComponent>([&](GameObject&, ModelComponent& model) {
        for (const RuntimeMaterialOverrideSlot& slot : model.GetRuntimeMaterialOverrides()) {
            Material* material = slot.material.get();
            if (material == nullptr || !visited.emplace(material).second) {
                continue;
            }
            projectedCount += ApplyExternalValuesToRuntimeMaterial(*material, snapshot) ? 1u : 0u;
        }
    });
    return projectedCount;
}

} // namespace HIKARI::BINDING::MATERIAL_ADAPTER
