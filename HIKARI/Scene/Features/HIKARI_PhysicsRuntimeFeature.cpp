#include "Scene/Features/HIKARI_PhysicsRuntimeFeature.h"

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "Core/HIKARI_JsonRead.h"
#include "Physics/HIKARI_PhysicsSystem.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

namespace HIKARI {
    namespace {
        PHYSICS::PhysicsWorldSettings ReadSettings(
            const nlohmann::json& settings) {
            PHYSICS::PhysicsWorldSettings result{};
            if (settings.contains("gravity")) {
                result.gravity = JSONREAD::Vec3Or(
                    settings["gravity"], result.gravity);
            }
            result.allowSleeping = settings.value(
                "allowSleeping", result.allowSleeping);
            result.emitPersistContactEvents = settings.value(
                "emitPersistContactEvents",
                result.emitPersistContactEvents);
            return result;
        }

        void NormalizeSettings(nlohmann::json& settings) {
            PHYSICS::PhysicsWorldSettings normalized =
                ReadSettings(settings);
            const auto finiteOr = [](float value, float fallback) {
                return std::isfinite(value) ? value : fallback;
            };
            normalized.gravity = {
                finiteOr(normalized.gravity.x, 0.0f),
                finiteOr(normalized.gravity.y, -9.81f),
                finiteOr(normalized.gravity.z, 0.0f)
            };
            settings["gravity"] = nlohmann::json::array({
                normalized.gravity.x,
                normalized.gravity.y,
                normalized.gravity.z
            });
            settings["allowSleeping"] = normalized.allowSleeping;
            settings["emitPersistContactEvents"] =
                normalized.emitPersistContactEvents;
        }

        ComponentTypePresentation PhysicsPresentation(
            const char* displayName,
            const char* description) {
            return ComponentTypePresentation{
                displayName,
                "Physics",
                description,
                std::string(RuntimeFeatureIds::Physics)
            };
        }

        class PhysicsRuntimeFeature final : public IRuntimeFeature {
        public:
            std::string_view GetFeatureId() const noexcept override {
                return RuntimeFeatureIds::Physics;
            }

            std::string_view GetDisplayName() const noexcept override {
                return "Physics";
            }

            std::string_view GetDescription() const noexcept override {
                return "Backend-independent bodies, colliders, queries, and contact events.";
            }

            std::span<const std::string_view>
                GetComponentTypeNames() const noexcept override {
                static constexpr std::array<std::string_view, 2> names{
                    "PhysicsBodyComponent",
                    "ColliderComponent"
                };
                return names;
            }

            std::span<const std::string_view>
                GetSystemIds() const noexcept override {
                static constexpr std::array<std::string_view, 1> ids{
                    "PhysicsSystem"
                };
                return ids;
            }

            bool Register(RuntimeFeatureContext& context) override {
                ComponentTypeInfo body{};
                body.typeName = "PhysicsBodyComponent";
                body.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<PhysicsBodyComponent>();
                };
                body.requiredComponents = { "ColliderComponent" };
                body.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["motionType"] = "Dynamic";
                    properties["mass"] = 1.0f;
                    properties["gravityScale"] = 1.0f;
                    properties["linearDamping"] = 0.05f;
                    properties["angularDamping"] = 0.05f;
                    properties["allowSleeping"] = true;
                    properties["continuousCollision"] = false;
                    properties["initialLinearVelocity"] = { 0.0f, 0.0f, 0.0f };
                    properties["initialAngularVelocity"] = { 0.0f, 0.0f, 0.0f };
                    properties["lockTranslation"] = { 0.0f, 0.0f, 0.0f };
                    properties["lockRotation"] = { 0.0f, 0.0f, 0.0f };
                };
                body.presentation = PhysicsPresentation(
                    "Physics Body",
                    "Defines static, kinematic, or dynamic body behavior.");

                ComponentTypeInfo collider{};
                collider.typeName = "ColliderComponent";
                collider.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<ColliderComponent>();
                };
                collider.optionalComponents = {
                    "PhysicsBodyComponent",
                    "ModelComponent"
                };
                collider.allowMultiple = true;
                collider.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["fitMode"] = "Manual";
                    properties["shape"] = "Box";
                    properties["collisionAssetId"] = "";
                    properties["center"] = { 0.0f, 0.0f, 0.0f };
                    properties["rotation"] = { 0.0f, 0.0f, 0.0f };
                    properties["size"] = { 1.0f, 1.0f, 1.0f };
                    properties["radius"] = 0.5f;
                    properties["height"] = 1.0f;
                    properties["trigger"] = false;
                    properties["material"] = {
                        { "friction", 0.5f },
                        { "restitution", 0.0f },
                        { "density", 1.0f }
                    };
                    properties["filter"] = {
                        { "layer", 1u },
                        { "mask", 0xFFFFFFFFu }
                    };
                };
                collider.presentation = PhysicsPresentation(
                    "Collider",
                    "Adds a box, sphere, or capsule collision shape.");

                SystemSettingsContract settings{};
                settings.version = 1;
                settings.defaultSettings = {
                    { "gravity", { 0.0f, -9.81f, 0.0f } },
                    { "allowSleeping", true },
                    { "emitPersistContactEvents", false }
                };
                settings.normalize = NormalizeSettings;

                bool success = context.componentRegistry.Register(
                    std::move(collider));
                success = context.componentRegistry.Register(
                    std::move(body)) && success;
                success = context.systemTypeRegistry.Register(SystemTypeInfo{
                    "PhysicsSystem",
                    [](const nlohmann::json& systemSettings)
                        -> std::unique_ptr<ISystem> {
                        return std::make_unique<PHYSICS::PhysicsSystem>(
                            ReadSettings(systemSettings));
                    },
                    "Physics",
                    std::string(RuntimeFeatureIds::Physics),
                    std::move(settings)
                }) && success;
                success = context.componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "ColliderComponent",
                        "PhysicsSystem",
                        120
                    }) && success;
                success = context.componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "PhysicsBodyComponent",
                        "PhysicsSystem",
                        120
                    }) && success;
                return success;
            }
        };
    }

    std::unique_ptr<IRuntimeFeature> CreatePhysicsRuntimeFeature() {
        return std::make_unique<PhysicsRuntimeFeature>();
    }

} // namespace HIKARI
