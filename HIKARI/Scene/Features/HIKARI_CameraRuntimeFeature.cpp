#include "Scene/Features/HIKARI_CameraRuntimeFeature.h"

#include <array>
#include <memory>

#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraActivationVolumeComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
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
                ComponentTypeInfo camera{};
                camera.typeName = "CameraComponent";
                camera.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<CameraComponent>();
                };
                camera.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["verticalFovDegrees"] = 60.0f;
                    properties["nearClip"] = 0.1f;
                    properties["farClip"] = 100.0f;
                };
                camera.presentation = CameraPresentation(
                    "Camera",
                    "Defines a renderable scene camera and its lens.");

                ComponentTypeInfo follow{};
                follow.typeName = "CameraFollowComponent";
                follow.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<CameraFollowComponent>();
                };
                follow.requiredComponents = {
                    "CameraComponent"
                };
                follow.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["priority"] = 0;
                    properties["targetObjectId"] = 0;
                    properties["useOwnerAsFallbackTarget"] = true;
                    properties["pivotOffset"] = nlohmann::json::array({
                        0.0f, 1.2f, 0.0f
                    });
                    properties["initialYawDegrees"] = 0.0f;
                    properties["initialPitchDegrees"] = 28.0f;
                    properties["initialDistance"] = 8.0f;
                    properties["minimumPitchDegrees"] = -65.0f;
                    properties["maximumPitchDegrees"] = 75.0f;
                    properties["minimumDistance"] = 1.0f;
                    properties["maximumDistance"] = 20.0f;
                    properties["followSmooth"] = 12.0f;
                    properties["lookSmooth"] = 14.0f;
                    properties["distanceSmooth"] = 18.0f;
                    properties["orbitInputEnabled"] = true;
                    properties["lookAction"] = "Gameplay.Look";
                    properties["zoomAction"] = "Gameplay.CameraZoom";
                    properties["recenterAction"] =
                        "Gameplay.CameraRecenter";
                    properties["yawSpeedDegreesPerSecond"] = 180.0f;
                    properties["pitchSpeedDegreesPerSecond"] = 140.0f;
                    properties["mouseSensitivityDegreesPerPixel"] =
                        0.12f;
                    properties["zoomSpeedUnitsPerSecond"] = 30.0f;
                    properties["mouseWheelZoomUnitsPerStep"] = 1.0f;
                    properties["invertVerticalLook"] = false;
                    properties["autoRecenterEnabled"] = false;
                    properties["autoRecenterDelaySeconds"] = 1.25f;
                    properties["autoRecenterSpeedDegreesPerSecond"] =
                        90.0f;
                    properties["collisionEnabled"] = true;
                    properties["collisionRadius"] = 0.25f;
                    properties["collisionPadding"] = 0.08f;
                    properties["collisionLayerMask"] = 0xFFFFFFFFu;
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

                bool success = context.componentRegistry.Register(
                    std::move(camera));
                success = context.componentRegistry.Register(
                    std::move(follow)) && success;
                success = context.componentRegistry.Register(
                    std::move(activationVolume)) && success;
                success = context.systemTypeRegistry.Register(SystemTypeInfo{
                    "CameraFollowSystem",
                    [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<CameraFollowSystem>();
                    },
                    "Camera Follow",
                    "Camera"
                }) && success;
                success = context.systemTypeRegistry.Register(SystemTypeInfo{
                    "CameraActivationVolumeSystem",
                    [](const nlohmann::json&) ->
                        std::unique_ptr<ISystem> {
                        return std::make_unique<
                            CameraActivationVolumeSystem>();
                    },
                    "Camera Activation Volumes",
                    "Camera"
                }) && success;
                success = context.componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "CameraFollowComponent",
                        "CameraFollowSystem",
                        190
                    }) && success;
                success = context.componentSystemPolicy.Register(
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
