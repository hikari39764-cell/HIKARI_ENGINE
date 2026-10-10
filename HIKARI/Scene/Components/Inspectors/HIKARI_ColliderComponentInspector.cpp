// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/HIKARI_ColliderComponent.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Scene/Components/Inspectors/HIKARI_PhysicsCollisionFilterInspector.h"
#include "Physics/HIKARI_PhysicsBodyValidator.h"
#include "Physics/HIKARI_PhysicsProjectSettings.h"
#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {

namespace {
constexpr std::array<std::string_view, std::size(kColliderFitModeNames)> kFitModeLabels{"Manual", "Fit Object Geometry",
                                                                                        "Model Default Collision"};
} // namespace

void ColliderComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled"}, enabled_);
    (void)EnumChoice(builder, AuthoringFieldDescriptor{"fitMode", "Shape Source"}, fitMode_, kColliderFitModeNames,
                     &kFitModeLabels);
    if (fitMode_ == ColliderFitMode::CollisionAsset) {
        builder.AssetIdPicker(AuthoringFieldDescriptor{"collisionAssetId", "Collision Source Model"}, AssetType::Model,
                              collisionGeometryAssetId_);
        const InspectorContext& context = builder.GetContext();
        const ModelAssetDescriptor* descriptor =
            context.assetRegistry != nullptr
                ? context.assetRegistry->FindAs<ModelAssetDescriptor>(AssetId{collisionGeometryAssetId_})
                : nullptr;
        if (collisionGeometryAssetId_.empty()) {
            builder.Text("Select a model and author its default collision in the Model Collision workspace.");
        } else if (descriptor == nullptr || descriptor->collisionGeometryPath.empty()) {
            builder.Text(
                "No model collision artifact. Open this model in the Model Collision workspace, add shapes, and save.");
        } else {
            builder.Text("Model collision ready: " + descriptor->collisionGeometryPath);
        }
    }
    if (fitMode_ == ColliderFitMode::Manual) {
        (void)EnumChoice(builder, AuthoringFieldDescriptor{"shape", "Shape"}, shapeType_, kColliderShapeTypeNames);
        builder.Vec3(AuthoringFieldDescriptor{"center", "Center"}, center_.x, center_.y, center_.z);
        builder.Vec3(AuthoringFieldDescriptor{"rotation", "Rotation"}, rotationEulerDegrees_.x, rotationEulerDegrees_.y,
                     rotationEulerDegrees_.z);
        if (shapeType_ == PHYSICS::PhysicsShapeType::Box) {
            builder.Vec3(AuthoringFieldDescriptor{"size", "Size"}, size_.x, size_.y, size_.z);
        } else {
            builder.Float(AuthoringFieldDescriptor{"radius", "Radius"}, radius_);
            if (shapeType_ == PHYSICS::PhysicsShapeType::Capsule) {
                builder.Float(AuthoringFieldDescriptor{"height", "Height"}, height_);
            }
        }
    }
    builder.Bool(AuthoringFieldDescriptor{"trigger", "Is Trigger"}, trigger_);
    const InspectorContext& context = builder.GetContext();
    const auto* projectSettings =
        context.worldServices != nullptr ? context.worldServices->Find<PHYSICS::PhysicsProjectSettings>() : nullptr;
    if (projectSettings != nullptr && !projectSettings->GetMaterialPresets().empty()) {
        std::vector<AuthoringChoice> presets{{"Custom", "Custom"}};
        for (const PHYSICS::PhysicsMaterialPreset& preset : projectSettings->GetMaterialPresets()) {
            presets.push_back({preset.name, preset.name});
        }
        int selectedPreset = projectSettings->FindMaterialPreset(friction_, restitution_, density_) + 1;
        if (builder.Choice(AuthoringFieldDescriptor{"materialPreset", "Material Preset",
                                                    "Copies friction, restitution and density from a project preset."},
                           selectedPreset, presets) &&
            selectedPreset > 0) {
            const PHYSICS::PhysicsMaterialPreset& preset =
                projectSettings->GetMaterialPresets()[static_cast<size_t>(selectedPreset - 1)];
            friction_ = preset.friction;
            restitution_ = preset.restitution;
            density_ = preset.density;
        }
    }
    builder.Float(AuthoringFieldDescriptor{"friction", "Friction"}, friction_);
    builder.Float(AuthoringFieldDescriptor{"restitution", "Restitution"}, restitution_);
    builder.Float(AuthoringFieldDescriptor{"density", "Density"}, density_);
    DrawPhysicsCollisionFilterInspector(builder, collisionLayer_, collisionMask_);
    ClampSettings();

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
