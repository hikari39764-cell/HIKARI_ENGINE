#include "Scene/Components/HIKARI_ColliderComponent.h"

#include <string>
#include <vector>

#include "Authoring/HIKARI_FieldInspector.h"
#include "Physics/HIKARI_PhysicsProjectSettings.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {

// 衝突の層と、当たる層の集合を、プロジェクトの層名（ProjectSettings/Physics/collision.json）で編集する。部件が持つのは
// 数値の層と集合だけで、名前は編集器が足す。層名が無い間は数値のまま編集する。
bool DrawColliderFilter(IInspectorBuilder& builder, const AuthoringFieldDescriptor& field,
                        PHYSICS::PhysicsCollisionFilter& filter, const ColliderSettings&) {
    const std::string layerId = std::string(field.id) + ".layer";
    const std::string maskId = std::string(field.id) + ".mask";
    const AuthoringFieldDescriptor layerField{layerId, "Collision Layer", {}, field.writable};
    const AuthoringFieldDescriptor maskField{maskId, "Collision Mask", {}, field.writable};

    const InspectorContext& context = builder.GetContext();
    const auto* projectSettings =
        context.worldServices != nullptr ? context.worldServices->Find<PHYSICS::PhysicsProjectSettings>() : nullptr;
    if (projectSettings == nullptr || projectSettings->GetLayers().empty()) {
        bool changed = FIELD_INSPECTOR::EditInteger(builder, layerField, filter.layer);
        changed |= FIELD_INSPECTOR::EditInteger(builder, maskField, filter.mask);
        return changed;
    }

    const auto& layers = projectSettings->GetLayers();
    std::vector<AuthoringChoice> layerChoices{};
    layerChoices.reserve(layers.size() + 1u);
    for (const PHYSICS::PhysicsCollisionLayerSetting& layer : layers) {
        layerChoices.push_back({layer.name, layer.name});
    }

    int selectedLayer = projectSettings->FindLayerIndex(filter.layer);
    int layerIndexOffset = 0;
    std::string unmappedLayerName{};
    if (selectedLayer < 0) {
        unmappedLayerName = "Unmapped (" + std::to_string(filter.layer) + ")";
        layerChoices.insert(layerChoices.begin(), {"unmapped", unmappedLayerName});
        selectedLayer = 0;
        layerIndexOffset = 1;
    }

    bool changed = false;
    if (builder.Choice(layerField, selectedLayer, layerChoices)) {
        const int configuredLayerIndex = selectedLayer - layerIndexOffset;
        if (configuredLayerIndex >= 0) {
            filter.layer = layers[static_cast<size_t>(configuredLayerIndex)].bit;
            changed = true;
        }
    }

    const int activeLayerIndex = projectSettings->FindLayerIndex(filter.layer);
    if (activeLayerIndex >= 0) {
        if (builder.Button("Use Layer Default Mask")) {
            filter.mask = layers[static_cast<size_t>(activeLayerIndex)].defaultMask;
            changed = true;
        }
    } else {
        builder.Text("This layer is not present in ProjectSettings/Physics/collision.json.");
    }

    for (const PHYSICS::PhysicsCollisionLayerSetting& layer : layers) {
        bool enabled = (filter.mask & layer.bit) != 0u;
        const std::string id = "collidesWith." + layer.name;
        const std::string label = "Collides With " + layer.name;
        if (!builder.Bool(AuthoringFieldDescriptor{id, label, {}, field.writable}, enabled)) {
            continue;
        }
        if (enabled) {
            filter.mask |= layer.bit;
        } else {
            filter.mask &= ~layer.bit;
        }
        changed = true;
    }
    return changed;
}

} // namespace HIKARI
