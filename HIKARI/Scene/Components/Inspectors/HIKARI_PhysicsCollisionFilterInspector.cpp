// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/Inspectors/HIKARI_PhysicsCollisionFilterInspector.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Physics/HIKARI_PhysicsProjectSettings.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {

bool DrawPhysicsCollisionFilterInspector(IInspectorBuilder& builder, uint32_t& collisionLayer,
                                         uint32_t& collisionMask) {
    const InspectorContext& context = builder.GetContext();
    const auto* projectSettings =
        context.worldServices != nullptr ? context.worldServices->Find<PHYSICS::PhysicsProjectSettings>() : nullptr;
    if (projectSettings == nullptr || projectSettings->GetLayers().empty()) {
        int layer = static_cast<int>(collisionLayer);
        int mask = static_cast<int>(collisionMask);
        bool changed = false;
        if (builder.Int(AuthoringFieldDescriptor{"collisionLayer", "Collision Layer"}, layer)) {
            collisionLayer = static_cast<uint32_t>((std::max)(layer, 1));
            changed = true;
        }
        if (builder.Int(AuthoringFieldDescriptor{"collisionMask", "Collision Mask"}, mask)) {
            collisionMask = static_cast<uint32_t>(mask);
            changed = true;
        }
        return changed;
    }

    const auto& layers = projectSettings->GetLayers();
    std::vector<AuthoringChoice> layerChoices{};
    layerChoices.reserve(layers.size() + 1u);
    for (const PHYSICS::PhysicsCollisionLayerSetting& layer : layers) {
        layerChoices.push_back({layer.name, layer.name});
    }

    int selectedLayer = projectSettings->FindLayerIndex(collisionLayer);
    int layerIndexOffset = 0;
    std::string unmappedLayerName{};
    if (selectedLayer < 0) {
        unmappedLayerName = "Unmapped (" + std::to_string(collisionLayer) + ")";
        layerChoices.insert(layerChoices.begin(), {"unmapped", unmappedLayerName});
        selectedLayer = 0;
        layerIndexOffset = 1;
    }

    bool changed = false;
    if (builder.Choice(AuthoringFieldDescriptor{"collisionLayer", "Collision Layer"}, selectedLayer, layerChoices)) {
        const int configuredLayerIndex = selectedLayer - layerIndexOffset;
        if (configuredLayerIndex >= 0) {
            collisionLayer = layers[static_cast<size_t>(configuredLayerIndex)].bit;
            changed = true;
        }
    }

    const int activeLayerIndex = projectSettings->FindLayerIndex(collisionLayer);
    if (activeLayerIndex >= 0) {
        if (builder.Button("Use Layer Default Mask")) {
            collisionMask = layers[static_cast<size_t>(activeLayerIndex)].defaultMask;
            changed = true;
        }
    } else {
        builder.Text("This layer is not present in ProjectSettings/Physics/collision.json.");
    }

    for (const PHYSICS::PhysicsCollisionLayerSetting& layer : layers) {
        bool enabled = (collisionMask & layer.bit) != 0u;
        const std::string id = "collidesWith." + layer.name;
        const std::string label = "Collides With " + layer.name;
        if (!builder.Bool(AuthoringFieldDescriptor{id, label}, enabled)) {
            continue;
        }
        if (enabled) {
            collisionMask |= layer.bit;
        } else {
            collisionMask &= ~layer.bit;
        }
        changed = true;
    }
    return changed;
}

} // namespace HIKARI

#endif // defined(HIKARI_WITH_EDITOR)
