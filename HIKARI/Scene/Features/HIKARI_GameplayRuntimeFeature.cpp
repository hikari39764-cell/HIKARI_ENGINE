#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"

#include <array>
#include <algorithm>
#include <memory>
#include <string>

#include "Scene/Components/HIKARI_CharacterInputComponent.h"
#include "Scene/Components/HIKARI_CharacterLocomotionComponent.h"
#include "Scene/Components/HIKARI_SpawnPointComponent.h"
#include "Scene/Components/HIKARI_DialogueInteractorComponent.h"
#include "Scene/Components/HIKARI_DialogueTriggerComponent.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/Features/HIKARI_RuntimeFeatureContextAccess.h"
#include "Scene/HIKARI_CharacterInputSystem.h"
#include "Scene/HIKARI_CharacterLocomotionSystem.h"
#include "Scene/HIKARI_CharacterMotionStateSystem.h"
#include "Scene/HIKARI_DialogueRuntimeSystem.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

namespace HIKARI {
namespace {
ComponentTypePresentation GameplayPresentation(const char* displayName, const char* description) {
    return ComponentTypePresentation{displayName, "Gameplay", description,
                                     std::string(RuntimeFeatureIds::GameplayBasic)};
}

bool WasDependencyAdded(const std::vector<std::string>& added, std::string_view typeName) {
    return std::find(added.begin(), added.end(), typeName) != added.end();
}

SceneComponentData* FindComponentData(SceneObjectData& object, std::string_view typeName) {
    const auto found =
        std::find_if(object.components.begin(), object.components.end(),
                     [typeName](const SceneComponentData& component) { return component.type == typeName; });
    return found != object.components.end() ? &*found : nullptr;
}

class GameplayRuntimeFeature final : public IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return RuntimeFeatureIds::GameplayBasic; }

    std::string_view GetDisplayName() const noexcept override { return "Gameplay Motion"; }

    std::string_view GetDescription() const noexcept override {
        return "Replaceable input and locomotion built on standard physics components.";
    }

    std::span<const std::string_view> GetRequiredFeatureIds() const noexcept override {
        static constexpr std::array<std::string_view, 1> ids{RuntimeFeatureIds::Physics};
        return ids;
    }

    bool Register(RuntimeFeatureContext& context) override {
        ComponentRegistry& componentRegistry = RuntimeFeatureContextAccess::Components(context);
        SystemTypeRegistry& systemTypeRegistry = RuntimeFeatureContextAccess::Systems(context);
        ComponentSystemPolicy& componentSystemPolicy = RuntimeFeatureContextAccess::Policy(context);
        ComponentTypeInfo input{};
        input.typeName = "CharacterInputComponent";
        input.factory = []() -> std::unique_ptr<IComponent> { return IComponent::Create<CharacterInputComponent>(); };
        input.requiredComponents = {"CharacterLocomotionComponent"};
        input.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            properties = {{"enabled", true},
                          {"priority", 100},
                          {"moveAction", "Gameplay.Move"},
                          {"lookAction", "Gameplay.Look"},
                          {"jumpAction", "Gameplay.Jump"},
                          {"sprintAction", "Gameplay.Sprint"}};
        };
        input.presentation =
            GameplayPresentation("Character Input", "Maps project actions into a replaceable motion intent source.");

        ComponentTypeInfo locomotion{};
        locomotion.typeName = "CharacterLocomotionComponent";
        locomotion.factory = []() -> std::unique_ptr<IComponent> {
            return std::make_unique<CharacterLocomotionComponent>();
        };
        locomotion.requiredComponents = {"PhysicsBodyComponent"};
        locomotion.optionalComponents = {"CharacterInputComponent"};
        locomotion.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            properties = {{"enabled", true},
                          {"motionPriority", 100},
                          {"movementSpace", "Camera"},
                          {"maximumSpeed", 5.0f},
                          {"sprintMultiplier", 1.5f},
                          {"acceleration", 30.0f},
                          {"deceleration", 40.0f},
                          {"airAcceleration", 10.0f},
                          {"airDeceleration", 12.0f},
                          {"turnSpeedDegrees", 720.0f},
                          {"jumpHeight", 1.2f},
                          {"jumpBufferSeconds", 0.12f},
                          {"groundGraceSeconds", 0.10f},
                          {"maximumFallSpeed", 55.0f},
                          {"maximumSlopeAngleDegrees", 42.0f},
                          {"stepHeight", 0.25f},
                          {"stickToFloorDistance", 0.28f},
                          {"stepForwardTestDistance", 0.30f},
                          {"edgeProbeDistance", 0.45f},
                          {"edgeProbeRadius", 0.18f},
                          {"maximumGroundedDrop", 0.35f},
                          {"characterPadding", 0.02f},
                          {"predictiveContactDistance", 0.1f},
                          {"penetrationRecoverySpeed", 1.0f},
                          {"maximumCollisionHits", 256u},
                          {"cancelVelocityTowardsSteepSlopes", true},
                          {"edgeGuardEnabled", true},
                          {"enhancedInternalEdgeRemoval", true},
                          {"inputDeadZone", 0.08f},
                          {"rotateToMove", true}};
        };
        locomotion.configureDependencies = [](SceneObjectData& object, const std::vector<std::string>& added) {
            // Character locomotion is a controller for the existing
            // physics body, not a second body implementation. The
            // contract therefore applies equally to a pre-existing
            // PhysicsBodyComponent and an automatically added one.
            if (SceneComponentData* body = FindComponentData(object, "PhysicsBodyComponent")) {
                body->properties["motionType"] = "Kinematic";
                body->properties["mass"] = 70.0f;
                body->properties["allowSleeping"] = false;
                body->properties["continuousCollision"] = true;
            }
            if (WasDependencyAdded(added, "ColliderComponent")) {
                if (SceneComponentData* collider = FindComponentData(object, "ColliderComponent")) {
                    collider->properties["fitMode"] = "Manual";
                    collider->properties["shape"] = "Capsule";
                    collider->properties["center"] = {0.0f, 0.9f, 0.0f};
                    collider->properties["radius"] = 0.35f;
                    collider->properties["height"] = 1.8f;
                    collider->properties["trigger"] = false;
                    collider->properties["filter"] = {{"layer", 1u << 3u}, {"mask", 0xFFFFFFFFu}};
                }
            }
        };
        locomotion.presentation =
            GameplayPresentation("Character Locomotion", "Converts motion intent into kinematic movement requests.");

        ComponentTypeInfo spawnPoint{};
        spawnPoint.typeName = "SpawnPointComponent";
        spawnPoint.factory = []() -> std::unique_ptr<IComponent> { return IComponent::Create<SpawnPointComponent>(); };
        spawnPoint.initializeDefaults = [](const SceneObjectData& object, nlohmann::json& properties) {
            properties["spawnPointId"] = object.name.empty() ? "DefaultSpawn" : object.name;
            properties["enabled"] = true;
        };
        spawnPoint.presentation = GameplayPresentation("Spawn Point", "Names a reusable spawn location in the scene.");

        ComponentTypeInfo dialogueInteractor{};
        dialogueInteractor.typeName = "DialogueInteractorComponent";
        dialogueInteractor.factory = []() -> std::unique_ptr<IComponent> {
            return std::make_unique<DialogueInteractorComponent>();
        };
        dialogueInteractor.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            properties = {{"enabled", true}};
        };
        dialogueInteractor.presentation = GameplayPresentation(
            "Dialogue Interactor", "Marks an actor that can enter and activate dialogue trigger volumes.");

        ComponentTypeInfo dialogueTrigger{};
        dialogueTrigger.typeName = "DialogueTriggerComponent";
        dialogueTrigger.factory = []() -> std::unique_ptr<IComponent> {
            return IComponent::Create<DialogueTriggerComponent>();
        };
        dialogueTrigger.requiredComponents = {"PhysicsBodyComponent", "ColliderComponent"};
        dialogueTrigger.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            properties = {
                {"enabled", true},
                {"dialogueAssetGuid", ""},
                {"locale", "ja-JP"},
                {"activation", "OnEnter"},
                {"interactAction", "Gameplay.Interact"},
                {"pauseWorld", true},
                {"oneShot", false},
            };
        };
        dialogueTrigger.configureDependencies = [](SceneObjectData& object, const std::vector<std::string>& added) {
            if (WasDependencyAdded(added, "PhysicsBodyComponent")) {
                if (SceneComponentData* body = FindComponentData(object, "PhysicsBodyComponent")) {
                    body->properties["motionType"] = "Static";
                    body->properties["gravityScale"] = 0.0f;
                    body->properties["allowSleeping"] = true;
                }
            }
            if (WasDependencyAdded(added, "ColliderComponent")) {
                if (SceneComponentData* collider = FindComponentData(object, "ColliderComponent")) {
                    collider->properties["fitMode"] = "Manual";
                    collider->properties["shape"] = "Box";
                    collider->properties["center"] = {0.0f, 1.0f, 0.0f};
                    collider->properties["size"] = {4.0f, 2.0f, 4.0f};
                    collider->properties["trigger"] = true;
                }
            }
        };
        dialogueTrigger.presentation = GameplayPresentation(
            "Dialogue Trigger", "Binds a formal Dialogue asset to an editable physics trigger volume.");

        bool success = componentRegistry.Register(std::move(locomotion));
        success = componentRegistry.Register(std::move(input)) && success;
        success = componentRegistry.Register(std::move(spawnPoint)) && success;
        success = componentRegistry.Register(std::move(dialogueInteractor)) && success;
        success = componentRegistry.Register(std::move(dialogueTrigger)) && success;

        success = systemTypeRegistry.Register(SystemTypeInfo{"CharacterInputSystem",
                                                             [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                                                                 return std::make_unique<CharacterInputSystem>();
                                                             },
                                                             "Character Input",
                                                             std::string(RuntimeFeatureIds::GameplayBasic)}) &&
                  success;
        success = systemTypeRegistry.Register(SystemTypeInfo{"DialogueRuntimeSystem",
                                                             [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                                                                 return std::make_unique<DialogueRuntimeSystem>();
                                                             },
                                                             "Dialogue Runtime",
                                                             std::string(RuntimeFeatureIds::GameplayBasic)}) &&
                  success;
        success = systemTypeRegistry.Register(SystemTypeInfo{"CharacterMotionStateSystem",
                                                             [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                                                                 return std::make_unique<CharacterMotionStateSystem>();
                                                             },
                                                             "Character Motion State",
                                                             std::string(RuntimeFeatureIds::GameplayBasic)}) &&
                  success;
        success = systemTypeRegistry.Register(SystemTypeInfo{"CharacterLocomotionSystem",
                                                             [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                                                                 return std::make_unique<CharacterLocomotionSystem>();
                                                             },
                                                             "Character Locomotion",
                                                             std::string(RuntimeFeatureIds::GameplayBasic)}) &&
                  success;
        success = componentSystemPolicy.Register(
                      ComponentSystemRule{"CharacterInputComponent", "CharacterInputSystem", 90}) &&
                  success;
        success = componentSystemPolicy.Register(
                      ComponentSystemRule{"CharacterLocomotionComponent", "CharacterLocomotionSystem", 110}) &&
                  success;
        success = componentSystemPolicy.Register(
                      ComponentSystemRule{"CharacterLocomotionComponent", "CharacterMotionStateSystem", 130}) &&
                  success;
        success = componentSystemPolicy.Register(
                      ComponentSystemRule{"DialogueTriggerComponent", "DialogueRuntimeSystem", 85}) &&
                  success;
        return success;
    }
};
} // namespace

std::unique_ptr<IRuntimeFeature> CreateGameplayRuntimeFeature() { return std::make_unique<GameplayRuntimeFeature>(); }

} // namespace HIKARI
