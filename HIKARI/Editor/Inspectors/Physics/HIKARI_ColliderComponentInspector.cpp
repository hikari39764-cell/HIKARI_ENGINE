#include "Scene/Components/HIKARI_ColliderComponent.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Editor/Inspectors/HIKARI_PhysicsCollisionFilterInspector.h"
#include "Physics/HIKARI_PhysicsBodyValidator.h"
#include "Physics/HIKARI_PhysicsProjectSettings.h"
#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {

    namespace {
        constexpr std::array<const char*, 3> kShapeTypeNames{
            "Box", "Sphere", "Capsule"
        };
        constexpr std::array<const char*, 3> kFitModeNames{
            "Manual", "Fit Object Geometry", "Model Default Collision"
        };

        void DrawVec3(
            IInspectorBuilder& builder,
            std::string_view prefix,
            MATH::Vec3& value) {
            const std::string base(prefix);
            builder.Float(base + " X", value.x);
            builder.Float(base + " Y", value.y);
            builder.Float(base + " Z", value.z);
        }
    }

    void ColliderComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        int fitMode = static_cast<int>(fitMode_);
        if (builder.Choice("Shape Source", fitMode, kFitModeNames)) {
            fitMode_ = static_cast<ColliderFitMode>(
                (std::clamp)(fitMode, 0, 2));
        }
        if (fitMode_ == ColliderFitMode::CollisionAsset) {
            builder.AssetIdPicker(
                "Collision Source Model",
                AssetType::Model,
                collisionGeometryAssetId_);
            const InspectorContext& context = builder.GetContext();
            const ModelAssetDescriptor* descriptor =
                context.assetRegistry != nullptr
                ? context.assetRegistry->FindAs<ModelAssetDescriptor>(
                    AssetId{ collisionGeometryAssetId_ })
                : nullptr;
            if (collisionGeometryAssetId_.empty()) {
                builder.Text(
                    "Select a model and author its default collision in the Model Collision workspace.");
            } else if (descriptor == nullptr ||
                descriptor->collisionGeometryPath.empty()) {
                builder.Text(
                    "No model collision artifact. Open this model in the Model Collision workspace, add shapes, and save.");
            } else {
                builder.Text(
                    "Model collision ready: " +
                    descriptor->collisionGeometryPath);
            }
        }
        if (fitMode_ == ColliderFitMode::Manual) {
            int shapeType = static_cast<int>(shapeType_);
            if (builder.Choice("Shape", shapeType, kShapeTypeNames)) {
                shapeType_ = static_cast<PHYSICS::PhysicsShapeType>(
                    (std::clamp)(shapeType, 0, 2));
            }
            DrawVec3(builder, "Center", center_);
            DrawVec3(builder, "Rotation", rotationEulerDegrees_);
            if (shapeType_ == PHYSICS::PhysicsShapeType::Box) {
                DrawVec3(builder, "Size", size_);
            } else {
                builder.Float("Radius", radius_);
                if (shapeType_ == PHYSICS::PhysicsShapeType::Capsule) {
                    builder.Float("Height", height_);
                }
            }
        }
        builder.Bool("Is Trigger", trigger_);
        const InspectorContext& context = builder.GetContext();
        const auto* projectSettings = context.worldServices != nullptr
            ? context.worldServices->Find<
                PHYSICS::PhysicsProjectSettings>()
            : nullptr;
        if (projectSettings != nullptr &&
            !projectSettings->GetMaterialPresets().empty()) {
            std::vector<const char*> presetNames{ "Custom" };
            for (const PHYSICS::PhysicsMaterialPreset& preset :
                    projectSettings->GetMaterialPresets()) {
                presetNames.push_back(preset.name.c_str());
            }
            int selectedPreset = projectSettings->FindMaterialPreset(
                friction_, restitution_, density_) + 1;
            if (builder.Choice(
                    "Material Preset",
                    selectedPreset,
                    presetNames) && selectedPreset > 0) {
                const PHYSICS::PhysicsMaterialPreset& preset =
                    projectSettings->GetMaterialPresets()[
                        static_cast<size_t>(selectedPreset - 1)];
                friction_ = preset.friction;
                restitution_ = preset.restitution;
                density_ = preset.density;
            }
        }
        builder.Float("Friction", friction_);
        builder.Float("Restitution", restitution_);
        builder.Float("Density", density_);
        DrawPhysicsCollisionFilterInspector(
            builder,
            collisionLayer_,
            collisionMask_);
        ClampSettings();

        const auto* statusService = context.worldServices != nullptr
            ? context.worldServices->Find<
                PHYSICS::PhysicsRuntimeStatusService>()
            : nullptr;
        const PHYSICS::PhysicsBodyRuntimeStatus* runtimeStatus =
            statusService != nullptr && context.runtimeObject != nullptr
            ? statusService->FindBodyStatus(*context.runtimeObject)
            : nullptr;
        if (runtimeStatus != nullptr) {
            std::string runtimeLine = "Runtime: ";
            runtimeLine += PHYSICS::ToString(runtimeStatus->state);
            runtimeLine += " | ";
            runtimeLine += PHYSICS::ToString(
                runtimeStatus->effectiveMotionType);
            runtimeLine += " | ";
            runtimeLine += std::to_string(runtimeStatus->shapeCount);
            runtimeLine += " shape(s)";
            builder.Text(runtimeLine);
            if (runtimeStatus->error != PHYSICS::PhysicsErrorCode::None) {
                builder.Text(
                    std::string(PHYSICS::ToString(runtimeStatus->error)) +
                    ": " + runtimeStatus->message);
            }
        }
    }

} // namespace HIKARI
