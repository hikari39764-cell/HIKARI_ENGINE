#include "Editor/SystemAuthoring/HIKARI_BuiltInSystemAuthoring.h"

#include <array>
#include <utility>

#include "Editor/SystemAuthoring/HIKARI_SystemAuthoringRegistry.h"

namespace HIKARI::EDITOR {
    namespace {

        SystemAuthoringDescriptor RuntimeOnly(
            const char* systemId,
            const char* description,
            const char* hint) {

            SystemAuthoringDescriptor descriptor{};
            descriptor.systemId = systemId;
            descriptor.description = description;
            descriptor.scope = SystemAuthoringScope::RuntimeOnly;
            descriptor.configurationHint = hint;
            return descriptor;
        }

        SystemAuthoringDescriptor ComponentOwned(
            const char* systemId,
            const char* description,
            const char* hint) {

            SystemAuthoringDescriptor descriptor{};
            descriptor.systemId = systemId;
            descriptor.description = description;
            descriptor.scope = SystemAuthoringScope::ComponentOwned;
            descriptor.configurationHint = hint;
            return descriptor;
        }

        SystemAuthoringDescriptor ProjectOwned(
            const char* systemId,
            const char* description,
            const char* hint) {

            SystemAuthoringDescriptor descriptor{};
            descriptor.systemId = systemId;
            descriptor.description = description;
            descriptor.scope = SystemAuthoringScope::ProjectSettings;
            descriptor.configurationHint = hint;
            return descriptor;
        }

        SystemAuthoringDescriptor PhysicsOwned() {
            SystemAuthoringDescriptor descriptor = ComponentOwned(
                "PhysicsSystem",
                "Synchronizes scene transforms with the active physics backend.",
                "Body and shape settings live on Physics Body and Collider components.");

            SystemSettingField gravity{};
            gravity.jsonPointer = "/gravity";
            gravity.displayName = "Gravity";
            gravity.description = "World-space acceleration applied by the backend.";
            gravity.type = SystemSettingFieldType::Vec3;

            SystemSettingField sleeping{};
            sleeping.jsonPointer = "/allowSleeping";
            sleeping.displayName = "Allow Sleeping";
            sleeping.description = "Allows inactive dynamic bodies to sleep.";
            sleeping.type = SystemSettingFieldType::Boolean;

            SystemSettingField persistEvents{};
            persistEvents.jsonPointer = "/emitPersistContactEvents";
            persistEvents.displayName = "Emit Contact Stay";
            persistEvents.description =
                "Publishes Persist events every fixed step while contact continues.";
            persistEvents.type = SystemSettingFieldType::Boolean;
            persistEvents.advanced = true;

            descriptor.fields = {
                std::move(gravity),
                std::move(sleeping),
                std::move(persistEvents)
            };
            return descriptor;
        }
    }

    void RegisterBuiltInSystemAuthoring(
        SystemAuthoringRegistry& registry) {

        std::array<SystemAuthoringDescriptor, 7> descriptors{
            ProjectOwned(
                "ModelRenderSystem",
                "Submits scene models to the renderer.",
                "Rendering quality and pipeline choices are configured in the Quality panel."),
            ComponentOwned(
                "AnimationSystem",
                "Advances object animation state.",
                "Animation clips, speed, looping, and autoplay are configured on Animator components."),
            ComponentOwned(
                "CameraFollowSystem",
                "Updates gameplay cameras that follow scene targets.",
                "Target, offset, and smoothing are configured on Camera Follow components."),
            ComponentOwned(
                "CharacterInputSystem",
                "Maps project input actions into motion intent.",
                "Action names are configured on Character Input components; physical bindings are edited in Project Input."),
            ComponentOwned(
                "CharacterLocomotionSystem",
                "Converts motion intent into kinematic physics requests.",
                "Movement rules are configured on Character Locomotion; collision remains on Collider and Physics Body."),
            ComponentOwned(
                "SequencePlayerSystem",
                "Runs reusable Sequence assets during gameplay.",
                "Sequence assets, playback policy, and binding slots are configured on Sequence Player components."),
            PhysicsOwned(),
        };

        for (SystemAuthoringDescriptor& descriptor : descriptors) {
            registry.Register(std::move(descriptor));
        }
    }

} // namespace HIKARI::EDITOR
