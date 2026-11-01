// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/HIKARI_ColliderComponent.h"

#include <string>
#include <utility>
#include <vector>

#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "Authoring/HIKARI_FieldInspector.h"
#include "Physics/HIKARI_PhysicsBodyValidator.h"
#include "Physics/HIKARI_PhysicsProjectSettings.h"
#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {

void ColliderComponent::BuildInspector(IInspectorBuilder& builder) {
    ColliderSettings next = settings_;
    bool changed = InspectFields(builder, next, kColliderFields);
    const InspectorContext& context = builder.GetContext();

    if (next.fitMode == ColliderFitMode::CollisionAsset) {
        const ModelAssetDescriptor* descriptor =
            context.assetRegistry != nullptr
                ? context.assetRegistry->FindAs<ModelAssetDescriptor>(AssetGuid{next.collisionAssetId})
                : nullptr;
        if (next.collisionAssetId.empty()) {
            builder.Text("Select a model and author its default collision in the Model Collision workspace.");
        } else if (descriptor == nullptr || descriptor->collisionGeometryPath.empty()) {
            builder.Text(
                "No model collision artifact. Open this model in the Model Collision workspace, add shapes, and save.");
        } else {
            builder.Text("Model collision ready: " + descriptor->collisionGeometryPath);
        }
    }

    const auto* projectSettings =
        context.worldServices != nullptr ? context.worldServices->Find<PHYSICS::PhysicsProjectSettings>() : nullptr;
    if (projectSettings != nullptr && !projectSettings->GetMaterialPresets().empty()) {
        std::vector<AuthoringChoice> presets{{"Custom", "Custom"}};
        for (const PHYSICS::PhysicsMaterialPreset& preset : projectSettings->GetMaterialPresets()) {
            presets.push_back({preset.name, preset.name});
        }
        PHYSICS::PhysicsMaterialDesc& material = next.material;
        int selectedPreset =
            projectSettings->FindMaterialPreset(material.friction, material.restitution, material.density) + 1;
        if (builder.Choice(AuthoringFieldDescriptor{"materialPreset", "Material Preset",
                                                    "Copies friction, restitution and density from a project preset."},
                           selectedPreset, presets) &&
            selectedPreset > 0) {
            const PHYSICS::PhysicsMaterialPreset& preset =
                projectSettings->GetMaterialPresets()[static_cast<size_t>(selectedPreset - 1)];
            material.friction = preset.friction;
            material.restitution = preset.restitution;
            material.density = preset.density;
            changed = true;
        }
    }
    if (changed) {
        settings_ = NormalizeColliderSettings(std::move(next));
    }

    const auto* statusService = context.worldServices != nullptr
                                    ? context.worldServices->Find<PHYSICS::PhysicsRuntimeStatusService>()
                                    : nullptr;
    const PHYSICS::PhysicsBodyRuntimeStatus* runtimeStatus =
        statusService != nullptr && context.runtimeObject != nullptr
            ? statusService->FindBodyStatus(*context.runtimeObject)
            : nullptr;
    if (runtimeStatus != nullptr) {
        std::string runtimeLine = "Runtime: ";
        runtimeLine += PHYSICS::ToString(runtimeStatus->state);
        runtimeLine += " | ";
        runtimeLine += TEXT::EnumToName(PHYSICS::kPhysicsMotionTypeNames, runtimeStatus->effectiveMotionType);
        runtimeLine += " | ";
        runtimeLine += std::to_string(runtimeStatus->shapeCount);
        runtimeLine += " shape(s)";
        builder.Text(runtimeLine);
        if (runtimeStatus->error != PHYSICS::PhysicsErrorCode::None) {
            builder.Text(std::string(PHYSICS::ToString(runtimeStatus->error)) + ": " + runtimeStatus->message);
        }
    }
}

} // namespace HIKARI

#endif // defined(HIKARI_WITH_EDITOR)
