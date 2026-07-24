#include "Scene/Features/HIKARI_AnimationRuntimeFeature.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "Animation/Runtime/HIKARI_AnimationSystem.h"
#include "Animation/StateMachine/HIKARI_AnimationStateMachineSystem.h"
#include "Scene/Components/HIKARI_AnimationStateMachineComponent.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

namespace HIKARI {
    namespace {
        class AnimationRuntimeFeature final : public IRuntimeFeature {
        public:
            std::string_view GetFeatureId() const noexcept override {
                return RuntimeFeatureIds::Animation;
            }

            std::string_view GetDisplayName() const noexcept override {
                return "Animation";
            }

            std::string_view GetDescription() const noexcept override {
                return "Animation playback, state machines, and pose evaluation.";
            }

            std::span<const std::string_view>
                GetRequiredFeatureIds() const noexcept override {
                static constexpr std::array<std::string_view, 1> ids{
                    RuntimeFeatureIds::Rendering
                };
                return ids;
            }

            std::span<const std::string_view>
                GetComponentTypeNames() const noexcept override {
                static constexpr std::array<std::string_view, 2> names{
                    "AnimatorComponent",
                    "AnimationStateMachineComponent"
                };
                return names;
            }

            std::span<const std::string_view>
                GetSystemIds() const noexcept override {
                static constexpr std::array<std::string_view, 2> ids{
                    "AnimationStateMachineSystem",
                    "AnimationSystem"
                };
                return ids;
            }

            bool Register(RuntimeFeatureContext& context) override {
                ComponentTypeInfo animator{};
                animator.typeName = "AnimatorComponent";
                animator.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<AnimatorComponent>();
                };
                animator.requiredComponents = { "ModelComponent" };
                animator.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties = {
                        { "clip", "" },
                        { "clipModelAssetId", "" },
                        { "clipId", 0u },
                        { "timeSec", 0.0f },
                        { "speed", 1.0f },
                        { "defaultBlendDurationSec", 0.2f },
                        { "loop", true },
                        { "autoPlay", true },
                        { "playing", true },
                        { "finished", false }
                    };
                };
                animator.presentation = ComponentTypePresentation{
                    "Animator",
                    "Animation",
                    "Evaluates model clips and publishes a reusable pose.",
                    std::string(RuntimeFeatureIds::Animation)
                };

                bool success = context.componentRegistry.Register(
                    std::move(animator));
                ComponentTypeInfo stateMachine{};
                stateMachine.typeName = "AnimationStateMachineComponent";
                stateMachine.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<
                        AnimationStateMachineComponent>();
                };
                stateMachine.requiredComponents = { "AnimatorComponent" };
                stateMachine.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties = {
                        { "enabled", true },
                        { "stateMachineAssetGuid", "" },
                        { "playOnStart", true },
                        { "syncCharacterMotion", true }
                    };
                };
                stateMachine.presentation = ComponentTypePresentation{
                    "Animation State Machine",
                    "Animation",
                    "Selects animation states from parameters without owning pose evaluation.",
                    std::string(RuntimeFeatureIds::Animation)
                };
                success = context.componentRegistry.Register(
                    std::move(stateMachine)) && success;
                success = context.systemTypeRegistry.Register(
                    SystemTypeInfo{
                        "AnimationStateMachineSystem",
                        [](const nlohmann::json&)
                            -> std::unique_ptr<ISystem> {
                            return std::make_unique<
                                AnimationStateMachineSystem>();
                        },
                        "Animation",
                        std::string(RuntimeFeatureIds::Animation)
                    }) && success;
                success = context.systemTypeRegistry.Register(
                    SystemTypeInfo{
                        "AnimationSystem",
                        [](const nlohmann::json&)
                            -> std::unique_ptr<ISystem> {
                            return std::make_unique<AnimationSystem>();
                        },
                        "Animation",
                        std::string(RuntimeFeatureIds::Animation)
                    }) && success;
                success = context.componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "AnimationStateMachineComponent",
                        "AnimationStateMachineSystem",
                        140
                    }) && success;
                success = context.componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "AnimatorComponent",
                        "AnimationSystem",
                        150
                    }) && success;
                return success;
            }
        };
    }

    std::unique_ptr<IRuntimeFeature> CreateAnimationRuntimeFeature() {
        return std::make_unique<AnimationRuntimeFeature>();
    }

} // namespace HIKARI
