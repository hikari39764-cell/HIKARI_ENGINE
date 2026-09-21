#include "Scene/Features/HIKARI_CameraRuntimeFeature.h"

#include <array>
#include <memory>

#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraActivationVolumeComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_CameraFollowSystem.h"
#include "Scene/HIKARI_CameraActivationVolumeSystem.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

namespace HIKARI {
    namespace {
        ComponentTypePresentation CameraPresentation(
            const char* displayName,
            const char* description) {

            return ComponentTypePresentation{
                displayName,
                "Camera",
                description,
                "Camera"
            };
        }

        class CameraRuntimeFeature final : public IRuntimeFeature {
        public:
            std::string_view GetFeatureId() const noexcept override {
                return RuntimeFeatureIds::Camera;
            }

            std::string_view GetDisplayName() const noexcept override {
                return "Camera";
            }

            std::string_view GetDescription() const noexcept override {
                return "Scene cameras, reusable gameplay rigs, and director arbitration.";
            }

            std::span<const std::string_view>
                GetComponentTypeNames() const noexcept override {
                static constexpr std::array<std::string_view, 3> names{
                    "CameraComponent",
                    "CameraFollowComponent",
                    "CameraActivationVolumeComponent"
                };
                return names;
            }

            std::span<const std::string_view>
                GetSystemIds() const noexcept override {
                static constexpr std::array<std::string_view, 2> ids{
                    "CameraFollowSystem",
                    "CameraActivationVolumeSystem"
                };
                return ids;
            }

            bool Register(RuntimeFeatureContext& context) override {
                ComponentRegistry& componentRegistry =
                    RuntimeFeatureContextAccess::Components(context);
                SystemTypeRegistry& systemTypeRegistry =
                    RuntimeFeatureContextAccess::Systems(context);
                ComponentSystemPolicy& componentSystemPolicy =
                    RuntimeFeatureContextAccess::Policy(context);
                ComponentTypeInfo camera{};
                camera.typeName = "CameraComponent";
                camera.factory = []() -> std::unique_ptr<IComponent> {
                    return IComponent::Create<CameraComponent>();
                };
                camera.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["verticalFovDegrees"] = 60.0f;
                    properties["nearClip"] = Camera3D::kDefaultNearZ;
                    properties["farClip"] = Camera3D::kDefaultFarZ;
                };
                camera.presentation = CameraPresentation(
                    "Camera",
                    "Defines a renderable scene camera and its lens.");

                ComponentTypeInfo follow{};
                follow.typeName = "CameraFollowComponent";
                follow.factory = []() -> std::unique_ptr<IComponent> {
                    return IComponent::Create<CameraFollowComponent>();
                };
                follow.requiredComponents = {
                    "CameraComponent"
                };
                follow.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    CameraFollowComponent defaults;
                    defaults.Serialize(properties);
                };
                follow.presentation = CameraPresentation(
                    "Gameplay Camera Rig",
                    "Orbit-follow gameplay camera with input, recentering, smoothing, and collision avoidance.");

                ComponentTypeInfo activationVolume{};
                activationVolume.typeName =
                    "CameraActivationVolumeComponent";
                activationVolume.factory =
                    []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<
                        CameraActivationVolumeComponent>();
                };
                activationVolume.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["subjectObjectId"] = 0;
                    properties["cameraObjectId"] = 0;
                    properties["halfExtents"] = nlohmann::json::array({
                        4.0f, 3.0f, 4.0f
                    });
                    properties["priority"] = 50;
                    properties["blendSeconds"] = 0.5f;
                    properties["affectsControlBasis"] = true;
                };
                activationVolume.presentation = CameraPresentation(
                    "Camera Activation Volume",
                    "Activates a camera while a selected subject is inside this oriented volume.");

                bool success = componentRegistry.Register(
                    std::move(camera));
                success = componentRegistry.Register(
                    std::move(follow)) && success;
                success = componentRegistry.Register(
                    std::move(activationVolume)) && success;
                success = systemTypeRegistry.Register(SystemTypeInfo{
                    "CameraFollowSystem",
                    [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<CameraFollowSystem>();
                    },
                    "Camera Follow",
                    "Camera"
                }) && success;
                success = systemTypeRegistry.Register(SystemTypeInfo{
                    "CameraActivationVolumeSystem",
                    [](const nlohmann::json&) ->
                        std::unique_ptr<ISystem> {
                        return std::make_unique<
                            CameraActivationVolumeSystem>();
                    },
                    "Camera Activation Volumes",
                    "Camera"
                }) && success;
                success = componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "CameraFollowComponent",
                        "CameraFollowSystem",
                        190
                    }) && success;
                success = componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "CameraActivationVolumeComponent",
                        "CameraActivationVolumeSystem",
                        200
                    }) && success;
                return success;
            }
        };
    }

    std::unique_ptr<IRuntimeFeature> CreateCameraRuntimeFeature() {
        return std::make_unique<CameraRuntimeFeature>();
    }

} // namespace HIKARI
